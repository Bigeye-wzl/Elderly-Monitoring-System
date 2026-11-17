/*
 * 通晓 TX-SMART-R 板级引脚定义（适配层）
 *
 * 本工程 SDK 是 lockzhiner 小凌派，但实机是通晓 SMART-R。
 * 两者外设引脚不同，所有例程请统一引用本文件，不要各写各的。
 *
 * 引脚依据（通晓官方仓库 isoh_edu/txsmartropenharmony）：
 *   b8_electrical_machinery  电机  EPWMDEV_PWM6_M0 -> GPIO0_PC6
 *   b7_beep                  蜂鸣  EPWMDEV_PWM5_M0 -> GPIO0_PC5
 *   b9_rgb_led               RGB   PWM1_M1/PWM7_M1/PWM0_M1 -> PB5/PD0/PB4
 *   c2_lcd                   LCD   DC=GPIO0_PA4（源码，不是 README）
 */
#ifndef BOARD_SMART_R_H
#define BOARD_SMART_R_H

/* ---- 板载外设（危险，勿当普通 GPIO 用） ---- */
#define SMART_R_PIN_MOTOR   GPIO0_PC6   /* 电机  PWM6_M0 */
#define SMART_R_PIN_BEEP    GPIO0_PC5   /* 蜂鸣器 PWM5_M0 */
#define SMART_R_PIN_RGB_R   GPIO0_PB5   /* RGB_R PWM1_M1 */
#define SMART_R_PIN_RGB_G   GPIO1_PD0   /* RGB_G PWM7_M1 */
#define SMART_R_PIN_RGB_B   GPIO0_PB4   /* RGB_B PWM0_M1 */

/* ---- 2.4寸 LCD (ST7789V, SPI0_M1) ---- */
#define SMART_R_LCD_SPI_BUS ESPI0_M1
#define SMART_R_PIN_LCD_CS  GPIO0_PC0
#define SMART_R_PIN_LCD_CLK GPIO0_PC1
#define SMART_R_PIN_LCD_MOSI GPIO0_PC2
#define SMART_R_PIN_LCD_RES GPIO0_PC3
#define SMART_R_PIN_LCD_DC  GPIO0_PA4   /* ⚠ 不是 PC6，PC6 是电机 */

/* ---- 按键 / ADC ---- */
#define SMART_R_PIN_KEY     GPIO0_PC7   /* 板载按键 K1，也是 ADC 通道 7 */
#define SMART_R_ADC_KEY_CH  7

/* ---- 安全空闲脚（PWM/ADC 实验用这些） ---- */
#define SMART_R_PIN_FREE1   GPIO0_PB6   /* PWM2_M1 */
#define SMART_R_PIN_FREE2   GPIO0_PC4   /* PWM4_M0 */
#define SMART_R_PIN_FREE3   GPIO0_PB7   /* PWM3_M1 */

#endif /* BOARD_SMART_R_H */
