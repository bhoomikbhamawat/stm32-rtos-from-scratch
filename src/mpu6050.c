/*
 * mpu6050.c - MPU-6050 configuration for a 100 Hz output data rate.
 *
 *   CONFIG.DLPF_CFG = 3      -> 44 Hz accel / 42 Hz gyro low-pass,
 *                               gyro output rate = 1 kHz
 *   SMPLRT_DIV      = 9      -> sample rate = 1 kHz / (1 + 9) = 100 Hz
 *   GYRO  +/-250 dps, ACCEL +/-2 g
 *   INT_ENABLE.DATA_RDY = 1  -> INT_STATUS bit0 set on every new sample
 *
 * The task polls at 200 Hz (2x the data rate) and only consumes a sample when
 * DATA_RDY is set, so every sensor sample is read exactly once, and the
 * measured rate (g_imu.rate_hz) reflects the sensor's real ODR.
 */
#include "mpu6050.h"
#include "i2c.h"
#include <stddef.h>

int mpu6050_init(void (*delay_ms)(uint32_t), uint8_t *who_am_i)
{
    uint8_t who = 0;
    int rc = i2c1_read_regs(MPU6050_ADDR, MPU_REG_WHO_AM_I, &who, 1);
    if (who_am_i != NULL) {
        *who_am_i = who;
    }
    if (rc != I2C_OK) {
        return rc;                      /* typically NACK: not connected */
    }
    if (who != 0x68U) {
        return -10;                     /* something else at 0x68 (e.g. MPU-6500 = 0x70) */
    }

    if ((rc = i2c1_write_reg(MPU6050_ADDR, MPU_REG_PWR_MGMT_1, 0x80U)) != I2C_OK) return rc; /* reset */
    delay_ms(100);
    if ((rc = i2c1_write_reg(MPU6050_ADDR, MPU_REG_PWR_MGMT_1, 0x01U)) != I2C_OK) return rc; /* wake, PLL gyro-X clock */
    delay_ms(10);
    if ((rc = i2c1_write_reg(MPU6050_ADDR, MPU_REG_CONFIG, 0x03U)) != I2C_OK) return rc;
    if ((rc = i2c1_write_reg(MPU6050_ADDR, MPU_REG_SMPLRT_DIV, 9U)) != I2C_OK) return rc;
    if ((rc = i2c1_write_reg(MPU6050_ADDR, MPU_REG_GYRO_CONFIG, 0x00U)) != I2C_OK) return rc;
    if ((rc = i2c1_write_reg(MPU6050_ADDR, MPU_REG_ACCEL_CONFIG, 0x00U)) != I2C_OK) return rc;
    if ((rc = i2c1_write_reg(MPU6050_ADDR, MPU_REG_INT_PIN_CFG, 0x00U)) != I2C_OK) return rc;
    if ((rc = i2c1_write_reg(MPU6050_ADDR, MPU_REG_INT_ENABLE, MPU_INT_DATA_RDY)) != I2C_OK) return rc;
    return 0;
}

int mpu6050_read_if_ready(mpu6050_raw_t *out)
{
    uint8_t b[15];   /* INT_STATUS (0x3A) + ACCEL..GYRO (0x3B-0x48) */
    int rc = i2c1_read_regs(MPU6050_ADDR, MPU_REG_INT_STATUS, b, sizeof(b));
    if (rc != I2C_OK) {
        return rc;
    }
    if (!(b[0] & MPU_INT_DATA_RDY)) {
        return 0;
    }
    /* Registers are big-endian (high byte first) */
    out->ax   = (int16_t)((b[1]  << 8) | b[2]);
    out->ay   = (int16_t)((b[3]  << 8) | b[4]);
    out->az   = (int16_t)((b[5]  << 8) | b[6]);
    out->temp = (int16_t)((b[7]  << 8) | b[8]);
    out->gx   = (int16_t)((b[9]  << 8) | b[10]);
    out->gy   = (int16_t)((b[11] << 8) | b[12]);
    out->gz   = (int16_t)((b[13] << 8) | b[14]);
    return 1;
}
