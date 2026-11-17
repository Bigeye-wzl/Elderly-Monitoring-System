/*
 * 华为云 IoTDA MQTT 上报模块
 *
 * 基于 oc_mqtt 库 (d5_iot_cloud_body_induction 例程)
 * 定时上报传感器属性
 *
 * 使用前须在华为云 IoTDA 控制台:
 *   1. 创建产品 → 定义服务/属性模型 (见 cloud_properties.h)
 *   2. 创建设备 → 获取 ClientID / Username / Password
 *   3. 将三元组填入下方 CLOUD_CLIENT_ID / CLOUD_USERNAME / CLOUD_PASSWORD
 */
#include <stdio.h>
#include <string.h>
#include "los_task.h"
#include "oc_mqtt.h"
#include "cloud_mqtt.h"
#include "cloud_properties.h"
#include "wifi_provision.h"

/* ============================================================
 * 华为云设备三元组
 * IoTDA 控制台 → 设备详情 → MQTT 接入
 * ============================================================ */
#define CLOUD_CLIENT_ID   "your-client-id"       /* TODO: 替换为你的设备 ClientID */
#define CLOUD_USERNAME    "your-username"        /* TODO: 替换为你的设备 Username */
#define CLOUD_PASSWORD    "your-password"        /* TODO: 替换为你的设备 Password */

static int s_cloud_ready = 0;

int cloud_is_ready(void) { return s_cloud_ready; }

/* ============================================================
 * 属性上报: 把传感器数据打包成华为云 profile 格式
 *
 * 服务: health (唯一)
 * 生成的 JSON:
 * {
 *   "services": [
 *     {"service_id":"health","properties":{
 *       "spo2":98,"heart_rate":75,"sos_state":0,"fall_state":0,
 *       "client1_online":1,"client2_online":1,
 *       "accel_x":0.12,"accel_y":-0.35,"accel_z":9.81,
 *       "gyro_x":0.05,"gyro_y":-0.12,"gyro_z":0.01,
 *       "temp":33,
 *       "env_temp":26,"env_humi":45.5,"gas_concentration":15.5,
 *       "human_detected":1,"door_state":0
 *     }}
 *   ]
 * }
 * ============================================================ */
void cloud_report_properties(const cloud_data_t *d)
{
    if (!s_cloud_ready || d == NULL) return;

    /* health 服务 — 所有属性放在一个链表 */
    oc_mqtt_profile_kv_t kv_spo2   = {NULL,              PROP_SPO2,        EN_OC_MQTT_PROFILE_VALUE_INT,   (void *)&d->spo2};
    oc_mqtt_profile_kv_t kv_hr     = {&kv_spo2,          PROP_HEART_RATE,  EN_OC_MQTT_PROFILE_VALUE_INT,   (void *)&d->heart_rate};
    oc_mqtt_profile_kv_t kv_sos    = {&kv_hr,            PROP_SOS_STATE,   EN_OC_MQTT_PROFILE_VALUE_INT,   (void *)&d->sos_state};
    oc_mqtt_profile_kv_t kv_fall   = {&kv_sos,           PROP_FALL_STATE,  EN_OC_MQTT_PROFILE_VALUE_INT,   (void *)&d->fall_state};
    oc_mqtt_profile_kv_t kv_temp   = {&kv_fall,          PROP_TEMP,        EN_OC_MQTT_PROFILE_VALUE_INT,   (void *)&d->temp};
    oc_mqtt_profile_kv_t kv_c1     = {&kv_temp,          PROP_CLIENT1,     EN_OC_MQTT_PROFILE_VALUE_INT,   (void *)&d->client1_online};
    oc_mqtt_profile_kv_t kv_c2     = {&kv_c1,            PROP_CLIENT2,     EN_OC_MQTT_PROFILE_VALUE_INT,   (void *)&d->client2_online};
    oc_mqtt_profile_kv_t kv_ax     = {&kv_c2,            PROP_ACCEL_X,     EN_OC_MQTT_PROFILE_VALUE_FLOAT, (void *)&d->accel_x};
    oc_mqtt_profile_kv_t kv_ay     = {&kv_ax,            PROP_ACCEL_Y,     EN_OC_MQTT_PROFILE_VALUE_FLOAT, (void *)&d->accel_y};
    oc_mqtt_profile_kv_t kv_az     = {&kv_ay,            PROP_ACCEL_Z,     EN_OC_MQTT_PROFILE_VALUE_FLOAT, (void *)&d->accel_z};
    oc_mqtt_profile_kv_t kv_gx     = {&kv_az,            PROP_GYRO_X,      EN_OC_MQTT_PROFILE_VALUE_FLOAT, (void *)&d->gyro_x};
    oc_mqtt_profile_kv_t kv_gy     = {&kv_gx,            PROP_GYRO_Y,      EN_OC_MQTT_PROFILE_VALUE_FLOAT, (void *)&d->gyro_y};
    oc_mqtt_profile_kv_t kv_gz     = {&kv_gy,            PROP_GYRO_Z,      EN_OC_MQTT_PROFILE_VALUE_FLOAT, (void *)&d->gyro_z};
    oc_mqtt_profile_kv_t kv_etemp  = {&kv_gz,            PROP_ENV_TEMP,    EN_OC_MQTT_PROFILE_VALUE_FLOAT, (void *)&d->env_temp};
    oc_mqtt_profile_kv_t kv_ehumi  = {&kv_etemp,         PROP_ENV_HUMI,    EN_OC_MQTT_PROFILE_VALUE_FLOAT, (void *)&d->env_humi};
    oc_mqtt_profile_kv_t kv_gas    = {&kv_ehumi,         PROP_GAS_CONC,    EN_OC_MQTT_PROFILE_VALUE_FLOAT, (void *)&d->gas_concentration};
    oc_mqtt_profile_kv_t kv_human  = {&kv_gas,           PROP_HUMAN,       EN_OC_MQTT_PROFILE_VALUE_INT,   (void *)&d->human_detected};
    oc_mqtt_profile_kv_t kv_door   = {&kv_human,         PROP_DOOR,        EN_OC_MQTT_PROFILE_VALUE_INT,   (void *)&d->door_state};
    oc_mqtt_profile_kv_t kv_alarm  = {&kv_door,          PROP_ALARM,       EN_OC_MQTT_PROFILE_VALUE_INT,   (void *)&d->alarm_active};

    oc_mqtt_profile_service_t svc = {NULL, SVC_HEALTH, NULL, &kv_alarm};

    int ret = oc_mqtt_profile_propertyreport(CLOUD_USERNAME, &svc);
    if (ret != 0) {
        printf("[CLOUD] property report fail ret=%d\n", ret);
    }
}

