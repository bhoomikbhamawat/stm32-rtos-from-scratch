/*
 * board.h - STM32F4-Discovery (STM32F407VG) board pins.
 *
 *   LEDs:  PD12 green  (heartbeat: scheduler alive)
 *          PD13 orange (MPU-6050 not detected)
 *          PD14 red    (fault recorded / fault on last boot)
 *          PD15 blue   (toggles every 50 IMU samples -> 1 Hz blink at 100 Hz)
 *   I2C1:  PB6 = SCL, PB7 = SDA  (AF4, open-drain)
 *   MPU-6050 (GY-521 module): VCC->3V, GND->GND, SCL->PB6, SDA->PB7, AD0->GND
 */
#ifndef BOARD_H
#define BOARD_H

#include <stdint.h>

#define LED_GREEN  12U
#define LED_ORANGE 13U
#define LED_RED    14U
#define LED_BLUE   15U

void board_leds_init(void);
void led_on(uint32_t pin);
void led_off(uint32_t pin);
void led_toggle(uint32_t pin);

#endif
