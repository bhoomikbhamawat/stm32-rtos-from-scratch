/*
 * stm32f407.h - Hand-written register map for the STM32F407VG (no HAL, no CMSIS).
 *
 * Only the peripherals this project touches are described. Every address and
 * bit position comes from:
 *   - RM0090 (STM32F4xx reference manual)
 *   - PM0214 (Cortex-M4 programming manual) for the core peripherals
 *
 * Interview note: writing your own register structs is a common "bare-metal"
 * screening topic. Know why every field is `volatile` (the hardware can change
 * it, and every write has a side effect, so the compiler must never cache or
 * drop an access) and why struct layout must match the memory map exactly.
 */
#ifndef STM32F407_H
#define STM32F407_H

#include <stdint.h>

#define __IO volatile

/* ------------------------------------------------------------------------- */
/* RCC - Reset and Clock Control (0x4002_3800)                               */
/* ------------------------------------------------------------------------- */
typedef struct {
    __IO uint32_t CR;          /* 0x00 */
    __IO uint32_t PLLCFGR;     /* 0x04 */
    __IO uint32_t CFGR;        /* 0x08 */
    __IO uint32_t CIR;         /* 0x0C */
    __IO uint32_t AHB1RSTR;    /* 0x10 */
    __IO uint32_t AHB2RSTR;    /* 0x14 */
    __IO uint32_t AHB3RSTR;    /* 0x18 */
    uint32_t      RESERVED0;   /* 0x1C */
    __IO uint32_t APB1RSTR;    /* 0x20 */
    __IO uint32_t APB2RSTR;    /* 0x24 */
    uint32_t      RESERVED1[2];/* 0x28-0x2C */
    __IO uint32_t AHB1ENR;     /* 0x30 */
    __IO uint32_t AHB2ENR;     /* 0x34 */
    __IO uint32_t AHB3ENR;     /* 0x38 */
    uint32_t      RESERVED2;   /* 0x3C */
    __IO uint32_t APB1ENR;     /* 0x40 */
    __IO uint32_t APB2ENR;     /* 0x44 */
} RCC_TypeDef;

#define RCC ((RCC_TypeDef *)0x40023800UL)

#define RCC_CR_HSION        (1UL << 0)
#define RCC_CR_HSIRDY       (1UL << 1)
#define RCC_CR_HSEON        (1UL << 16)
#define RCC_CR_HSERDY       (1UL << 17)
#define RCC_CR_PLLON        (1UL << 24)
#define RCC_CR_PLLRDY       (1UL << 25)

#define RCC_PLLCFGR_PLLM_Pos    0
#define RCC_PLLCFGR_PLLN_Pos    6
#define RCC_PLLCFGR_PLLP_Pos    16
#define RCC_PLLCFGR_PLLSRC_HSE  (1UL << 22)
#define RCC_PLLCFGR_PLLQ_Pos    24

#define RCC_CFGR_SW_Msk     (3UL << 0)
#define RCC_CFGR_SW_PLL     (2UL << 0)
#define RCC_CFGR_SWS_Msk    (3UL << 2)
#define RCC_CFGR_SWS_PLL    (2UL << 2)
#define RCC_CFGR_HPRE_Msk   (0xFUL << 4)
#define RCC_CFGR_PPRE1_Msk  (7UL << 10)
#define RCC_CFGR_PPRE1_DIV4 (5UL << 10)
#define RCC_CFGR_PPRE2_Msk  (7UL << 13)
#define RCC_CFGR_PPRE2_DIV2 (4UL << 13)

#define RCC_AHB1ENR_GPIOAEN (1UL << 0)
#define RCC_AHB1ENR_GPIOBEN (1UL << 1)
#define RCC_AHB1ENR_GPIODEN (1UL << 3)
#define RCC_AHB1ENR_CCMEN   (1UL << 20)

#define RCC_APB1ENR_TIM2EN  (1UL << 0)
#define RCC_APB1ENR_I2C1EN  (1UL << 21)
#define RCC_APB1ENR_PWREN   (1UL << 28)
#define RCC_APB1RSTR_I2C1RST (1UL << 21)

/* ------------------------------------------------------------------------- */
/* FLASH interface (0x4002_3C00)                                             */
/* ------------------------------------------------------------------------- */
typedef struct {
    __IO uint32_t ACR;
} FLASH_TypeDef;

#define FLASH ((FLASH_TypeDef *)0x40023C00UL)

#define FLASH_ACR_LATENCY_Msk (7UL << 0)
#define FLASH_ACR_LATENCY_5WS (5UL << 0)
#define FLASH_ACR_PRFTEN      (1UL << 8)
#define FLASH_ACR_ICEN        (1UL << 9)
#define FLASH_ACR_DCEN        (1UL << 10)

/* ------------------------------------------------------------------------- */
/* PWR (0x4000_7000)                                                         */
/* ------------------------------------------------------------------------- */
typedef struct {
    __IO uint32_t CR;
    __IO uint32_t CSR;
} PWR_TypeDef;

