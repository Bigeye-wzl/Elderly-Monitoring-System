/*
 * 华为云 IoTDA MQTT 上报模块
 */
#ifndef CLOUD_MQTT_H
#define CLOUD_MQTT_H

#include "cloud_properties.h"

/* 启动云上报线程 (WiFi 就绪后自动连接 MQTT) */
int cloud_mqtt_start(void);

/* 上报传感器属性 (care_thread 主循环调用) */
void cloud_report_properties(const cloud_data_t *d);

/* 上报告警事件 */
void cloud_report_event(const char *event_id, const cloud_data_t *d);

/* 云是否就绪 */
int cloud_is_ready(void);

#endif /* CLOUD_MQTT_H */
