/*
 * 守望.星护居 · 智慧养老监护系统 · 主程序
 *
 * 传感器:
 *   PIR 人体感应 (PA3) · MQ2 可燃气体 (ADC4) · SHT30 温湿度 (I2C0)
 *   门磁 (PB3)
 *   MPU6050 姿态 + MAX30102 血氧 + SOS (星闪网关 -> UART0 PB6/PB7)
 */
#include "los_task.h"
#include "ohos_init.h"
#include "iot_gpio.h"
#include "iot_adc.h"
#include "iot_errno.h"
#include "lcd.h"
#include "board_pins.h"
#include "sensors.h"
#include "lcd_ui.h"
#include "mpu_uart.h"
#include "alert.h"
#include "cloud_properties.h"
#include "cloud_mqtt.h"
#include "wifi_provision.h"
#include "nfc_provision.h"
#include <stdio.h>

static void board_safe_init(void) {
    unsigned int pins[]={PIN_MOTOR,PIN_RGB_R,PIN_RGB_G,PIN_RGB_B,PIN_BEEP};
    for(unsigned int i=0;i<sizeof(pins)/sizeof(pins[0]);i++){
        IoTGpioInit(pins[i]);IoTGpioSetDir(pins[i],IOT_GPIO_DIR_OUT);IoTGpioSetOutputVal(pins[i],IOT_GPIO_VALUE0);
    }
}

/* ============================================================
 * K3~K6 按键 (ADC ch7, 分压)
 * K3≈10mV  K4≈550mV  K5≈1V  K6≈1.65V
 * 返回: 0=无  3=K3  4=K4  5=K5  6=K6
 * ============================================================ */
static int read_key(void) {
    unsigned int data = 0;
    if (IoTAdcGetVal(KEY_ADC_CHANNEL, &data) != IOT_SUCCESS) return 0;
    if (data < 50)   return 3;                  /* K3 */
    if (data > 120 && data < 220) return 4;     /* K4 */
    if (data > 260 && data < 360) return 5;     /* K5 */
    if (data > 450 && data < 560) return 6;     /* K6 */
    return 0;
}

/* ============================================================
 * WiFi 配网 — 独立任务, 避免阻塞传感器轮询
 * ============================================================ */
static void wifi_thread(void){
    wifi_provision_start();
    printf("[CARE] wifi_provision_start returned, connected=%d\n", wifi_provision_is_connected());
}

/* ============================================================
 * 主传感器循环
 * ============================================================ */
