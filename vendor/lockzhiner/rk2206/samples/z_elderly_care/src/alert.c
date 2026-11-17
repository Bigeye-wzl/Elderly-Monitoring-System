/*
 * 声光报警模块 — 蜂鸣器 + RGB LED
 *
 * 硬件: 通晓 TX-SMART-R 板载
 *   蜂鸣器: GPIO0_PC5 -> EPWMDEV_PWM5_M0 (无源, PWM 3KHz 50%)
 *   RGB LED: R=GPIO0_PB5  G=GPIO1_PD0  B=GPIO0_PB4
 *
 * 声光联动:
 *   无报警   → 绿灯常亮 (系统正常)
 *   SOS      → 红灯快闪 200ms + 蜂鸣器急促滴滴
 *   FALL     → 红灯中闪 500ms + 蜂鸣器较慢滴滴
 *   两者同时 → 红灯按 SOS 节奏 (优先级高)
 *
 * 报警节拍: 独立低优先级任务翻转 PWM/GPIO，报警源可随时增删
 */
#include "iot_pwm.h"
#include "iot_gpio.h"
#include "iot_errno.h"
#include "los_task.h"
#include "alert.h"
#include "board_pins.h"
#include <stdio.h>

#define BEEP_PWM_PORT   EPWMDEV_PWM5_M0     /* GPIO0_PC5, 板载蜂鸣器 */
#define BEEP_FREQ_HZ    3000                /* 3KHz 方波，蜂鸣器谐振点附近 */
#define BEEP_DUTY       50                  /* 占空比 % */

/* 报警节拍 (ms): 越急越快 */
#define SOS_ON_MS       200
#define SOS_OFF_MS      200
#define FALL_ON_MS      500
#define FALL_OFF_MS     500
#define FIRE_ON_MS      100
#define FIRE_OFF_MS     100

#define ALERT_TASK_STACK 4096
#define ALERT_TASK_PRIO  25     /* 略低于 care_thread(24) */

static volatile uint8_t s_src[ALERT_SRC_NUM];
static uint8_t s_pwm_on = 0;

/* ============================================================
 * RGB LED 控制
 * ============================================================ */
static void led_init(void)
{
    IoTGpioInit(PIN_RGB_R);
    IoTGpioSetDir(PIN_RGB_R, IOT_GPIO_DIR_OUT);
    IoTGpioInit(PIN_RGB_G);
    IoTGpioSetDir(PIN_RGB_G, IOT_GPIO_DIR_OUT);
    IoTGpioInit(PIN_RGB_B);
    IoTGpioSetDir(PIN_RGB_B, IOT_GPIO_DIR_OUT);
}

static void led_set(int r, int g, int b)
{
    IoTGpioSetOutputVal(PIN_RGB_R, r ? IOT_GPIO_VALUE1 : IOT_GPIO_VALUE0);
    IoTGpioSetOutputVal(PIN_RGB_G, g ? IOT_GPIO_VALUE1 : IOT_GPIO_VALUE0);
    IoTGpioSetOutputVal(PIN_RGB_B, b ? IOT_GPIO_VALUE1 : IOT_GPIO_VALUE0);
}

static void led_green(void)  { led_set(0, 1, 0); }  /* 正常 */
static void led_red(void)    { led_set(1, 0, 0); }  /* 报警 */
static void led_off(void)    { led_set(0, 0, 0); }

/* ============================================================
 * 蜂鸣器控制
 * ============================================================ */
static void beep_on(void)
{
    if (s_pwm_on) return;
    if (IoTPwmStart(BEEP_PWM_PORT, BEEP_DUTY, BEEP_FREQ_HZ) != IOT_SUCCESS) {
        printf("[ALERT] IoTPwmStart fail\n");
        return;
    }
    s_pwm_on = 1;
}

static void beep_off(void)
{
    if (!s_pwm_on) return;
    IoTPwmStop(BEEP_PWM_PORT);
    s_pwm_on = 0;
}

/* ============================================================
 * 对外接口
 * ============================================================ */
void alert_set(alert_src_t src, int active)
{
    if (src >= ALERT_SRC_NUM) return;
    uint8_t v = (uint8_t)(active ? 1 : 0);
    if (s_src[src] != v) {
        s_src[src] = v;
        printf("[ALERT] src=%d -> %d\n", (int)src, (int)v);
    }
}

int alert_is_active(void)
{
    int i;
    for (i = 0; i < ALERT_SRC_NUM; i++) {
        if (s_src[i]) return 1;
    }
    return 0;
}

void alert_stop_all(void)
{
    int i;
    for (i = 0; i < ALERT_SRC_NUM; i++) {
        s_src[i] = 0;
    }
    beep_off();
    led_green();    /* 恢复绿灯 */
}

/* ============================================================
 * 声光报警节拍任务
 *   蜂鸣器和红灯同步闪烁, 无报警时绿灯常亮
 * ============================================================ */
static void alert_task(void)
{
    led_green();    /* 上电先亮绿灯 */

    while (1) {
        int on_ms, off_ms;

        if (s_src[ALERT_SRC_FIRE]) {
            on_ms = FIRE_ON_MS;
            off_ms = FIRE_OFF_MS;
        } else if (s_src[ALERT_SRC_SOS]) {
            on_ms = SOS_ON_MS;
            off_ms = SOS_OFF_MS;
        } else if (s_src[ALERT_SRC_FALL]) {
            on_ms = FALL_ON_MS;
            off_ms = FALL_OFF_MS;
        } else {
            /* 无报警: 关蜂鸣器, 绿灯常亮 */
            beep_off();
            led_green();
            LOS_Msleep(100);
            continue;
        }

        /* 响: 蜂鸣器 + 红灯同亮 */
        beep_on();
        led_red();
        LOS_Msleep((uint32_t)on_ms);

        /* 停: 蜂鸣器 + 红灯同灭 */
        beep_off();
        led_off();
        LOS_Msleep((uint32_t)off_ms);
    }
}

int alert_init(void)
{
    unsigned int ret;
    unsigned int tid;
    TSK_INIT_PARAM_S task = {0};

    /* 蜂鸣器 PWM */
    ret = IoTPwmInit(BEEP_PWM_PORT);
    if (ret != IOT_SUCCESS) {
        printf("[ALERT] IoTPwmInit fail %u\n", ret);
        return -1;
    }
    beep_off();

    /* RGB LED */
    led_init();
    led_green();

    printf("[ALERT] buzzer(PWM5_M0 @%dHz) + RGB LED ready\n", BEEP_FREQ_HZ);

    task.pfnTaskEntry = (TSK_ENTRY_FUNC)alert_task;
    task.uwStackSize = ALERT_TASK_STACK;
    task.pcName = "alert_task";
    task.usTaskPrio = ALERT_TASK_PRIO;
    if (LOS_TaskCreate(&tid, &task) != LOS_OK) {
        printf("[ALERT] task create fail\n");
        return -1;
    }
    return 0;
}
