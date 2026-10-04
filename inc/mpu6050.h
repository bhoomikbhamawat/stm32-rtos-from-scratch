/*
 * mpu6050.h - InvenSense MPU-6050 6-axis IMU over I2C.
 */
#ifndef MPU6050_H
#define MPU6050_H

#include <stdint.h>

#define MPU6050_ADDR        0x68U   /* AD0 = GND (0x69 if AD0 = VCC) */

#define MPU_REG_SMPLRT_DIV  0x19U
#define MPU_REG_CONFIG      0x1AU
#define MPU_REG_GYRO_CONFIG 0x1BU
#define MPU_REG_ACCEL_CONFIG 0x1CU
#define MPU_REG_INT_PIN_CFG 0x37U
#define MPU_REG_INT_ENABLE  0x38U
#define MPU_REG_INT_STATUS  0x3AU
#define MPU_REG_ACCEL_XOUT_H 0x3BU
#define MPU_REG_PWR_MGMT_1  0x6BU
#define MPU_REG_WHO_AM_I    0x75U

#define MPU_INT_DATA_RDY    0x01U

/* Scale factors for the ranges configured in mpu6050_init() */
#define MPU_ACCEL_LSB_PER_G   16384.0f   /* +/-2 g     */
#define MPU_GYRO_LSB_PER_DPS  131.0f     /* +/-250 dps */

typedef struct {
    int16_t ax, ay, az;
    int16_t temp;
    int16_t gx, gy, gz;
} mpu6050_raw_t;

/* delay_ms is injected so the driver doesn't depend on the RTOS */
int mpu6050_init(void (*delay_ms)(uint32_t), uint8_t *who_am_i);

/* Burst-reads INT_STATUS + 14 data bytes in ONE transaction.
 * Returns 1 if a new sample was ready, 0 if not, <0 on I2C error. */
int mpu6050_read_if_ready(mpu6050_raw_t *out);

#endif
