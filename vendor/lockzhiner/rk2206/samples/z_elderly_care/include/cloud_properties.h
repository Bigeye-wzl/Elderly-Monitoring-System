/*
 * 华为云 IoTDA 属性定义
 *
 * 服务和属性名必须与华为云控制台 → 产品 → 模型 完全一致！
 *
 * 服务 ID: health (唯一服务, 所有属性都在这个服务下)
 *
 * MQTT Topic 格式：
 *   属性上报: $oc/devices/{device_id}/sys/properties/report
 */
#ifndef CLOUD_PROPERTIES_H
#define CLOUD_PROPERTIES_H

/* ============================================================
 * 服务 ID — 与 IoTDA 产品模型一致
 * ============================================================ */
#define SVC_HEALTH          "health"

/* ============================================================
 * 属性名 — 与 IoTDA 产品模型完全一致
 * ============================================================ */
/* 健康数据 (来自星闪节点) */
#define PROP_SPO2           "spo2"              /* 血氧饱和度, int, % */
#define PROP_HEART_RATE     "heart_rate"        /* 心率, int, bpm */
#define PROP_SOS_STATE      "sos_state"         /* SOS 按键, int: 0=无 1=按下 */
#define PROP_FALL_STATE     "fall_state"        /* 摔倒检测, int: 0=正常 1=摔倒 */
#define PROP_TEMP           "temp"              /* 体温, int, 0.1℃ */

/* 节点在线状态 */
#define PROP_CLIENT1        "client1_online"    /* 腿绑节点(MPU)在线, int */
#define PROP_CLIENT2        "client2_online"    /* 腕戴节点(SPO2)在线, int */

/* IMU 原始数据 (来自 MPU6050) */
#define PROP_ACCEL_X        "accel_x"           /* 加速度 X, float, m/s² */
#define PROP_ACCEL_Y        "accel_y"           /* 加速度 Y, float, m/s² */
#define PROP_ACCEL_Z        "accel_z"           /* 加速度 Z, float, m/s² */
#define PROP_GYRO_X         "gyro_x"            /* 陀螺仪 X, float, °/s */
#define PROP_GYRO_Y         "gyro_y"            /* 陀螺仪 Y, float, °/s */
#define PROP_GYRO_Z         "gyro_z"            /* 陀螺仪 Z, float, °/s */

/* 环境数据 */
#define PROP_ENV_TEMP       "env_temp"          /* 环境温度, float, ℃ */
#define PROP_ENV_HUMI       "env_humi"          /* 环境湿度, float, % */
#define PROP_GAS_CONC       "gas_concentration" /* 可燃气体浓度, float */

/* 安全监测 */
#define PROP_HUMAN          "human_detected"    /* 人体感应, int: 0=无人 1=有人 */
#define PROP_DOOR           "door_state"        /* 门磁状态, int: 0=关 1=开 */
#define PROP_ALARM          "alarm_active"      /* 统一报警, int: 0=无 1=报警中 */

/* ============================================================
 * MQTT Topic 模板
 * ============================================================ */
#define TOPIC_PROPERTY_REPORT  "$oc/devices/%s/sys/properties/report"

/* ============================================================
 * 数据上报结构体
 * ============================================================ */
typedef struct {
    /* 健康 */
    int     spo2;
    int     heart_rate;
    int     sos_state;
    int     fall_state;
    int     temp;

    /* 节点在线 */
    int     client1_online;
    int     client2_online;

    /* IMU */
    float   accel_x, accel_y, accel_z;
    float   gyro_x, gyro_y, gyro_z;

    /* 环境 */
    float   env_temp;
    float   env_humi;
    float   gas_concentration;

    /* 安全 */
    int     human_detected;
    int     door_state;
    int     alarm_active;
} cloud_data_t;

#endif /* CLOUD_PROPERTIES_H */
