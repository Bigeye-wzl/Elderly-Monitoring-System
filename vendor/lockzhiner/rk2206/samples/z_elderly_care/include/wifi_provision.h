/*
 * NFC 碰一碰 WiFi 配网
 *
 * 流程:
 *   1. 开机检查 KV 存储有无 WiFi 信息
 *   2. 有 → STA 模式连 WiFi → 正常工作
 *   3. 无 → AP 模式 (热点 "ElderlyCare-XXXX") + HTTP 服务器
 *   4. NFC 标签写入 http://192.168.4.1
 *   5. 手机碰 NFC → 浏览器打开配网页 → 输入 WiFi → 提交
 *   6. 存 KV → 重启 → STA 模式
 *
 * 长按 K1 (PC7) 5秒 → 清除 WiFi 信息 → 恢复配网模式
 */
#ifndef WIFI_PROVISION_H
#define WIFI_PROVISION_H

/* 配网 AP 热点名称 */
#define PROVISION_AP_SSID   "ElderlyCare"
#define PROVISION_AP_PWD    "12345678"

/* AP 模式下 HTTP 服务器地址 (手机浏览器访问) */
/* RK2206 AP 接口 IP 固定为 192.168.2.10 (见日志 wifi_api ip=192.168.2.10) */
#define PROVISION_HTTP_IP   "192.168.2.10"
#define PROVISION_HTTP_PORT 80

/*
 * 启动 WiFi 配网管理
 * - 检查已存 WiFi → 有则 STA 连接, 无则开 AP 配网
 * - 返回 0: 已连上 WiFi, -1: 进入配网模式 (会阻塞)
 */
int wifi_provision_start(void);

/*
 * 获取当前 WiFi 连接状态
 * 返回 1=已连接, 0=未连接/配网中
 */
int wifi_provision_is_connected(void);

/* 清除已存 WiFi 信息 (下次重启进入配网) */
void wifi_provision_clear(void);

#endif /* WIFI_PROVISION_H */
