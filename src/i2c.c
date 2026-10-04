/*
 * i2c.c - Register-level, polling I2C1 master driver for the STM32F4 "v1"
 * I2C peripheral. 400 kHz fast mode, PB6 = SCL, PB7 = SDA.
 *
 * The F4 I2C block is famous for its awkward receive sequences: the hardware
 * ACKs/NACKs the NEXT byte while you are still reading the current one, so
 * the ACK bit and STOP must be programmed at exactly the right moment
 * depending on how many bytes are left (RM0090 section 27.3.3, "Master
 * receiver"). The three cases N=1, N=2 and N>2 below follow that section.
 *
 * Every wait has a timeout so an unplugged or stuck sensor returns an error
 * instead of hanging the task (important here: the MPU-6050 may not be
 * connected at all).
 */
#include "i2c.h"
#include "stm32f407.h"
#include "clock.h"
#include <stddef.h>

#define SCL_PIN 6U
#define SDA_PIN 7U
#define I2C_AF  4U

/* Loop-count timeout (~several ms at 168 MHz); independent of timers so it
 * works before the RTOS starts and in emulators. */
#define I2C_TIMEOUT_LOOPS 100000UL

volatile i2c_stats_t g_i2c_stats;

static uint32_t g_pclk1_mhz(void)
{
    return g_pclk1_hz / 1000000UL;
}

/* ------------------------------------------------------------------------- */
static int wait_sr1(uint32_t flag)
{
    for (uint32_t n = 0; n < I2C_TIMEOUT_LOOPS; n++) {
        uint32_t sr1 = I2C1->SR1;
        if (sr1 & I2C_SR1_AF) {             /* slave did not ACK */
            I2C1->SR1 &= ~I2C_SR1_AF;
            return I2C_ERR_NACK;
        }
        if (sr1 & (I2C_SR1_BERR | I2C_SR1_ARLO)) {
            I2C1->SR1 &= ~(I2C_SR1_BERR | I2C_SR1_ARLO);
            return I2C_ERR_BUS;
        }
        if (sr1 & flag) {
            return I2C_OK;
        }
    }
    return I2C_ERR_TIMEOUT;
}

static int wait_not_busy(void)
{
    for (uint32_t n = 0; n < I2C_TIMEOUT_LOOPS; n++) {
        if (!(I2C1->SR2 & I2C_SR2_BUSY)) {
            return I2C_OK;
        }
    }
    return I2C_ERR_TIMEOUT;
}

static void wait_stop_cleared(void)
{
    for (uint32_t n = 0; n < I2C_TIMEOUT_LOOPS; n++) {
        if (!(I2C1->CR1 & I2C_CR1_STOP)) {
            return;
        }
    }
}

static inline void clear_addr(void)
{
    (void)I2C1->SR1;   /* ADDR is cleared by reading SR1 followed by SR2 */
    (void)I2C1->SR2;
}

static int fail(int err)
{
    I2C1->CR1 |= I2C_CR1_STOP;
    I2C1->CR1 &= ~I2C_CR1_POS;
    wait_stop_cleared();
    g_i2c_stats.errors++;
    if (err == I2C_ERR_NACK)    { g_i2c_stats.nacks++; }
    if (err == I2C_ERR_TIMEOUT) { g_i2c_stats.timeouts++; }
    if (err != I2C_ERR_NACK) {
        (void)i2c1_init();       /* timeout / bus error: reset the peripheral */
    }
    return err;
}

/* START + 7-bit address. dir: 0 = write, 1 = read */
static int start_and_address(uint8_t addr7, uint8_t dir)
{
    I2C1->CR1 |= I2C_CR1_START;
    int rc = wait_sr1(I2C_SR1_SB);
    if (rc != I2C_OK) {
        return rc;
    }
    I2C1->DR = (uint32_t)((addr7 << 1) | (dir & 1U));
    return wait_sr1(I2C_SR1_ADDR);
}

/* ------------------------------------------------------------------------- */
/*
 * Bus recovery: if a slave was reset mid-transfer it may still be holding
 * SDA low waiting for clocks. Bit-bang up to 9 SCL pulses until it lets go,
 * then a STOP condition. (Classic interview question: "the I2C bus is stuck
 * BUSY after a reset, what do you do?")
 */
