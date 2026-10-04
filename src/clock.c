/*
 * clock.c - HSE (8 MHz crystal on the Discovery board) -> PLL -> 168 MHz.
 *
 *   f_VCO_in  = HSE / M       = 8 MHz / 8     = 1 MHz   (must be 1..2 MHz)
 *   f_VCO     = f_VCO_in * N  = 1 MHz * 336   = 336 MHz (must be 100..432 MHz)
 *   SYSCLK    = f_VCO / P     = 336 / 2       = 168 MHz
 *   USB/SDIO  = f_VCO / Q     = 336 / 7       = 48 MHz
 *   AHB  = SYSCLK / 1 = 168 MHz
 *   APB1 = AHB / 4    =  42 MHz (max 42)  -> I2C1
 *   APB2 = AHB / 2    =  84 MHz (max 84)
 *
 * Interview notes:
 *  - Flash wait states MUST be raised before switching to the faster clock
 *    (RM0090 table 10: 5 WS for 150-168 MHz at 2.7-3.6 V). Otherwise the
 *    CPU fetches faster than flash can deliver -> garbage instructions.
 *  - The ART accelerator (prefetch + I-cache + D-cache) is what makes 5 WS
 *    flash run close to 0 WS. It also changes cycle counts you measure with
 *    the DWT, so note it when you quote numbers.
 */
#include "clock.h"
#include "stm32f407.h"

uint32_t SystemCoreClock = 16000000UL;
uint32_t g_pclk1_hz      = 16000000UL;
volatile uint32_t g_clock_hse_failed = 0;

#define HSE_STARTUP_LOOPS 500000UL

void clock_init_168mhz(void)
{
    /* Voltage regulator scale 1 (needed above 144 MHz) */
    RCC->APB1ENR |= RCC_APB1ENR_PWREN;
    (void)RCC->APB1ENR;                 /* read-back: give the clock time to start */
    PWR->CR |= PWR_CR_VOS;

    /* Start the external 8 MHz crystal */
    RCC->CR |= RCC_CR_HSEON;
    uint32_t n = 0;
    while (!(RCC->CR & RCC_CR_HSERDY)) {
        if (++n > HSE_STARTUP_LOOPS) {
            /* No crystal (or running in an emulator): stay on HSI 16 MHz */
            RCC->CR &= ~RCC_CR_HSEON;
            g_clock_hse_failed = 1;
            return;
        }
    }

    /* Flash: 5 wait states + ART accelerator, set BEFORE raising the clock */
    FLASH->ACR = FLASH_ACR_LATENCY_5WS | FLASH_ACR_PRFTEN |
                 FLASH_ACR_ICEN | FLASH_ACR_DCEN;
    while ((FLASH->ACR & FLASH_ACR_LATENCY_Msk) != FLASH_ACR_LATENCY_5WS) { }

    /* Bus prescalers: AHB /1, APB1 /4, APB2 /2 */
    uint32_t cfgr = RCC->CFGR;
    cfgr &= ~(RCC_CFGR_HPRE_Msk | RCC_CFGR_PPRE1_Msk | RCC_CFGR_PPRE2_Msk);
    cfgr |= RCC_CFGR_PPRE1_DIV4 | RCC_CFGR_PPRE2_DIV2;
    RCC->CFGR = cfgr;

    /* PLL: M=8, N=336, P=2 (encoded 0), Q=7, source = HSE */
    RCC->PLLCFGR = (8UL   << RCC_PLLCFGR_PLLM_Pos) |
                   (336UL << RCC_PLLCFGR_PLLN_Pos) |
                   (0UL   << RCC_PLLCFGR_PLLP_Pos) |
                   RCC_PLLCFGR_PLLSRC_HSE |
                   (7UL   << RCC_PLLCFGR_PLLQ_Pos);
    RCC->CR |= RCC_CR_PLLON;
    while (!(RCC->CR & RCC_CR_PLLRDY)) { }

    /* Switch SYSCLK to the PLL and wait until the hardware confirms */
    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW_Msk) | RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS_Msk) != RCC_CFGR_SWS_PLL) { }

    SystemCoreClock = 168000000UL;
    g_pclk1_hz      = 42000000UL;
}
