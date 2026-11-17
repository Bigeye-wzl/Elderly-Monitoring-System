/*
 * NFC 碰一碰跳转配网
 *
 * NT3H1201 NFC 标签 (I2C1, GPIO0_D5/D6)
 * 写入 URL → 手机碰一下 → 浏览器自动打开
 */
#include "nfc_provision.h"
#include "wifi_provision.h"
#include "nfc.h"
#include <stdio.h>
#include <string.h>

#define LOG_TAG "[nfc_prov]"
#define TX_LOG(fmt, ...)  printf(LOG_TAG " " fmt "\n", ##__VA_ARGS__)

int nfc_provision_init(void)
{
    unsigned int ret;
    uint8_t url[64];

    TX_LOG("=== NFC 配网标签初始化 ===");

    /* 初始化 NFC (I2C1) */
    ret = nfc_init();
    if (ret != 0) {
        TX_LOG("NFC init fail: %u", ret);
        return -1;
    }

    /* 写入配网 URL */
    snprintf((char *)url, sizeof(url), "%s", PROVISION_HTTP_IP);
    TX_LOG("写入 NFC URL: http://%s", url);

    if (nfc_store_uri_http(NDEFFirstPos, url)) {
        TX_LOG("NFC URL 写入成功!");
        TX_LOG("手机碰 NFC → 浏览器打开 http://%s", url);
        return 0;
    }

    TX_LOG("NFC URL 写入失败!");
    return -1;
}

int nfc_provision_set_url(const char *url)
{
    if (nfc_store_uri_http(NDEFLastPos, (uint8_t *)url)) {
        TX_LOG("NFC URL 更新: %s", url);
        return 0;
    }
    return -1;
}