static void i2c1_bus_recover(void)
{
    /* SCL as open-drain GPIO output, SDA as input (both with pull-ups) */
    GPIOB->OTYPER |= (1UL << SCL_PIN) | (1UL << SDA_PIN);
    GPIOB->PUPDR = (GPIOB->PUPDR & ~((3UL << (SCL_PIN * 2)) | (3UL << (SDA_PIN * 2)))) |
                   (GPIO_PULL_UP << (SCL_PIN * 2)) | (GPIO_PULL_UP << (SDA_PIN * 2));
    GPIOB->BSRR  = (1UL << SCL_PIN);
    GPIOB->MODER = (GPIOB->MODER & ~((3UL << (SCL_PIN * 2)) | (3UL << (SDA_PIN * 2)))) |
                   (GPIO_MODE_OUTPUT << (SCL_PIN * 2)) | (GPIO_MODE_INPUT << (SDA_PIN * 2));

    for (uint32_t i = 0; i < 9U && !(GPIOB->IDR & (1UL << SDA_PIN)); i++) {
        GPIOB->BSRR = (1UL << (SCL_PIN + 16));          /* SCL low  */
        for (volatile uint32_t d = 0; d < 200U; d++) { }
        GPIOB->BSRR = (1UL << SCL_PIN);                 /* SCL high */
        for (volatile uint32_t d = 0; d < 200U; d++) { }
        g_i2c_stats.bus_recoveries++;
    }
}

int i2c1_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN;
    RCC->APB1ENR |= RCC_APB1ENR_I2C1EN;
    (void)RCC->APB1ENR;

    i2c1_bus_recover();

    /* PB6/PB7: alternate function 4, open-drain, high speed, pull-up.
     * The internal ~40k pull-ups only keep the lines idle-high when nothing
     * is connected; the GY-521 module has its own 4.7k pull-ups. */
    GPIOB->AFR[0] = (GPIOB->AFR[0] & ~((0xFUL << (SCL_PIN * 4)) | (0xFUL << (SDA_PIN * 4)))) |
                    (I2C_AF << (SCL_PIN * 4)) | (I2C_AF << (SDA_PIN * 4));
    GPIOB->OTYPER  |= (1UL << SCL_PIN) | (1UL << SDA_PIN);
    GPIOB->OSPEEDR |= (3UL << (SCL_PIN * 2)) | (3UL << (SDA_PIN * 2));
    GPIOB->PUPDR = (GPIOB->PUPDR & ~((3UL << (SCL_PIN * 2)) | (3UL << (SDA_PIN * 2)))) |
                   (GPIO_PULL_UP << (SCL_PIN * 2)) | (GPIO_PULL_UP << (SDA_PIN * 2));
    GPIOB->MODER = (GPIOB->MODER & ~((3UL << (SCL_PIN * 2)) | (3UL << (SDA_PIN * 2)))) |
                   (GPIO_MODE_AF << (SCL_PIN * 2)) | (GPIO_MODE_AF << (SDA_PIN * 2));

    /* Full peripheral reset, then a software reset of the I2C state machine */
    RCC->APB1RSTR |= RCC_APB1RSTR_I2C1RST;
    RCC->APB1RSTR &= ~RCC_APB1RSTR_I2C1RST;
    I2C1->CR1 = I2C_CR1_SWRST;
    I2C1->CR1 = 0;

    /*
     * Timing for PCLK1 = 42 MHz, 400 kHz fast mode, DUTY = 0 (Tlow:Thigh = 2:1):
     *   CR2.FREQ = 42                              (PCLK1 in MHz)
     *   CCR      = PCLK1 / (3 * 400 kHz) = 35      -> 400 kHz
     *   TRISE    = 300 ns * 42 MHz + 1   = 13      (max rise time in fast mode)
     */
    uint32_t pclk_mhz = g_pclk1_mhz();
    I2C1->CR2   = pclk_mhz & 0x3FU;
    uint32_t ccr = (pclk_mhz * 1000000UL) / (3UL * 400000UL);
    if (ccr < 1U) {
        ccr = 1U;
    }
    I2C1->CCR   = I2C_CCR_FS | (ccr & 0xFFFU);
    I2C1->TRISE = ((pclk_mhz * 300UL) / 1000UL) + 1UL;
    I2C1->CR1   = I2C_CR1_PE;

    return I2C_OK;
}

