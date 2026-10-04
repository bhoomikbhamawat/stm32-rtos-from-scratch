/*
 * board.c - LED GPIO setup on port D.
 */
#include "board.h"
#include "stm32f407.h"

void board_leds_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIODEN;
    (void)RCC->AHB1ENR;

    for (uint32_t pin = LED_GREEN; pin <= LED_BLUE; pin++) {
        GPIOD->MODER   = (GPIOD->MODER & ~(3UL << (pin * 2))) | (GPIO_MODE_OUTPUT << (pin * 2));
        GPIOD->OTYPER &= ~(1UL << pin);              /* push-pull */
        GPIOD->PUPDR  &= ~(3UL << (pin * 2));        /* no pull   */
        GPIOD->BSRR    = (1UL << (pin + 16));        /* off       */
    }
}

/* BSRR writes are atomic: no read-modify-write, so they are safe from any
 * task or ISR without a critical section. ODR ^= is NOT atomic. */
void led_on(uint32_t pin)  { GPIOD->BSRR = (1UL << pin); }
void led_off(uint32_t pin) { GPIOD->BSRR = (1UL << (pin + 16)); }

void led_toggle(uint32_t pin)
{
    if (GPIOD->ODR & (1UL << pin)) {
        led_off(pin);
    } else {
        led_on(pin);
    }
}
