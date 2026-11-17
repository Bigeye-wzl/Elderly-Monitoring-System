#ifndef LCD_UI_H
#define LCD_UI_H

#include <stdint.h>

/* ---- 背景与卡片 ---- */
#define UI_BG           0xE71C      /* 浅灰背景 #E0E0E0 */
#define UI_CARD_BG      0xFFFF      /* 卡片白色 */
#define UI_CARD_BORDER  0xC618      /* 边框灰 #C0C0C0 */

/* ---- 标题栏 ---- */
#define UI_TITLE_BG     0x1949      /* 深蓝 */
#define UI_TITLE_FG     0xFFFF      /* 白 */

/* ---- 文字 ---- */
#define UI_LABEL_FG     0x52AA      /* 标签灰 */
#define UI_VALUE_FG     0x18A3      /* 数值深 */

/* ---- 状态色 ---- */
#define UI_VALUE_OK     0x1C9F      /* 正常蓝 */
#define UI_VALUE_GOOD   0x07E0      /* 良好绿 */
#define UI_VALUE_WARN   0xF980      /* 警告红 */
#define UI_VALUE_NA     0x8430      /* 无数据灰 */

/* ---- 图标色块 ---- */
#define UI_ICON_TEMP    0xFB46      /* 橙 */
#define UI_ICON_HUMI    0x05FA      /* 青 */
#define UI_ICON_PIR     0x8810      /* 紫 */
#define UI_ICON_GAS     0xFD20      /* 琥珀 */
#define UI_ICON_DOOR    0x8A22      /* 棕 */
#define UI_ICON_FLAME   0xFA80      /* 红橙 */
#define UI_ICON_SPO2    0xE8E4      /* 粉 */
#define UI_ICON_HR      0xF804      /* 红 */
#define UI_ICON_SOS     0xF980      /* 红 */
#define UI_ICON_FALL    0xA810      /* 紫 */
#define UI_ICON_POSE    0x0C9F      /* 蓝 */
#define UI_ICON_BUZZ    0xFE60      /* 黄 */

/* ---- 状态点 ---- */
#define UI_DOT_GOOD     0x07E0      /* 绿 */
#define UI_DOT_WARN     0xF980      /* 红 */
#define UI_DOT_NA       0x8430      /* 灰 */

void ui_init(void);
void ui_update_temp(float temp, int has_data);
void ui_update_humi(float humi, int has_data);
void ui_update_pir(int detected);
void ui_update_gas(int alarm);
void ui_update_door(int open);
void ui_update_sle(int active);  /* 报警状态: 1=告警 0=正常 */
void ui_update_posture(float pitch, float roll, int has_data);
void ui_update_fall(int fall, int has_data);
void ui_update_spo2(int spo2, int has_data);
void ui_update_heartrate(int hr, int has_data);
void ui_update_sos(int active);
void ui_update_flame(int fire);

/* 网络状态 */
void ui_set_wifi(int connected);
void ui_set_cloud(int connected);

/* 页面切换: 1=环境页 2=健康页 */
void ui_set_page(int page);
int  ui_get_page(void);

uint16_t ui_show_zh16(uint16_t x, uint16_t y, const char *zh, uint16_t fc, uint16_t bc);
uint16_t ui_show_zh16_str(uint16_t x, uint16_t y, const char *str, uint16_t fc, uint16_t bc);
uint16_t ui_show_zh24(uint16_t x, uint16_t y, const char *zh, uint16_t fc, uint16_t bc);
uint16_t ui_show_zh24_str(uint16_t x, uint16_t y, const char *str, uint16_t fc, uint16_t bc);

#endif
