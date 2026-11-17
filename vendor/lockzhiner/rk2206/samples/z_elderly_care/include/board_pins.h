/*
 * 通晓 TX-SMART-R · 智慧养老项目 · 板级引脚定义
 *
 * 所有引脚以通晓官方仓库 isoh_edu/txsmartropenharmony 源码为准。
 * 不要用小凌派的引脚定义！
 */
#ifndef BOARD_PINS_H
#define BOARD_PINS_H

/* ---- 板载危险外设（开机必须先拉低） ---- */
#define PIN_MOTOR       GPIO0_PC6   /* 电机  PWM6_M0 */
#define PIN_BEEP        GPIO0_PC5   /* 蜂鸣器 PWM5_M0 */
#define PIN_RGB_R       GPIO0_PB5   /* RGB_R PWM1_M1 */
#define PIN_RGB_G       GPIO1_PD0   /* RGB_G PWM7_M1 */
#define PIN_RGB_B       GPIO0_PB4   /* RGB_B PWM0_M1 */

/* ---- LCD ST7789V (SPI0_M1) ---- */
#define PIN_LCD_CS      GPIO0_PC0
#define PIN_LCD_CLK     GPIO0_PC1
#define PIN_LCD_MOSI    GPIO0_PC2
#define PIN_LCD_RES     GPIO0_PC3
#define PIN_LCD_DC      GPIO0_PA4   /* ⚠ 不是 PC6，PC6 是电机 */

/* ---- 传感器 ---- */
#define PIN_PIR         GPIO0_PA3   /* 人体感应 PIR */
#define MQ2_ADC_CHANNEL 4           /* 可燃气体 MQ2 */
#define SHT30_I2C_PORT  EI2C0_M2   /* 温湿度 I2C0 */
#define SHT30_ADDR      0x44

/* ---- 按键 ---- */
#define PIN_KEY         GPIO0_PC7   /* 板载按键 K1 */

/* ---- 门磁（外接 GPIO，按实际接线改） ---- */
#define PIN_DOOR        GPIO0_PB3   /* 门磁 DO→B3 */

/* ---- 火焰传感器（外接，DO→PC4） ---- */
#define PIN_FLAME       GPIO0_PC4   /* 火焰 DO→PC4，数字量，高=有火 */

/* ---- 按键 ADC ---- */
#define KEY_ADC_CHANNEL 7           /* K3~K6 分压在 PC7 / ADC ch7 */

/* ---- 安全空闲脚 ---- */
#define PIN_FREE1       GPIO0_PB6   /* PWM2_M1，已被星闪占用 */

#endif /* BOARD_PINS_H */