#define PWR ((PWR_TypeDef *)0x40007000UL)
#define PWR_CR_VOS (1UL << 14)   /* Regulator scale 1: required for 168 MHz */

/* ------------------------------------------------------------------------- */
/* GPIO                                                                      */
/* ------------------------------------------------------------------------- */
typedef struct {
    __IO uint32_t MODER;    /* 0x00 */
    __IO uint32_t OTYPER;   /* 0x04 */
    __IO uint32_t OSPEEDR;  /* 0x08 */
    __IO uint32_t PUPDR;    /* 0x0C */
    __IO uint32_t IDR;      /* 0x10 */
    __IO uint32_t ODR;      /* 0x14 */
    __IO uint32_t BSRR;     /* 0x18 */
    __IO uint32_t LCKR;     /* 0x1C */
    __IO uint32_t AFR[2];   /* 0x20-0x24 */
} GPIO_TypeDef;

#define GPIOA ((GPIO_TypeDef *)0x40020000UL)
#define GPIOB ((GPIO_TypeDef *)0x40020400UL)
#define GPIOD ((GPIO_TypeDef *)0x40020C00UL)

#define GPIO_MODE_INPUT  0UL
#define GPIO_MODE_OUTPUT 1UL
#define GPIO_MODE_AF     2UL
#define GPIO_MODE_ANALOG 3UL

#define GPIO_PULL_NONE   0UL
#define GPIO_PULL_UP     1UL
#define GPIO_PULL_DOWN   2UL

/* ------------------------------------------------------------------------- */
/* I2C (v1 peripheral, F1/F2/F4 family)                                      */
/* ------------------------------------------------------------------------- */
typedef struct {
    __IO uint32_t CR1;    /* 0x00 */
    __IO uint32_t CR2;    /* 0x04 */
    __IO uint32_t OAR1;   /* 0x08 */
    __IO uint32_t OAR2;   /* 0x0C */
    __IO uint32_t DR;     /* 0x10 */
    __IO uint32_t SR1;    /* 0x14 */
    __IO uint32_t SR2;    /* 0x18 */
    __IO uint32_t CCR;    /* 0x1C */
    __IO uint32_t TRISE;  /* 0x20 */
    __IO uint32_t FLTR;   /* 0x24 */
} I2C_TypeDef;

#define I2C1 ((I2C_TypeDef *)0x40005400UL)

#define I2C_CR1_PE      (1UL << 0)
#define I2C_CR1_START   (1UL << 8)
#define I2C_CR1_STOP    (1UL << 9)
#define I2C_CR1_ACK     (1UL << 10)
#define I2C_CR1_POS     (1UL << 11)
#define I2C_CR1_SWRST   (1UL << 15)

#define I2C_SR1_SB      (1UL << 0)
#define I2C_SR1_ADDR    (1UL << 1)
#define I2C_SR1_BTF     (1UL << 2)
#define I2C_SR1_RXNE    (1UL << 6)
#define I2C_SR1_TXE     (1UL << 7)
#define I2C_SR1_BERR    (1UL << 8)
#define I2C_SR1_ARLO    (1UL << 9)
#define I2C_SR1_AF      (1UL << 10)

#define I2C_SR2_BUSY    (1UL << 1)

#define I2C_CCR_FS      (1UL << 15)

/* ------------------------------------------------------------------------- */
/* TIM2 - 32-bit general-purpose timer (0x4000_0000), IRQ 28                 */
/* ------------------------------------------------------------------------- */
typedef struct {
    __IO uint32_t CR1;    /* 0x00 */
    __IO uint32_t CR2;    /* 0x04 */
    __IO uint32_t SMCR;   /* 0x08 */
    __IO uint32_t DIER;   /* 0x0C */
    __IO uint32_t SR;     /* 0x10 */
    __IO uint32_t EGR;    /* 0x14 */
    __IO uint32_t CCMR1;  /* 0x18 */
    __IO uint32_t CCMR2;  /* 0x1C */
    __IO uint32_t CCER;   /* 0x20 */
    __IO uint32_t CNT;    /* 0x24 */
    __IO uint32_t PSC;    /* 0x28 */
    __IO uint32_t ARR;    /* 0x2C */
} TIM_TypeDef;

#define TIM2 ((TIM_TypeDef *)0x40000000UL)
#define TIM2_IRQn     28U
#define TIM_CR1_CEN   (1UL << 0)
#define TIM_DIER_UIE  (1UL << 0)
#define TIM_SR_UIF    (1UL << 0)
#define TIM_EGR_UG    (1UL << 0)

/* ------------------------------------------------------------------------- */
/* Cortex-M4 core peripherals                                                */
/* ------------------------------------------------------------------------- */

