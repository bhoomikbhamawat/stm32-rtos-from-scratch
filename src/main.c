/*
 * main.c - Application: RTOS + MPU-6050 100 Hz sensor pipeline + benchmarks.
 *
 * Tasks (higher number = higher priority):
 *   prio 4  bench_a / bench_b  context-switch benchmark, runs once at boot, then exits
 *   prio 3  imu                polls MPU-6050 at 200 Hz, consumes each new 100 Hz sample,
 *                              publishes it under a mutex and signals a semaphore
 *   prio 2  proc               waits on the semaphore, runs a complementary filter
 *                              (roll/pitch) in floating point
 *   prio 1  stats              1 Hz heartbeat LED, measures the real IMU sample rate
 *   prio 1  fault_test         only with `make FAULT_TEST=n`: crashes on purpose
 *
 * `make SIM_IMU=1` replaces the I2C reads with a synthetic 100 Hz sample
 * generator, so the whole pipeline (mutex, semaphore, filter task) runs on a
 * bare Discovery board before the MPU-6050 is soldered.
 *   prio 0  idle               WFI (created by os_start)
 *
 * Everything worth inspecting is a global you can print from GDB:
 *   g_cs, g_imu, g_attitude, g_i2c_stats, g_fault, g_last_boot_fault
 */
#include <math.h>
#include "stm32f407.h"
#include "board.h"
#include "clock.h"
#include "dwt.h"
#include "os.h"
#include "i2c.h"
#include "mpu6050.h"
#include "fault.h"
#include "cs_bench.h"

#ifndef FAULT_TEST
#define FAULT_TEST 0
#endif
#ifndef SIM_IMU
#define SIM_IMU 0      /* 1: synthetic 100 Hz data, no I2C (MPU-6050 not wired yet) */
#endif

#define IMU_START_DELAY_MS  300U   /* after the benchmark has finished */
#define IMU_POLL_PERIOD_MS  5U     /* 2x the sensor's 100 Hz output data rate */
#define MS_TO_TICKS(ms)     (((ms) * OS_TICK_HZ) / 1000U)

/* ---------------- shared data ---------------- */
typedef struct {
    mpu6050_raw_t raw;
    uint32_t      seq;
    uint32_t      t_cycles;   /* DWT timestamp when the sample was read */
} imu_sample_t;

static imu_sample_t s_latest;          /* protected by s_sample_mutex   */
static os_mutex_t   s_sample_mutex;
static os_sem_t     s_sample_sem;      /* "new sample available" signal */

typedef struct {
    uint32_t present;        /* 1 once WHO_AM_I == 0x68 and config succeeded */
    uint32_t who_am_i;
    int32_t  last_err;       /* last I2C / init error code                   */
    uint32_t init_attempts;
    uint32_t samples;        /* new samples consumed                          */
    uint32_t stale_polls;    /* polls where DATA_RDY was not yet set          */
    uint32_t rate_hz;        /* samples counted in the last 1 s window        */
} imu_status_t;

typedef struct {
    float    ax_g, ay_g, az_g;
    float    gx_dps, gy_dps, gz_dps;
    float    temp_c;
    float    roll_deg, pitch_deg;
    uint32_t processed;
} attitude_t;

volatile imu_status_t g_imu;
volatile attitude_t   g_attitude;
volatile uint32_t     g_heartbeat;
volatile float        g_uptime_s;
volatile uint32_t     g_prev_boot_faulted;

/* ---------------- stacks ---------------- */
static uint32_t s_stack_imu[512]   __attribute__((aligned(8)));
static uint32_t s_stack_proc[512]  __attribute__((aligned(8)));
static uint32_t s_stack_stats[256] __attribute__((aligned(8)));
#if FAULT_TEST
static uint32_t s_stack_fault[256] __attribute__((aligned(8)));
#endif

/* ------------------------------------------------------------------------- */
static void delay_ms(uint32_t ms)
{
    os_delay(MS_TO_TICKS(ms));
}

#if SIM_IMU
/* Fake sensor: a new sample every second poll (= 100 Hz), board tilting slowly */
static int sim_read_if_ready(mpu6050_raw_t *out)
{
    static uint32_t polls;
    if ((++polls & 1U) != 0U) {
        return 0;                          /* "DATA_RDY not set yet" */
    }
    float t = (float)os_ticks() / (float)OS_TICK_HZ;
    float angle = 0.5f * sinf(0.5f * t);   /* +/-0.5 rad roll */
    out->ax   = 0;
    out->ay   = (int16_t)(sinf(angle) * MPU_ACCEL_LSB_PER_G);
    out->az   = (int16_t)(cosf(angle) * MPU_ACCEL_LSB_PER_G);
    out->gx   = (int16_t)(0.25f * cosf(0.5f * t) * 57.29578f * MPU_GYRO_LSB_PER_DPS);
    out->gy   = 0;
    out->gz   = 0;
    out->temp = (int16_t)((25.0f - 36.53f) * 340.0f);
    return 1;
}
#define IMU_INIT(d, w)   (*(w) = 0x68U, 0)
#define IMU_READ(o)      sim_read_if_ready(o)
#else
#define IMU_INIT(d, w)   mpu6050_init((d), (w))
#define IMU_READ(o)      mpu6050_read_if_ready(o)
#endif

