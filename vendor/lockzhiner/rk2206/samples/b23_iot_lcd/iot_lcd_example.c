/*
 * 通晓 SMART-R · 人体感应 PIR + LCD 显示
 *
 * 硬件：
 *   PIR 传感器 = GPIO0_PA3（板载，官方 b2_gpio_interrupt）
 *   LCD ST7789V = SPI0_M1（板载，已调通）
 *
 * 功能：
 *   读 PA3 电平，有人=高 无人=低，实时显示在 LCD 上
 *   背景变色：有人=绿色  无人=灰色
 */

#include "los_task.h"
#include "ohos_init.h"
#include "iot_gpio.h"

#include "lcd.h"

/* ============================================================
 * 通晓 SMART-R 板载外设，必须先关掉，否则电机狂转
 * ============================================================ */
#define PIN_MOTOR   GPIO0_PC6   /* 电机 PWM6_M0 */
#define PIN_RGB_R   GPIO0_PB5   /* RGB_R PWM1_M1 */
#define PIN_RGB_G   GPIO1_PD0   /* RGB_G PWM7_M1 */
#define PIN_RGB_B   GPIO0_PB4   /* RGB_B PWM0_M1 */

/* ============================================================
 * PIR 人体感应
 * ============================================================ */
#define PIN_PIR     GPIO0_PA3   /* 人体感应传感器 */

static void smart_r_all_off(void)
{
    unsigned int pins[4] = {PIN_MOTOR, PIN_RGB_R, PIN_RGB_G, PIN_RGB_B};
    unsigned int i;

    for (i = 0; i < 4; i++) {
        IoTGpioInit(pins[i]);
        IoTGpioSetDir(pins[i], IOT_GPIO_DIR_OUT);
        IoTGpioSetOutputVal(pins[i], IOT_GPIO_VALUE0);
    }
    printf("[PIR] motor/rgb off\n");
}

/* 初始化 PIR 引脚 */
static void pir_init(void)
{
    IoTGpioInit(PIN_PIR);
    IoTGpioSetDir(PIN_PIR, IOT_GPIO_DIR_IN);
    printf("[PIR] PA3 input init done\n");
}

/* 读 PIR 电平：1=有人  0=无人 */
static int pir_read(void)
{
    IotGpioValue val = IOT_GPIO_VALUE0;

    IoTGpioGetInputVal(PIN_PIR, &val);
    return (val == IOT_GPIO_VALUE1) ? 1 : 0;
}

/***************************************************************
 * 函数名称: lcd_thread
 * 说    明: PIR + LCD 主循环
 ***************************************************************/
void lcd_thread(void)
{
    int pir_val;
    int last_pir = -1;          /* 上一次值，-1 强制首次刷新 */
    int stable_cnt = 0;         /* 消抖计数 */
    int confirmed = 0;          /* 确认后的状态 */
    uint16_t bg;

    printf("[PIR] lcd_thread start\n");
    smart_r_all_off();
    lcd_init(0);
    printf("[PIR] lcd_init done\n");
    pir_init();
    printf("[PIR] pir_init done\n");

    /* 首屏画标题和边框 */
    lcd_fill(0, 0, LCD_W, LCD_H, LCD_BLACK);
    lcd_show_string(30, 10, (const uint8_t *)"PIR Sensor", LCD_WHITE, LCD_BLACK, 32, 0);
    lcd_draw_line(10, 50, 229, 50, LCD_GRAYBLUE);

    while (1) {
        /* 读传感器 */
        pir_val = pir_read();

        /* 简单消抖：连续 2 次相同才切换状态 */
        if (pir_val == last_pir) {
            stable_cnt++;
            if (stable_cnt >= 2) {
                confirmed = pir_val;
            }
        } else {
            stable_cnt = 0;
            last_pir = pir_val;
        }

        /* 选背景色 */
        bg = confirmed ? LCD_GREEN : LCD_GRAY;

        /* ---- 状态大字 ---- */
        lcd_fill(10, 55, 229, 150, bg);
        if (confirmed) {
            lcd_show_string(25, 70, (const uint8_t *)"Somebody!", LCD_BLACK, bg, 32, 0);
        } else {
            lcd_show_string(40, 70, (const uint8_t *)"No one", LCD_WHITE, bg, 32, 0);
        }

        /* ---- GPIO 原始值 ---- */
        lcd_fill(10, 155, 229, 200, LCD_BLACK);
        lcd_show_string(15, 160, (const uint8_t *)"GPIO PA3 = ", LCD_CYAN, LCD_BLACK, 24, 0);
        lcd_show_int_num(160, 160, pir_val, 1, LCD_YELLOW, LCD_BLACK, 24);

        /* ---- 提示 ---- */
        lcd_fill(10, 205, 229, 260, LCD_BLACK);
        if (confirmed) {
            lcd_show_string(15, 220, (const uint8_t *)"Status: DETECTED", LCD_YELLOW, LCD_BLACK, 16, 0);
        } else {
            lcd_show_string(15, 220, (const uint8_t *)"Status: CLEAR", LCD_CYAN, LCD_BLACK, 16, 0);
        }

        /* ---- 底部 ---- */
        lcd_show_string(20, 290, (const uint8_t *)"TX-SMART-R  2026", LCD_GRAYBLUE, LCD_BLACK, 16, 0);

        /* 串口同步输出，方便验证 */
        printf("[PIR] gpio=%d  confirmed=%d  %s\n",
               pir_val, confirmed, confirmed ? "SOMEBODY" : "NO_ONE");

        LOS_Msleep(500);
    }
}

/***************************************************************
 * 函数名称: iot_lcd_example
 * 说    明: 开机自启动
 ***************************************************************/
void iot_lcd_example()
{
    unsigned int thread_id;
    TSK_INIT_PARAM_S task = {0};
    unsigned int ret      = LOS_OK;

    task.pfnTaskEntry = (TSK_ENTRY_FUNC)lcd_thread;
    task.uwStackSize  = 20480;
    task.pcName       = "pir_lcd_thread";
    task.usTaskPrio   = 24;
    ret               = LOS_TaskCreate(&thread_id, &task);
    if (ret != LOS_OK) {
        printf("Failed to create pir_lcd_thread ret:0x%x\n", ret);
        return;
    }
}

APP_FEATURE_INIT(iot_lcd_example);
