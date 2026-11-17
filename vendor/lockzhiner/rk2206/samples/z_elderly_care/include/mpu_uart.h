/*
 * 星闪网关 UART 接收模块
 *
 * 从星闪网关接收 MPU6050 / MAX30102 数据 (JSON 格式)
 * 网关 GP8(TX) → RK2206 PB6(RX) = EUART0_M0
 *
 * JSON 格式 (带前缀标识):
 * [MPU]{"ax":-1463,"ay":1665,"az":7993,...,"fall":1}
 * [SPO2]{"spo2":98,"heart_rate":72,"sos":0}
 */
#ifndef MPU_UART_H
#define MPU_UART_H

#include <stdint.h>

/* MPU6050 数据结构 */
typedef struct {
    int16_t ax, ay, az;      /* 加速度原始值 */
    int16_t gx, gy, gz;      /* 陀螺仪原始值 */
    float   pitch;            /* 俯仰角(度) */
    float   roll;             /* 横滚角(度) */
    int     fall;             /* 摔倒标志: 0=正常, 1=摔倒 */
    int     valid;            /* 数据有效标志 */
} mpu_data_t;

/* MAX30102 血氧数据结构 */
typedef struct {
    int spo2;                 /* 血氧饱和度 (%) */
    int heart_rate;           /* 心率 (bpm) */
    int sos;                  /* SOS 按键: 0=未按, 1=按下 */
    int valid;                /* 数据有效标志 */
} spo2_data_t;

/*
 * 初始化 UART (EUART0_M0, PB6/PB7, 115200)
 * 返回 0=成功, -1=失败
 */
int mpu_uart_init(void);

/*
 * 读取并解析数据 (非阻塞, 自动识别 MPU/SPO2)
 * 返回 0=成功解析到 MPU 数据, 1=成功解析到 SPO2 数据, -1=无数据
 */
int mpu_uart_read(mpu_data_t *mpu, spo2_data_t *spo2);

/*
 * 获取最近一次有效 MPU 数据
 * 返回 0=有数据, -1=无数据
 */
int mpu_uart_get_last_mpu(mpu_data_t *data);

/*
 * 获取最近一次有效 SPO2 数据
 * 返回 0=有数据, -1=无数据
 */
int mpu_uart_get_last_spo2(spo2_data_t *data);

/*
 * 诊断统计: 累计收字节 / MPU帧 / SPO2帧 / 解析失败
 * 各参数可传 NULL
 */
void mpu_uart_get_stats(uint32_t *bytes, uint32_t *mpu, uint32_t *spo2, uint32_t *fail);

#endif /* MPU_UART_H */
