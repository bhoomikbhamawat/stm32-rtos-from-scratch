/*
 * i2c.h - Polling I2C1 master (PB6 SCL / PB7 SDA, 400 kHz).
 */
#ifndef I2C_H
#define I2C_H

#include <stdint.h>

#define I2C_OK           0
#define I2C_ERR_NACK    (-1)   /* no device answered at that address */
#define I2C_ERR_TIMEOUT (-2)
#define I2C_ERR_BUS     (-3)   /* bus error / arbitration lost */
#define I2C_ERR_PARAM   (-4)

typedef struct {
    uint32_t transfers;
    uint32_t errors;
    uint32_t nacks;
    uint32_t timeouts;
    uint32_t bus_recoveries;
} i2c_stats_t;

extern volatile i2c_stats_t g_i2c_stats;

int i2c1_init(void);
int i2c1_write_reg(uint8_t addr7, uint8_t reg, uint8_t value);
int i2c1_read_regs(uint8_t addr7, uint8_t reg, uint8_t *buf, uint32_t len);

#endif