static void imu_task(void *arg)
{
    (void)arg;
    os_delay(MS_TO_TICKS(IMU_START_DELAY_MS));
#if !SIM_IMU
    (void)i2c1_init();
#endif

    for (;;) {
        /* Detect / (re)initialise the sensor, retry every second */
        while (!g_imu.present) {
            uint8_t who = 0;
            g_imu.init_attempts++;
            int rc = IMU_INIT(delay_ms, &who);
            g_imu.who_am_i = who;
            g_imu.last_err = rc;
            if (rc == 0) {
                g_imu.present = 1;
                led_off(LED_ORANGE);
            } else {
                led_on(LED_ORANGE);
                delay_ms(1000);
            }
        }

        uint32_t last_wake = os_ticks();
        uint32_t consecutive_errors = 0;

        while (g_imu.present) {
            os_delay_until(&last_wake, MS_TO_TICKS(IMU_POLL_PERIOD_MS));

            mpu6050_raw_t raw;
            int rc = IMU_READ(&raw);
            if (rc < 0) {
                g_imu.last_err = rc;
                if (++consecutive_errors >= 10U) {
                    g_imu.present = 0;          /* sensor lost: go back to detection */
                }
                continue;
            }
            consecutive_errors = 0;
            if (rc == 0) {
                g_imu.stale_polls++;
                continue;
            }

            os_mutex_lock(&s_sample_mutex, OS_WAIT_FOREVER);
            s_latest.raw      = raw;
            s_latest.seq++;
            s_latest.t_cycles = dwt_cycles();
            os_mutex_unlock(&s_sample_mutex);

            (void)os_sem_give(&s_sample_sem);   /* wake the processing task */

            g_imu.samples++;
            if ((g_imu.samples % 50U) == 0U) {
                led_toggle(LED_BLUE);           /* 1 Hz blink at 100 Hz */
            }
        }
    }
}

/* Complementary filter: trust the gyro short-term, the accelerometer long-term */
static void proc_task(void *arg)
{
    (void)arg;
    const float RAD2DEG = 57.29578f;
    const float ALPHA   = 0.98f;
    float    roll = 0.0f, pitch = 0.0f;
    uint32_t last_t = 0;
    int      first  = 1;

    for (;;) {
        if (os_sem_take(&s_sample_sem, MS_TO_TICKS(1000)) != OS_OK) {
            continue;                           /* no sensor data this second */
        }

        imu_sample_t s;
        os_mutex_lock(&s_sample_mutex, OS_WAIT_FOREVER);
        s = s_latest;
        os_mutex_unlock(&s_sample_mutex);

        float ax = (float)s.raw.ax / MPU_ACCEL_LSB_PER_G;
        float ay = (float)s.raw.ay / MPU_ACCEL_LSB_PER_G;
        float az = (float)s.raw.az / MPU_ACCEL_LSB_PER_G;
        float gx = (float)s.raw.gx / MPU_GYRO_LSB_PER_DPS;
        float gy = (float)s.raw.gy / MPU_GYRO_LSB_PER_DPS;
        float gz = (float)s.raw.gz / MPU_GYRO_LSB_PER_DPS;

        float roll_acc  = atan2f(ay, az) * RAD2DEG;
        float pitch_acc = atan2f(-ax, sqrtf(ay * ay + az * az)) * RAD2DEG;

        if (first) {
            roll  = roll_acc;
            pitch = pitch_acc;
            first = 0;
        } else {
            float dt = (float)(s.t_cycles - last_t) / (float)SystemCoreClock;
            roll  = ALPHA * (roll  + gx * dt) + (1.0f - ALPHA) * roll_acc;
            pitch = ALPHA * (pitch + gy * dt) + (1.0f - ALPHA) * pitch_acc;
        }
        last_t = s.t_cycles;

        g_attitude.ax_g = ax;   g_attitude.ay_g = ay;   g_attitude.az_g = az;
        g_attitude.gx_dps = gx; g_attitude.gy_dps = gy; g_attitude.gz_dps = gz;
        g_attitude.temp_c    = (float)s.raw.temp / 340.0f + 36.53f;   /* datasheet formula */
        g_attitude.roll_deg  = roll;
        g_attitude.pitch_deg = pitch;
        g_attitude.processed++;
    }
}

static void stats_task(void *arg)
{
    (void)arg;
    uint32_t last_wake    = os_ticks();
    uint32_t last_samples = 0;

    for (;;) {
        os_delay_until(&last_wake, MS_TO_TICKS(1000));
        led_toggle(LED_GREEN);
        g_heartbeat++;
        g_uptime_s = (float)os_ticks() / (float)OS_TICK_HZ;   /* FPU use in a 2nd task */

        uint32_t now  = g_imu.samples;
        g_imu.rate_hz = now - last_samples;                  /* expect 100 */
        last_samples  = now;
    }
}

#if FAULT_TEST
static void fault_test_task(void *arg)
{
    (void)arg;
    os_delay(MS_TO_TICKS(3000));
    fault_trigger_test(FAULT_TEST);
    for (;;) { os_delay(1000); }
}
#endif

/* ------------------------------------------------------------------------- */
int main(void)
{
    board_leds_init();
    fault_init();

    g_prev_boot_faulted = (uint32_t)fault_check_previous_boot();
    if (g_prev_boot_faulted) {
        led_on(LED_RED);     /* inspect g_last_boot_fault in GDB */
    }

    (void)dwt_init();

    os_init();
    os_sem_init(&s_sample_sem, 0, 1);
    os_mutex_init(&s_sample_mutex);

    cs_bench_create_tasks();
    (void)os_task_create(imu_task,   NULL, 3, s_stack_imu,   512, "imu");
    (void)os_task_create(proc_task,  NULL, 2, s_stack_proc,  512, "proc");
    (void)os_task_create(stats_task, NULL, 1, s_stack_stats, 256, "stats");
#if FAULT_TEST
    (void)os_task_create(fault_test_task, NULL, 1, s_stack_fault, 256, "fault");
#endif

    os_start();   /* never returns */
}
