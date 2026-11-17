# 守望·星护居 —— 智慧养老监护系统

基于 RK2206（OpenHarmony 3.0 LTS · LiteOS-M）的独居老人智慧养老监护终端。

板载环境安全传感器 + 星闪（SLE）穿戴健康节点 + 本地声光告警 + LCD 中文界面 + 华为云 IoTDA 实时上云。

## 功能特性

| 模块 | 功能 |
|---|---|
| 环境安全监测 | PIR 人体感应 · MQ2 可燃气体 · SHT30 温湿度 · 门磁 · 火焰传感器 |
| 星闪健康节点 | 腿绑 MPU6050（跌倒检测）· 腕戴 MAX30102（血氧/心率）· SOS 一键呼救，经星闪网关接入 UART0 |
| 本地告警 | 蜂鸣器 · RGB 灯 · 震动电机，统一报警状态机 |
| 人机交互 | LCD ST7789V 中文实时界面 · K3~K6 按键 |
| 配网 | NFC 碰一碰配网 · AP 模式 Web 页面 WiFi 配网 |
| 华为云 IoTDA | MQTT 属性上报（`health` 服务）· 血氧/心率/跌倒/SOS/环境数据上云 |

## 硬件平台

- 开发板：通晓 TX-SMART-R（RK2206），引脚定义见 [`vendor/lockzhiner/rk2206/samples/z_elderly_care/include/board_pins.h`](vendor/lockzhiner/rk2206/samples/z_elderly_care/include/board_pins.h)
- 外接：MPU6050 + MAX30102 + SOS 按键（星闪节点）、门磁、火焰传感器
- 显示：ST7789V SPI LCD（SPI0_M1）

## 目录结构

项目源码位于 `vendor/lockzhiner/rk2206/samples/z_elderly_care/`：

```
z_elderly_care/
├── src/
│   ├── elderly_care_example.c   # 主程序（任务调度、按键、UI 刷新）
│   ├── sensors.c                # PIR / MQ2 / SHT30 / 门磁 / 火焰
│   ├── mpu_uart.c               # 星闪网关 UART 协议（MPU6050/MAX30102/SOS）
│   ├── alert.c                  # 声光/电机告警状态机
│   ├── lcd_ui.c                 # LCD 中文界面
│   ├── wifi_provision.c         # AP 模式 Web 配网
│   ├── nfc_provision.c          # NFC 碰一碰配网（NT3H）
│   ├── cloud_mqtt.c             # 华为云 IoTDA 上报（三元组在此配置）
│   └── oc_mqtt*.c               # oc_mqtt 移植层
└── include/
    ├── board_pins.h             # 板级引脚定义
    └── cloud_properties.h       # 华为云产品模型（服务/属性名）
```

## 开发环境

- Ubuntu 22.04（WSL2 / Docker / 原生均可）
- Python 3、GN/Ninja、`hb` 构建工具（OpenHarmony 标准）
- 环境搭建详细步骤见 [`vendor/lockzhiner/rk2206/README_zh.md`](vendor/lockzhiner/rk2206/README_zh.md)

## 编译

```shell
hb set -root .
hb set
# 选择 lockzhiner → lockzhiner-rk2206
hb build -f
```

编译产物（固件镜像）位于 `out/rk2206/lockzhiner-rk2206/images/`。

## 打包固件（可选）

`package_fw.sh` 可手动打包完整固件镜像（自动从 `sdk_liteos/include/link.h` 解析分区表，行为与官方 `mkimage.sh` 一致）：

```shell
./package_fw.sh
```

## 烧录

```shell
cd device/rockchip/tools/linux
sudo python3 flash.py
```

## 华为云 IoTDA 配置

1. 在华为云 IoTDA 控制台创建产品，按 [`cloud_properties.h`](vendor/lockzhiner/rk2206/samples/z_elderly_care/include/cloud_properties.h) 定义服务/属性模型（服务 ID：`health`）
2. 创建设备，获取 MQTT 接入三元组（ClientID / Username / Password）
3. 填入 [`src/cloud_mqtt.c`](vendor/lockzhiner/rk2206/samples/z_elderly_care/src/cloud_mqtt.c) 顶部的 `CLOUD_CLIENT_ID` / `CLOUD_USERNAME` / `CLOUD_PASSWORD`

> 仓库中三元组为占位符，请替换为你自己设备的凭据后再编译烧录。

## 致谢

- 本项目基于 [福州凌智电子 lockzhiner-rk2206-openharmony3.0lts](https://gitee.com/Lockzhiner-Electronics/lockzhiner-rk2206-openharmony3.0lts)（OpenHarmony 3.0 LTS）修改构建，遵循 Apache License 2.0
- 板级引脚定义参考通晓 TX-SMART-R 官方仓库（isoh_edu/txsmartropenharmony）

## License

Apache License 2.0，详见 [LICENSE](LICENSE)。
