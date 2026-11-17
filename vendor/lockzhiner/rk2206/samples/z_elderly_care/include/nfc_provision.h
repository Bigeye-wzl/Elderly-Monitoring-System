/*
 * NFC 碰一碰跳转配网
 *
 * 写入 NT3H1201 NFC 标签:
 *   URI: http://192.168.4.1  (配网页面)
 *
 * 手机碰 NFC → 自动打开浏览器 → 配网页
 */
#ifndef NFC_PROVISION_H
#define NFC_PROVISION_H

/*
 * 初始化 NFC 并写入配网 URL
 * 返回 0=成功, -1=失败
 */
int nfc_provision_init(void);

/*
 * 更新 NFC 标签中的 URL (如改成华为云控制台)
 */
int nfc_provision_set_url(const char *url);

#endif /* NFC_PROVISION_H */