/* System Control Block (0xE000_ED00) */
typedef struct {
    __IO uint32_t CPUID;   /* 0x00 */
    __IO uint32_t ICSR;    /* 0x04 */
    __IO uint32_t VTOR;    /* 0x08 */
    __IO uint32_t AIRCR;   /* 0x0C */
    __IO uint32_t SCR;     /* 0x10 */
    __IO uint32_t CCR;     /* 0x14 */
    __IO uint8_t  SHP[12]; /* 0x18-0x23: priority bytes for exceptions 4..15 */
    __IO uint32_t SHCSR;   /* 0x24 */
    __IO uint32_t CFSR;    /* 0x28 */
    __IO uint32_t HFSR;    /* 0x2C */
    __IO uint32_t DFSR;    /* 0x30 */
    __IO uint32_t MMFAR;   /* 0x34 */
    __IO uint32_t BFAR;    /* 0x38 */
    __IO uint32_t AFSR;    /* 0x3C */
} SCB_TypeDef;

#define SCB ((SCB_TypeDef *)0xE000ED00UL)

#define SCB_ICSR_PENDSVSET   (1UL << 28)
#define SCB_AIRCR_SYSRESET   ((0x05FAUL << 16) | (1UL << 2))
#define SCB_CCR_DIV_0_TRP    (1UL << 4)
#define SCB_SHCSR_MEMFAULTENA (1UL << 16)
#define SCB_SHCSR_BUSFAULTENA (1UL << 17)
#define SCB_SHCSR_USGFAULTENA (1UL << 18)

/* SHP[] index = exception number - 4 */
#define SHP_IDX_SVCALL  (11 - 4)
#define SHP_IDX_PENDSV  (14 - 4)
#define SHP_IDX_SYSTICK (15 - 4)

/* NVIC: enable/disable and priority for peripheral IRQs */
#define NVIC_ISER(n) (*(__IO uint32_t *)(0xE000E100UL + 4U * (n)))
#define NVIC_ICER(n) (*(__IO uint32_t *)(0xE000E180UL + 4U * (n)))
#define NVIC_ICPR(n) (*(__IO uint32_t *)(0xE000E280UL + 4U * (n)))
#define NVIC_IPR(irq) (*(__IO uint8_t *)(0xE000E400UL + (irq)))

/* Coprocessor access control: enable CP10/CP11 = the FPU */
#define SCB_CPACR (*(__IO uint32_t *)0xE000ED88UL)

/* SysTick (0xE000_E010) */
typedef struct {
    __IO uint32_t CTRL;
    __IO uint32_t LOAD;
    __IO uint32_t VAL;
    __IO uint32_t CALIB;
} SysTick_TypeDef;

#define SYSTICK ((SysTick_TypeDef *)0xE000E010UL)
#define SYSTICK_CTRL_ENABLE    (1UL << 0)
#define SYSTICK_CTRL_TICKINT   (1UL << 1)
#define SYSTICK_CTRL_CLKSOURCE (1UL << 2)   /* 1 = processor clock */

/* Debug: DHCSR tells us if a debugger is attached; DEMCR gates the DWT */
#define COREDEBUG_DHCSR (*(__IO uint32_t *)0xE000EDF0UL)
#define COREDEBUG_DEMCR (*(__IO uint32_t *)0xE000EDFCUL)
#define DHCSR_C_DEBUGEN (1UL << 0)
#define DEMCR_TRCENA    (1UL << 24)

/* DWT - Data Watchpoint and Trace unit (0xE000_1000) */
#define DWT_CTRL   (*(__IO uint32_t *)0xE0001000UL)
#define DWT_CYCCNT (*(__IO uint32_t *)0xE0001004UL)
#define DWT_CTRL_CYCCNTENA (1UL << 0)
#define DWT_CTRL_NOCYCCNT  (1UL << 25)

/* ------------------------------------------------------------------------- */
/* Small intrinsics (what CMSIS would normally give you)                     */
/* ------------------------------------------------------------------------- */
static inline void __dsb(void) { __asm volatile ("dsb 0xF" ::: "memory"); }
static inline void __isb(void) { __asm volatile ("isb 0xF" ::: "memory"); }
static inline void __wfi(void) { __asm volatile ("wfi"); }
static inline void __nop(void) { __asm volatile ("nop"); }

static inline uint32_t __get_PRIMASK(void)
{
    uint32_t r;
    __asm volatile ("mrs %0, primask" : "=r"(r));
    return r;
}

static inline void __set_PRIMASK(uint32_t v)
{
    __asm volatile ("msr primask, %0" :: "r"(v) : "memory");
}

static inline void __disable_irq(void) { __asm volatile ("cpsid i" ::: "memory"); }
static inline void __enable_irq(void)  { __asm volatile ("cpsie i" ::: "memory"); }

static inline uint32_t __get_IPSR(void)
{
    uint32_t r;
    __asm volatile ("mrs %0, ipsr" : "=r"(r));
    return r;
}

#endif /* STM32F407_H */