static void care_thread(void) {
    int pir_ok = 0;
    int gas_raw, gas_alarm = 0;
    float temperature = 0, humidity = 0;
    int sht_ok = 0;
    int door_val, door_ok = 0;
    mpu_data_t mpu_data = {0};
    spo2_data_t spo2_data = {0};
    int mpu_ok = 0, spo2_ok = 0, sos_active = 0;
    int fall_alarmed = 0;
    int fall_armed = 0;
    unsigned long long fall_start_tick = 0;
    int sos_hold = 0;
    int fall_cap = 0;
    int flame_hold = 0;
    int last_key = 0;

    printf("[CARE] ==== 守望.星护居 ====\n");
    board_safe_init();
    alert_init();
    lcd_init(0);
    ui_init();
    pir_init(); gas_init(); sht30_init(); door_init(); flame_init(); mpu_uart_init();
    IoTAdcInit(KEY_ADC_CHANNEL);

    /* 快速首读: 在云连接前先把数据刷上屏 */
    pir_ok = pir_read();        ui_update_pir(pir_ok);
    gas_raw = gas_read_raw();   if (gas_raw >= 0) ui_update_gas(gas_is_alarm(gas_raw));
    sht_ok = sht30_read(&temperature, &humidity);
    if (sht_ok == 0x03) { ui_update_temp(temperature, 1); ui_update_humi(humidity, 1); }
    door_val = door_read();     ui_update_door(door_val);
    ui_update_flame(flame_read());

    cloud_mqtt_start();

    /* WiFi 配网独立任务 */
    {
        unsigned int wtid; TSK_INIT_PARAM_S wtask = {0};
        wtask.pfnTaskEntry = (TSK_ENTRY_FUNC)wifi_thread;
        wtask.uwStackSize = 20480; wtask.pcName = "wifi_prov"; wtask.usTaskPrio = 20;
        if (LOS_TaskCreate(&wtid, &wtask) != LOS_OK) printf("[CARE] wifi task create fail\n");
    }

    printf("[CARE] init done, sensors running (WiFi in background)\n");

    while (1) {
        /* ---- 本地传感器 ---- */
        pir_ok = pir_read();
        ui_update_pir(pir_ok);

        gas_raw = gas_read_raw();
        if (gas_raw >= 0) {
            gas_alarm = gas_is_alarm(gas_raw);
            ui_update_gas(gas_alarm);
            if (gas_alarm) {
                sos_hold = 250;
                alert_set(ALERT_SRC_SOS, 1);
            }
        }

        sht_ok = sht30_read(&temperature, &humidity);
        if (sht_ok == 0x03) { ui_update_temp(temperature, 1); ui_update_humi(humidity, 1); }

        door_val = door_read();
        if (door_val != door_ok) { door_ok = door_val; ui_update_door(door_ok); }

        /* ---- 星闪数据快速轮询 (20ms x 100 = 2s) ---- */
        for (int fast = 0; fast < 100; fast++) {
            /* WiFi/云 状态更新 (每秒一次) */
            if (fast % 50 == 0) {
                ui_set_wifi(wifi_provision_is_connected());
                ui_set_cloud(cloud_is_ready());
            }

            /* 按键翻页: K5=环境页(1) K6=健康页(2) */
            {
                int btn = read_key();
                if (btn != 0 && last_key == 0) {
                    if (btn == 5) ui_set_page(1);
                    if (btn == 6) ui_set_page(2);
                }
                last_key = btn;
            }

            /* 火焰检测 */
            {
                int fire = flame_read();
                ui_update_flame(fire);
                if (fire) {
                    flame_hold = 150;
                    alert_set(ALERT_SRC_FIRE, 1);
                }
            }

            int r = mpu_uart_read(&mpu_data, &spo2_data);
            if (r == 0 || r == 2) {
                mpu_ok = 1;
                ui_update_fall(mpu_data.fall, 1);
                ui_update_posture(mpu_data.pitch, mpu_data.roll, 1);
            }

            /* 跌倒 5s 持续确认 */
            if (mpu_ok) {
                if (!mpu_data.fall) {
                    fall_armed = 1;
                    fall_start_tick = 0;
                    if (fall_alarmed) {
                        fall_alarmed = 0;
                        alert_set(ALERT_SRC_FALL, 0);
                        alert_set(ALERT_SRC_SOS, 0);
                        fall_cap = 0;
                        sos_hold = 0;
                    }
                } else if (fall_armed) {
                    if (fall_start_tick == 0) fall_start_tick = LOS_TickCountGet();
                    if (!fall_alarmed && (LOS_TickCountGet() - fall_start_tick) >= 500ULL) {
                        fall_alarmed = 1;
                        printf("[CARE] *** FALL ALARM! (5s sustained) ***\n");
                        fall_cap = 1500;
                        sos_hold = 1500;
                        alert_set(ALERT_SRC_FALL, 1);
                        alert_set(ALERT_SRC_SOS, 1);
                    }
                }
            }

            if (r == 1 || r == 2) {
                if (r == 2) mpu_uart_get_last_spo2(&spo2_data);
                spo2_ok = 1;
                ui_update_spo2(spo2_data.spo2, 1);
                ui_update_heartrate(spo2_data.heart_rate, 1);
                if (spo2_data.sos != sos_active) {
                    sos_active = spo2_data.sos;
                    if (sos_active) printf("[CARE] *** SOS ALARM! ***\n");
                }
                ui_update_sos(sos_active);
                if (spo2_data.sos) {
                    sos_hold = 150;
                    alert_set(ALERT_SRC_SOS, 1);
                }
            }

            /* 报警倒计时 */
            if (sos_hold > 0) { sos_hold--; if (sos_hold == 0) alert_set(ALERT_SRC_SOS, 0); }
            if (fall_cap > 0) { fall_cap--; if (fall_cap == 0) alert_set(ALERT_SRC_FALL, 0); }
            if (flame_hold > 0) { flame_hold--; if (flame_hold == 0) alert_set(ALERT_SRC_FIRE, 0); }

            LOS_Msleep(20);
        }

        /* ---- 串口日志 ---- */
        printf("[CARE] T=%.0f H=%.0f PIR=%d GAS=%d DOOR=%d FLAME=%d MPU=%s SPO2=%s SOS=%d",
            temperature, humidity, pir_ok, gas_raw, door_ok, flame_read(),
            mpu_ok ? "OK" : "-", spo2_ok ? "OK" : "-", sos_active);
        {
            uint32_t ub = 0, um = 0, us = 0, uf = 0;
            mpu_uart_get_stats(&ub, &um, &us, &uf);
            printf(" | UART rx=%u mpu=%u spo2=%u fail=%u wifi=%d\n",
                ub, um, us, uf, wifi_provision_is_connected());
        }

        /* 报警状态 */
        ui_update_sle(alert_is_active());

        /* ---- 华为云上报 (每 5s) ---- */
        {
            cloud_data_t cd = {0};
            cd.spo2            = spo2_ok ? spo2_data.spo2 : 0;
            cd.heart_rate      = spo2_ok ? spo2_data.heart_rate : 0;
            cd.sos_state       = sos_active;
            cd.fall_state      = fall_alarmed;
            cd.temp            = 0;
            cd.client1_online  = mpu_ok;
            cd.client2_online  = spo2_ok;
            cd.accel_x = mpu_ok ? (float)mpu_data.ax / 16384.0f * 9.8f : 0;
            cd.accel_y = mpu_ok ? (float)mpu_data.ay / 16384.0f * 9.8f : 0;
            cd.accel_z = mpu_ok ? (float)mpu_data.az / 16384.0f * 9.8f : 0;
            cd.gyro_x  = mpu_ok ? (float)mpu_data.gx / 131.0f : 0;
            cd.gyro_y  = mpu_ok ? (float)mpu_data.gy / 131.0f : 0;
            cd.gyro_z  = mpu_ok ? (float)mpu_data.gz / 131.0f : 0;
            cd.env_temp        = temperature;
            cd.env_humi        = humidity;
            cd.gas_concentration = (float)gas_raw;
            cd.human_detected  = pir_ok;
            cd.door_state      = door_ok;
            cd.alarm_active    = alert_is_active();
            cloud_report_properties(&cd);
        }
        LOS_Msleep(3000);
    }
}

void elderly_care_example(void) {
    unsigned int tid; TSK_INIT_PARAM_S task = {0};
    task.pfnTaskEntry = (TSK_ENTRY_FUNC)care_thread;
    task.uwStackSize = 40960; task.pcName = "care_thread"; task.usTaskPrio = 24;
    if (LOS_TaskCreate(&tid, &task) != LOS_OK) printf("task create fail\n");
}

APP_FEATURE_INIT(elderly_care_example);
