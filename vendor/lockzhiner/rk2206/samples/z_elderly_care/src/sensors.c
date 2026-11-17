/*
 * 传感器驱动 · PIR 人体感应 + MQ2 可燃气体 + SHT30 温湿度
 */
#include "iot_gpio.h"
#include "iot_adc.h"
#include "iot_i2c.h"
#include "iot_errno.h"
#include "board_pins.h"
#include "sensors.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>

/* ============================================================
 * PIR 人体感应（GPIO0_PA3）
 * ============================================================ */

void pir_init(void)
{
    IoTGpioInit(PIN_PIR);
    IoTGpioSetDir(PIN_PIR, IOT_GPIO_DIR_IN);
    printf("[PIR] PA3 input init\n");
}

int pir_read(void)
{
    IotGpioValue val = IOT_GPIO_VALUE0;
    IoTGpioGetInputVal(PIN_PIR, &val);
    return (val == IOT_GPIO_VALUE1) ? 1 : 0;
}

/* ============================================================
 * MQ2 可燃气体（ADC 通道 4）
 * ============================================================ */

#define GAS_THRESHOLD   300

void gas_init(void)
{
    unsigned int ret = IoTAdcInit(MQ2_ADC_CHANNEL);
    if (ret != IOT_SUCCESS) {
        printf("[GAS] ADC init fail ret:%u\n", ret);
    } else {
        printf("[GAS] ADC ch%d init ok\n", MQ2_ADC_CHANNEL);
    }
}

int gas_read_raw(void)
{
    unsigned int data = 0;
    unsigned int ret = IoTAdcGetVal(MQ2_ADC_CHANNEL, &data);
    if (ret != IOT_SUCCESS) return -1;
    return (int)data;
}

int gas_is_alarm(int raw)
{
    return (raw > GAS_THRESHOLD) ? 1 : 0;
}

float gas_raw_to_voltage(int raw)
{
    return (float)raw * 3.3f / 1024.0f;
}

/* ============================================================
 * SHT30 温湿度（I2C0, 地址 0x44）
 * 官方 c5_sht30 驱动，CRC 校验
 * ============================================================ */

#define SHT30_I2C_PORT      EI2C0_M2
#define SHT30_I2C_ADDRESS   0x44

static float sht30_calc_rh(uint16_t raw)
{
    return 100.0f * (float)raw / 65535.0f;
}

static float sht30_calc_temp(uint16_t raw)
{
    return 175.0f * (float)raw / 65535.0f - 45.0f;
}

static uint8_t sht30_check_crc(uint8_t *data, uint8_t len, uint8_t checksum)
{
    uint8_t crc = 0xFF;
    uint8_t bit, i;
    const uint16_t POLY = 0x131;

    for (i = 0; i < len; i++) {
        crc ^= data[i];
        for (bit = 8; bit > 0; bit--) {
            if (crc & 0x80) crc = (crc << 1) ^ POLY;
            else            crc = (crc << 1);
        }
    }
    return (crc == checksum) ? 0 : 1;
}

void sht30_init(void)
{
    unsigned int ret;
    uint8_t cmd[2] = {0x22, 0x36}; /* 高重复性，每秒 2 次 */

    ret = IoTI2cInit(SHT30_I2C_PORT, EI2C_FRE_400K);
    if (ret != IOT_SUCCESS) {
        printf("[SHT30] i2c init fail ret:%u\n", ret);
        return;
    }
    IoTI2cWrite(SHT30_I2C_PORT, SHT30_I2C_ADDRESS, cmd, 2);
    printf("[SHT30] I2C0 init ok\n");
}

int sht30_read(float *temp, float *humi)
{
    uint8_t cmd[2] = {0xE0, 0x00}; /* 读测量数据 */
    uint8_t buf[6];
    uint8_t crc_data[2];
    uint16_t raw;
    int ok = 0;

    memset(buf, 0, 6);
    IoTI2cWrite(SHT30_I2C_PORT, SHT30_I2C_ADDRESS, cmd, 2);
    IoTI2cRead(SHT30_I2C_PORT, SHT30_I2C_ADDRESS, buf, 6);

    /* 温度: buf[0..2] */
    crc_data[0] = buf[0]; crc_data[1] = buf[1];
    if (sht30_check_crc(crc_data, 2, buf[2]) == 0) {
        raw = ((uint16_t)buf[0] << 8) | buf[1];
        *temp = sht30_calc_temp(raw);
        ok |= 0x01;
    }

    /* 湿度: buf[3..5] */
    crc_data[0] = buf[3]; crc_data[1] = buf[4];
    if (sht30_check_crc(crc_data, 2, buf[5]) == 0) {
        raw = ((uint16_t)buf[3] << 8) | buf[4];
        *humi = sht30_calc_rh(raw);
        ok |= 0x02;
    }

    return ok; /* 0x03=温湿度都成功 */
}

/* ============================================================
 * 门磁传感器（GPIO0_PB3）
 * 干簧管/磁簧开关：门关=磁铁靠近=低电平，门开=高电平
 * ============================================================ */

void door_init(void)
{
    IoTGpioInit(PIN_DOOR);
    IoTGpioSetDir(PIN_DOOR, IOT_GPIO_DIR_IN);
    printf("[DOOR] PB3 input init\n");
}

int door_read(void)
{
    IotGpioValue val = IOT_GPIO_VALUE0;
    IoTGpioGetInputVal(PIN_DOOR, &val);
    return (val == IOT_GPIO_VALUE1) ? 1 : 0;
}

/* ============================================================
 * 火焰传感器（GPIO0_PC4，DO 数字量）
 * 模块 DO 输出: 有火=高电平, 无火=低电平（电位器调阈值）
 * ============================================================ */

void flame_init(void)
{
    IoTGpioInit(PIN_FLAME);
    IoTGpioSetDir(PIN_FLAME, IOT_GPIO_DIR_IN);
    printf("[FLAME] PC4 input init\n");
}

int flame_read(void)
{
    IotGpioValue val = IOT_GPIO_VALUE0;
    IoTGpioGetInputVal(PIN_FLAME, &val);
    return (val == IOT_GPIO_VALUE1) ? 1 : 0;
}