/* ============================================================
 * 事件上报: 告警事件 (摔倒/SOS/气体) — 用属性上报带上标记
 * ============================================================ */
void cloud_report_event(const char *event_id, const cloud_data_t *d)
{
    (void)event_id;
    (void)d;
    /* 简化: 直接调 cloud_report_properties 即可, 告警属性里已包含状态 */
}

/* ============================================================
 * 云连接线程: WiFi 就绪后初始化 MQTT
 * ============================================================ */
static void cloud_thread(void)
{
    int ret;
    int wait_count = 0;

    printf("[CLOUD] waiting for WiFi...\n");
    while (!wifi_provision_is_connected()) {
        LOS_Msleep(2000);
        wait_count++;
        if (wait_count % 5 == 0) printf("[CLOUD] still waiting WiFi... (%ds)\n", wait_count * 2);
        if (wait_count > 90) {
            printf("[CLOUD] WiFi timeout 3min, give up\n");
            return;
        }
    }
    printf("[CLOUD] WiFi connected, starting MQTT...\n");
    device_info_init(CLOUD_CLIENT_ID, CLOUD_USERNAME, CLOUD_PASSWORD);

    /* MQTT 连接失败自动重试, 每 10 秒一次 */
    for (int retry = 0; retry < 30; retry++) {
        printf("[CLOUD] oc_mqtt_init attempt #%d\n", retry + 1);
        ret = oc_mqtt_init();
        if (ret == 0) {
            s_cloud_ready = 1;
            printf("[CLOUD] *** MQTT connected to Huawei Cloud IoTDA ***\n");
            break;
        }
        printf("[CLOUD] oc_mqtt_init FAIL ret=%d, retry in 10s...\n", ret);
        LOS_Msleep(10000);
    }
    if (!s_cloud_ready) {
        printf("[CLOUD] MQTT connect failed 30 times, giving up\n");
        return;
    }

    while (1) {
        /* 数据由 care_thread 调 cloud_report_properties 上报 */
        LOS_Msleep(5000);
    }
}

int cloud_mqtt_start(void)
{
    unsigned int tid;
    TSK_INIT_PARAM_S task = {0};
    task.pfnTaskEntry = (TSK_ENTRY_FUNC)cloud_thread;
    task.uwStackSize = 8192;
    task.pcName = "cloud_mqtt";
    task.usTaskPrio = 28;  /* 必须低于 care_thread(24), 否则 MQTT 阻塞饿死传感器循环 */
    if (LOS_TaskCreate(&tid, &task) != LOS_OK) {
        printf("[CLOUD] task create fail\n");
        return -1;
    }
    printf("[CLOUD] cloud_mqtt task started\n");
    return 0;
}