/* ------------------------------------------------------------------------- */
int i2c1_write_reg(uint8_t addr7, uint8_t reg, uint8_t value)
{
    int rc;
    if ((rc = wait_not_busy()) != I2C_OK)              return fail(rc);
    if ((rc = start_and_address(addr7, 0)) != I2C_OK)  return fail(rc);
    clear_addr();

    I2C1->DR = reg;
    if ((rc = wait_sr1(I2C_SR1_TXE)) != I2C_OK)        return fail(rc);
    I2C1->DR = value;
    if ((rc = wait_sr1(I2C_SR1_BTF)) != I2C_OK)        return fail(rc);

    I2C1->CR1 |= I2C_CR1_STOP;
    wait_stop_cleared();
    g_i2c_stats.transfers++;
    return I2C_OK;
}

int i2c1_read_regs(uint8_t addr7, uint8_t reg, uint8_t *buf, uint32_t len)
{
    int rc;
    if (len == 0U || buf == NULL) {
        return I2C_ERR_PARAM;
    }

    /* Phase 1: write the register address */
    if ((rc = wait_not_busy()) != I2C_OK)              return fail(rc);
    I2C1->CR1 |= I2C_CR1_ACK;
    if ((rc = start_and_address(addr7, 0)) != I2C_OK)  return fail(rc);
    clear_addr();
    I2C1->DR = reg;
    if ((rc = wait_sr1(I2C_SR1_BTF)) != I2C_OK)        return fail(rc);

    /* Phase 2: repeated START, address + read */
    if ((rc = start_and_address(addr7, 1)) != I2C_OK)  return fail(rc);

    if (len == 1U) {
        /* NACK the only byte, STOP right after ADDR is cleared */
        I2C1->CR1 &= ~I2C_CR1_ACK;
        uint32_t pm = __get_PRIMASK();
        __disable_irq();                  /* ADDR clear + STOP must be back-to-back */
        clear_addr();
        I2C1->CR1 |= I2C_CR1_STOP;
        __set_PRIMASK(pm);
        if ((rc = wait_sr1(I2C_SR1_RXNE)) != I2C_OK)   return fail(rc);
        buf[0] = (uint8_t)I2C1->DR;
    } else if (len == 2U) {
        /* POS=1: the ACK bit applies to the NEXT byte -> byte 2 gets NACKed */
        I2C1->CR1 |= I2C_CR1_POS;
        uint32_t pm = __get_PRIMASK();
        __disable_irq();
        clear_addr();
        I2C1->CR1 &= ~I2C_CR1_ACK;
        __set_PRIMASK(pm);
        if ((rc = wait_sr1(I2C_SR1_BTF)) != I2C_OK)    return fail(rc);  /* both bytes in */
        I2C1->CR1 |= I2C_CR1_STOP;
        buf[0] = (uint8_t)I2C1->DR;
        buf[1] = (uint8_t)I2C1->DR;
        I2C1->CR1 &= ~I2C_CR1_POS;
    } else {
        clear_addr();
        uint32_t i = 0;
        /* Read with ACK until 3 bytes remain */
        while ((len - i) > 3U) {
            if ((rc = wait_sr1(I2C_SR1_RXNE)) != I2C_OK) return fail(rc);
            buf[i++] = (uint8_t)I2C1->DR;
        }
        /* N-2 in DR, N-1 in shift register */
        if ((rc = wait_sr1(I2C_SR1_BTF)) != I2C_OK)    return fail(rc);
        I2C1->CR1 &= ~I2C_CR1_ACK;                     /* NACK the last byte */
        buf[i++] = (uint8_t)I2C1->DR;                  /* N-2 */
        if ((rc = wait_sr1(I2C_SR1_BTF)) != I2C_OK)    return fail(rc);
        I2C1->CR1 |= I2C_CR1_STOP;
        buf[i++] = (uint8_t)I2C1->DR;                  /* N-1 */
        if ((rc = wait_sr1(I2C_SR1_RXNE)) != I2C_OK)   return fail(rc);
        buf[i++] = (uint8_t)I2C1->DR;                  /* N   */
    }

    wait_stop_cleared();
    I2C1->CR1 |= I2C_CR1_ACK;
    g_i2c_stats.transfers++;
    return I2C_OK;
}
