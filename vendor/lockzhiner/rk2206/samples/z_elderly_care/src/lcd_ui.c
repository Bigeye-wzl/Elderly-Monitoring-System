/*
 * lcd_ui.c — 卡片式 LCD UI (240x320 ST7789V)
 *
 * 布局:
 *   0~38    标题栏 (深蓝) "守望·星护居" + WiFi/云图标 + 页码
 *   38~320  内容区 (浅灰底 + 白色卡片)
 *
 *   环境页: 温度|湿度  人体|气体  FIRE|门磁
 *   健康页: 血氧|HR    SOS|摔倒   POSE|告警
 *
 * 渲染: 每张卡片一次性画完(底+边框+图标+标签+值),
 *       不会出现"先出线后出字"的撕裂感。
 * 翻页: 只画当前页内容, 数值更新只擦值区域。
 */
#include "lcd.h"
#include "lcd_ui.h"
#include "lcd_font_zh.h"
#include <string.h>
#include <stdio.h>

#define SW16(x) ((uint16_t)((((uint16_t)(x)&0x00FFU)<<8)|(((uint16_t)(x)&0xFF00U)>>8)))

static uint16_t s_pix[32*32];

/* ============================================================
 * 字体渲染
 * ============================================================ */
static const void *find16(const char *s) {
    for (uint32_t i=0;i<TFONT_ZH16_COUNT;i++)
        if (tfont_zh16[i].Index[0]==(uint8_t)s[0]&&tfont_zh16[i].Index[1]==(uint8_t)s[1]&&tfont_zh16[i].Index[2]==(uint8_t)s[2])
            return &tfont_zh16[i];
    return NULL;
}
static const void *find24(const char *s) {
    for (uint32_t i=0;i<TFONT_ZH24_COUNT;i++)
        if (tfont_zh24[i].Index[0]==(uint8_t)s[0]&&tfont_zh24[i].Index[1]==(uint8_t)s[1]&&tfont_zh24[i].Index[2]==(uint8_t)s[2])
            return &tfont_zh24[i];
    return NULL;
}

static void draw_zh16(uint16_t x,uint16_t y,const char *s,uint16_t fc,uint16_t bc) {
    const zh_font16_t *f=find16(s);
    if(!f){lcd_fill(x,y,x+16,y+16,bc);return;}
    uint16_t fw=SW16(fc),bw=SW16(bc);
    for(int i=0;i<32;i++){uint8_t b=f->Msk[i];for(int j=0;j<8;j++)s_pix[i*8+j]=(b&(1<<j))?fw:bw;}
    lcd_show_picture(x,y,16,16,(uint8_t*)s_pix);
}
static void draw_zh24(uint16_t x,uint16_t y,const char *s,uint16_t fc,uint16_t bc) {
    const zh_font24_t *f=find24(s);
    if(!f){lcd_fill(x,y,x+24,y+24,bc);return;}
    uint16_t fw=SW16(fc),bw=SW16(bc);
    for(int i=0;i<72;i++){uint8_t b=f->Msk[i];for(int j=0;j<8;j++)s_pix[i*8+j]=(b&(1<<j))?fw:bw;}
    lcd_show_picture(x,y,24,24,(uint8_t*)s_pix);
}

static uint16_t mix16(uint16_t x,uint16_t y,const char *s,uint16_t fc,uint16_t bc) {
    while(*s){if((uint8_t)*s>=0x80){draw_zh16(x,y,s,fc,bc);x+=16;s+=3;}else{lcd_show_char(x,y,*s,fc,bc,16,0);x+=8;s++;}}
    return x;
}
static uint16_t mix24(uint16_t x,uint16_t y,const char *s,uint16_t fc,uint16_t bc) {
    while(*s){if((uint8_t)*s>=0x80){draw_zh24(x,y,s,fc,bc);x+=24;s+=3;}else{lcd_show_char(x,y,*s,fc,bc,24,0);x+=12;s++;}}
    return x;
}
/* 数值: ASCII 32px, 中文 24px */
static uint16_t draw_val(uint16_t x,uint16_t y,const char *s,uint16_t fc,uint16_t bc) {
    while(*s){if((uint8_t)*s>=0x80){draw_zh24(x,y+4,s,fc,bc);x+=24;s+=3;}else{lcd_show_char(x,y,*s,fc,bc,32,0);x+=16;s++;}}
    return x;
}

uint16_t ui_show_zh16(uint16_t x,uint16_t y,const char *z,uint16_t fc,uint16_t bc){draw_zh16(x,y,z,fc,bc);return x+16;}
uint16_t ui_show_zh16_str(uint16_t x,uint16_t y,const char *s,uint16_t fc,uint16_t bc){return mix16(x,y,s,fc,bc);}
uint16_t ui_show_zh24(uint16_t x,uint16_t y,const char *z,uint16_t fc,uint16_t bc){draw_zh24(x,y,z,fc,bc);return x+24;}
uint16_t ui_show_zh24_str(uint16_t x,uint16_t y,const char *s,uint16_t fc,uint16_t bc){return mix24(x,y,s,fc,bc);}

/* ============================================================
 * 卡片布局常量 (240x320)
 * ============================================================ */
#define CW       114          /* 卡片宽 */
#define CLX        4          /* 左列 x */
#define CRX      122          /* 右列 x */

#define BIG_Y     44          /* 大行 y */
#define BIG_H     88          /* 大卡片高 */
#define MED_Y1   138          /* 中行1 y */
#define MED_Y2   230          /* 中行2 y */
#define MED_H     82          /* 中卡片高 */

/* 大卡片内部 */
#define BL_X      32          /* 标签 x 偏移 (图标 20px + 4px 间距) */
#define BL_Y      12          /* 标签 y 偏移 */
#define BV_X       8          /* 值 x 偏移 */
#define BV_Y      44          /* 值 y 偏移 */

/* 中卡片内部 */
#define MD_X       8          /* 状态点 x */
#define MD_Y      10          /* 状态点 y */
#define MI_X      18          /* 图标 x */
#define MI_Y       5          /* 图标 y */
#define ML_X      42          /* 标签 x (图标后) */
#define ML_Y       5          /* 标签 y */
#define MV_X       8          /* 值 x */
#define MV_Y      38          /* 值 y */

/* ============================================================
 * 状态缓存
 * ============================================================ */
static int   s_page = 1;
static int   s_wifi = 0;
static int   s_cloud = 0;
static float s_temp = 0;    static int s_temp_ok = 0;
static float s_humi = 0;    static int s_humi_ok = 0;
static int   s_pir = 0;
static int   s_gas = 0;
static int   s_spo2 = 0;    static int s_spo2_ok = 0;
static int   s_hr = 0;      static int s_hr_ok = 0;
static int   s_sos = 0;
static int   s_door = 0;
static int   s_sle = 0;
static int   s_fall = 0;    static int s_fall_ok = 0;
static float s_pitch = 0, s_roll = 0;  static int s_pose_ok = 0;
static int   s_flame = 0;

/* ============================================================
 * 绘图辅助
 * ============================================================ */

/* 卡片底: 边框 + 白色内胆 */
static void card_bg(int cx,int cy,int ch){
    lcd_fill(cx,cy,cx+CW,cy+ch,UI_CARD_BORDER);
    lcd_fill(cx+1,cy+1,cx+CW-1,cy+ch-1,UI_CARD_BG);
}

/* 状态圆点 (6x6) */
static void dot6(int cx,int cy,uint16_t clr){
    lcd_fill(cx+1,cy,cx+5,cy+6,clr);
    lcd_fill(cx,cy+1,cx+6,cy+5,clr);
}

/* ============================================================
 * 小图标 (20x20 像素插图)
 * ============================================================ */
typedef enum {
    ICON_NONE = 0,
    ICON_THERMO,    /* 温度计 */
    ICON_DROP,      /* 水滴 */
    ICON_PERSON,    /* 小人上半身 */
    ICON_CLOUD,     /* 云朵 */
    ICON_DOOR,      /* 小门 */
    ICON_FLAME,     /* 火苗 */
    ICON_HEART,     /* 爱心 */
} icon_type_t;

/* 填充圆: 逐行扫描线 */
static void fill_circle(int cx,int cy,int r,uint16_t c){
    for(int dy=-r;dy<=r;dy++){
        int dx2=r*r-dy*dy, dx=0;
        while((dx+1)*(dx+1)<=dx2) dx++;
        lcd_fill((uint16_t)(cx-dx),(uint16_t)(cy+dy),
                 (uint16_t)(cx+dx),(uint16_t)(cy+dy+1),c);
    }
}

/* 温度计: 竖管 + 圆球 */
static void icon_thermo(int x,int y,uint16_t c){
    lcd_fill(x+8,y+1,x+12,y+13,c);          /* 管身 */
    lcd_fill(x+9,y+0,x+11,y+2,c);           /* 管头 */
    fill_circle(x+10,y+16,5,c);             /* 球 */
}

/* 水滴: 上尖下圆 */
static void icon_drop(int x,int y,uint16_t c){
    lcd_fill(x+9,y+1,x+11,y+5,c);           /* 尖 */
    lcd_fill(x+8,y+4,x+12,y+7,c);           /* 上 */
    lcd_fill(x+6,y+6,x+14,y+10,c);          /* 中 */
    fill_circle(x+10,y+14,5,c);             /* 底圆 */
}

/* 小人上半身: 头 + 肩膀 */
static void icon_person(int x,int y,uint16_t c){
    fill_circle(x+10,y+5,4,c);              /* 头 */
    lcd_fill(x+4,y+11,x+16,y+13,c);         /* 肩 */
    lcd_fill(x+6,y+12,x+14,y+19,c);         /* 身 */
}

/* 云朵: 三圆叠加 + 底 */
static void icon_cloud(int x,int y,uint16_t c){
    fill_circle(x+7,y+11,4,c);              /* 左 */
    fill_circle(x+13,y+11,4,c);             /* 右 */
    fill_circle(x+10,y+8,5,c);              /* 上 */
    lcd_fill(x+3,y+11,x+17,y+16,c);         /* 底 */
}

/* 小门: 门框 + 把手 */
static void icon_door(int x,int y,uint16_t c){
    lcd_fill(x+5,y+2,x+15,y+4,c);           /* 顶框 */
    lcd_fill(x+5,y+2,x+7,y+19,c);           /* 左框 */
    lcd_fill(x+13,y+2,x+15,y+19,c);         /* 右框 */
    lcd_fill(x+5,y+17,x+15,y+19,c);         /* 底框 */
    fill_circle(x+11,y+11,1,c);             /* 把手 */
}

/* 火苗: 上尖下宽 */
static void icon_flame(int x,int y,uint16_t c){
    lcd_fill(x+9,y+0,x+11,y+3,c);
    lcd_fill(x+8,y+2,x+12,y+6,c);
    lcd_fill(x+6,y+5,x+14,y+10,c);
    lcd_fill(x+4,y+8,x+16,y+15,c);
    lcd_fill(x+6,y+14,x+14,y+18,c);
    lcd_fill(x+8,y+17,x+12,y+20,c);
}

/* 爱心: 双圆 + 尖底 */
static void icon_heart(int x,int y,uint16_t c){
    fill_circle(x+7,y+7,4,c);               /* 左圆 */
    fill_circle(x+13,y+7,4,c);              /* 右圆 */
    lcd_fill(x+3,y+8,x+17,y+10,c);          /* 中带 */
    for(int i=0;i<7;i++)
        lcd_fill((uint16_t)(x+3+i*2),(uint16_t)(y+10+i),
                 (uint16_t)(x+17-i*2),(uint16_t)(y+11+i),c);
}

/* 图标调度 */
static void draw_icon(int x,int y,icon_type_t t,uint16_t c){
    switch(t){
    case ICON_THERMO: icon_thermo(x,y,c); break;
    case ICON_DROP:   icon_drop(x,y,c);   break;
    case ICON_PERSON: icon_person(x,y,c); break;
    case ICON_CLOUD:  icon_cloud(x,y,c);  break;
    case ICON_DOOR:   icon_door(x,y,c);   break;
    case ICON_FLAME:  icon_flame(x,y,c);  break;
    case ICON_HEART:  icon_heart(x,y,c);  break;
    default: break;
    }
}

/* ---- 大卡片: 一次性画完 (含图标) ---- */
static void draw_big(int cx,int cy,icon_type_t ico,uint16_t ico_clr,
                     const char *lbl,const char *val,uint16_t val_clr){
    card_bg(cx,cy,BIG_H);
    draw_icon(cx+8,cy+10,ico,ico_clr);
    mix24(cx+BL_X,cy+BL_Y,lbl,UI_LABEL_FG,UI_CARD_BG);
    draw_val(cx+BV_X,cy+BV_Y,val,val_clr,UI_CARD_BG);
}

/* ---- 大卡片: 只更新值 ---- */
static void upd_big(int cx,int cy,const char *val,uint16_t val_clr){
    lcd_fill(cx+4,cy+BV_Y-2,cx+CW-2,cy+BIG_H-2,UI_CARD_BG);
    draw_val(cx+BV_X,cy+BV_Y,val,val_clr,UI_CARD_BG);
}

/* ---- 中卡片: 一次性画完 (含图标) ---- */
static void draw_med(int cx,int cy,uint16_t dot_clr,icon_type_t ico,uint16_t ico_clr,
                     const char *lbl,const char *val,uint16_t val_clr){
    card_bg(cx,cy,MED_H);
    dot6(cx+MD_X,cy+MD_Y,dot_clr);
    draw_icon(cx+MI_X,cy+MI_Y,ico,ico_clr);
    mix24(cx+ML_X,cy+ML_Y,lbl,UI_LABEL_FG,UI_CARD_BG);
    draw_val(cx+MV_X,cy+MV_Y,val,val_clr,UI_CARD_BG);
}

/* ---- 中卡片: 更新状态点 + 值 ---- */
static void upd_med_dot(int cx,int cy,uint16_t dot_clr,
                        const char *val,uint16_t val_clr){
    dot6(cx+MD_X,cy+MD_Y,dot_clr);
    lcd_fill(cx+4,cy+MV_Y-2,cx+CW-2,cy+MED_H-2,UI_CARD_BG);
    draw_val(cx+MV_X,cy+MV_Y,val,val_clr,UI_CARD_BG);
}

/* ============================================================
 * 标题栏
 * ============================================================ */
/* WiFi 信号图标: 3 根递增横条 */
static void draw_wifi_icon(uint16_t x,uint16_t y,uint16_t clr){
    lcd_fill(x,y+12,x+5,y+15,clr);      /* 短 */
    lcd_fill(x,y+7,x+8,y+10,clr);       /* 中 */
    lcd_fill(x,y+2,x+11,y+5,clr);       /* 长 */
}
/* 云朵图标: 3 层叠加 */
static void draw_cloud_icon(uint16_t x,uint16_t y,uint16_t clr){
    lcd_fill(x+1,y+8,x+19,y+14,clr);    /* 底 */
    lcd_fill(x+4,y+4,x+16,y+10,clr);    /* 中 */
    lcd_fill(x+8,y+1,x+13,y+5,clr);     /* 顶 */
}

static void draw_title(void){
    lcd_fill(0,0,LCD_W,38,UI_TITLE_BG);
    mix24(6,7,"守望.星护居",UI_TITLE_FG,UI_TITLE_BG);
    /* WiFi 图标 */
    draw_wifi_icon(150,11,s_wifi?UI_DOT_GOOD:UI_DOT_NA);
    /* 云图标 */
    draw_cloud_icon(175,11,s_cloud?UI_DOT_GOOD:UI_DOT_NA);
    /* 页码 */
    char pg[8]; snprintf(pg,sizeof(pg),"[%d/2]",s_page);
    mix16(200,11,pg,UI_TITLE_FG,UI_TITLE_BG);
}

/* 只更新标题栏动态部分 (WiFi/云/页码) */
static void upd_title_status(void){
    draw_wifi_icon(150,11,s_wifi?UI_DOT_GOOD:UI_DOT_NA);
    draw_cloud_icon(175,11,s_cloud?UI_DOT_GOOD:UI_DOT_NA);
    lcd_fill(198,8,240,30,UI_TITLE_BG);
    char pg[8]; snprintf(pg,sizeof(pg),"[%d/2]",s_page);
    mix16(200,11,pg,UI_TITLE_FG,UI_TITLE_BG);
}

/* ============================================================
 * 页面绘制 — 每张卡片一次性画完, 不会出现"先出线后出字"
 * ============================================================ */
static void draw_page1(void){
    lcd_fill(0,38,LCD_W,LCD_H,UI_BG);

    /* 行1: 温度 | 湿度 (大卡片) */
    if(s_temp_ok){char b[16];snprintf(b,sizeof(b),"%.1f",s_temp);
        draw_big(CLX,BIG_Y,ICON_THERMO,UI_ICON_TEMP,"温度",b,UI_VALUE_OK);}
    else draw_big(CLX,BIG_Y,ICON_THERMO,UI_ICON_TEMP,"温度","--",UI_VALUE_NA);

    if(s_humi_ok){char b[16];snprintf(b,sizeof(b),"%.0f",s_humi);
        draw_big(CRX,BIG_Y,ICON_DROP,UI_ICON_HUMI,"湿度",b,UI_VALUE_OK);}
    else draw_big(CRX,BIG_Y,ICON_DROP,UI_ICON_HUMI,"湿度","--",UI_VALUE_NA);

    /* 行2: 人体 | 气体 (中卡片) */
    draw_med(CLX,MED_Y1,s_pir?UI_DOT_WARN:UI_DOT_GOOD,ICON_PERSON,UI_ICON_PIR,
             "人体",s_pir?"有人":"无人",s_pir?UI_VALUE_WARN:UI_VALUE_GOOD);
    draw_med(CRX,MED_Y1,s_gas?UI_DOT_WARN:UI_DOT_GOOD,ICON_CLOUD,UI_ICON_GAS,
             "气体",s_gas?"报警":"正常",s_gas?UI_VALUE_WARN:UI_VALUE_GOOD);

    /* 行3: 门磁 | 火情 (中卡片) */
    draw_med(CLX,MED_Y2,s_door?UI_DOT_WARN:UI_DOT_GOOD,ICON_DOOR,UI_ICON_DOOR,
             "门磁",s_door?"开启":"关闭",s_door?UI_VALUE_WARN:UI_VALUE_GOOD);
    draw_med(CRX,MED_Y2,s_flame?UI_DOT_WARN:UI_DOT_GOOD,ICON_FLAME,UI_ICON_FLAME,
             "火情",s_flame?"告警":"正常",s_flame?UI_VALUE_WARN:UI_VALUE_GOOD);
}

static void draw_page2(void){
    lcd_fill(0,38,LCD_W,LCD_H,UI_BG);

    /* 行1: 血氧 | HR (大卡片) */
    if(s_spo2_ok&&s_spo2>0){char b[16];snprintf(b,sizeof(b),"%d%%",s_spo2);
        draw_big(CLX,BIG_Y,ICON_DROP,UI_ICON_SPO2,"血氧",b,s_spo2<90?UI_VALUE_WARN:UI_VALUE_GOOD);}
    else draw_big(CLX,BIG_Y,ICON_DROP,UI_ICON_SPO2,"血氧","--",UI_VALUE_NA);

    if(s_hr_ok&&s_hr>0){char b[16];snprintf(b,sizeof(b),"%d",s_hr);
        draw_big(CRX,BIG_Y,ICON_HEART,UI_ICON_HR,"HR",b,UI_VALUE_OK);}
    else draw_big(CRX,BIG_Y,ICON_HEART,UI_ICON_HR,"HR","--",UI_VALUE_NA);

    /* 行2: SOS | 摔倒 (中卡片) */
    draw_med(CLX,MED_Y1,s_sos?UI_DOT_WARN:UI_DOT_NA,ICON_NONE,0,
             "SOS",s_sos?"SOS!":"--",s_sos?UI_VALUE_WARN:UI_VALUE_NA);
    if(s_fall_ok){char b[16];snprintf(b,sizeof(b),"%d",s_fall);
        draw_med(CRX,MED_Y1,s_fall?UI_DOT_WARN:UI_DOT_GOOD,ICON_NONE,0,
                 "摔倒",b,s_fall?UI_VALUE_WARN:UI_VALUE_GOOD);}
    else draw_med(CRX,MED_Y1,UI_DOT_NA,ICON_NONE,0,"摔倒","--",UI_VALUE_NA);

    /* 行3: POSE | 报警 (中卡片) */
    if(s_pose_ok){char b[24];snprintf(b,sizeof(b),"%+.0f/%+.0f",s_pitch,s_roll);
        draw_med(CLX,MED_Y2,UI_DOT_GOOD,ICON_NONE,0,"POSE",b,UI_VALUE_OK);}
    else draw_med(CLX,MED_Y2,UI_DOT_NA,ICON_NONE,0,"POSE","--",UI_VALUE_NA);

    draw_med(CRX,MED_Y2,s_sle?UI_DOT_WARN:UI_DOT_GOOD,ICON_NONE,0,
             "报警",s_sle?"告警":"正常",s_sle?UI_VALUE_WARN:UI_VALUE_GOOD);
}

/* 整页重绘: 标题 + 当前页 */
static void redraw_page(void){
    draw_title();
    if(s_page==1) draw_page1();
    else draw_page2();
}

/* 翻页: 标题栏状态 + 当前页 */
static void switch_page(void){
    upd_title_status();
    if(s_page==1) draw_page1();
    else draw_page2();
}

/* ============================================================
 * 公开接口
 * ============================================================ */
void ui_init(void){
    s_page = 1;
    lcd_fill(0,0,LCD_W,LCD_H,UI_BG);
    redraw_page();
}

void ui_set_page(int page){
    if(page<1||page>2||page==s_page) return;
    s_page = page;
    switch_page();
}
int ui_get_page(void){ return s_page; }

void ui_set_wifi(int connected){
    if(s_wifi==connected) return;
    s_wifi = connected;
    draw_wifi_icon(150,11,s_wifi?UI_DOT_GOOD:UI_DOT_NA);
}
void ui_set_cloud(int connected){
    if(s_cloud==connected) return;
    s_cloud = connected;
    draw_cloud_icon(175,11,s_cloud?UI_DOT_GOOD:UI_DOT_NA);
}

/* ---- 环境页更新 (值不变则跳过, 防闪烁) ---- */
void ui_update_temp(float t,int ok){
    if(s_temp==t&&s_temp_ok==ok) return;
    s_temp=t; s_temp_ok=ok;
    if(s_page!=1) return;
    if(ok){char b[16];snprintf(b,sizeof(b),"%.1f",t);upd_big(CLX,BIG_Y,b,UI_VALUE_OK);}
    else upd_big(CLX,BIG_Y,"--",UI_VALUE_NA);
}
void ui_update_humi(float h,int ok){
    if(s_humi==h&&s_humi_ok==ok) return;
    s_humi=h; s_humi_ok=ok;
    if(s_page!=1) return;
    if(ok){char b[16];snprintf(b,sizeof(b),"%.0f",h);upd_big(CRX,BIG_Y,b,UI_VALUE_OK);}
    else upd_big(CRX,BIG_Y,"--",UI_VALUE_NA);
}
void ui_update_pir(int d){
    if(s_pir==d) return;
    s_pir=d;
    if(s_page!=1) return;
    upd_med_dot(CLX,MED_Y1,d?UI_DOT_WARN:UI_DOT_GOOD,d?"有人":"无人",d?UI_VALUE_WARN:UI_VALUE_GOOD);
}
void ui_update_gas(int a){
    if(s_gas==a) return;
    s_gas=a;
    if(s_page!=1) return;
    upd_med_dot(CRX,MED_Y1,a?UI_DOT_WARN:UI_DOT_GOOD,a?"报警":"正常",a?UI_VALUE_WARN:UI_VALUE_GOOD);
}
void ui_update_door(int open){
    if(s_door==open) return;
    s_door=open;
    if(s_page!=1) return;
    upd_med_dot(CLX,MED_Y2,open?UI_DOT_WARN:UI_DOT_GOOD,open?"开启":"关闭",open?UI_VALUE_WARN:UI_VALUE_GOOD);
}
void ui_update_flame(int fire){
    if(s_flame==fire) return;
    s_flame=fire;
    if(s_page!=1) return;
    upd_med_dot(CRX,MED_Y2,fire?UI_DOT_WARN:UI_DOT_GOOD,fire?"告警":"正常",fire?UI_VALUE_WARN:UI_VALUE_GOOD);
}

/* ---- 健康页更新 ---- */
void ui_update_spo2(int s,int ok){
    if(s_spo2==s&&s_spo2_ok==ok) return;
    s_spo2=s; s_spo2_ok=ok;
    if(s_page!=2) return;
    if(ok&&s>0){char b[16];snprintf(b,sizeof(b),"%d%%",s);upd_big(CLX,BIG_Y,b,s<90?UI_VALUE_WARN:UI_VALUE_GOOD);}
    else upd_big(CLX,BIG_Y,"--",UI_VALUE_NA);
}
void ui_update_heartrate(int hr,int ok){
    if(s_hr==hr&&s_hr_ok==ok) return;
    s_hr=hr; s_hr_ok=ok;
    if(s_page!=2) return;
    if(ok&&hr>0){char b[16];snprintf(b,sizeof(b),"%d",hr);upd_big(CRX,BIG_Y,b,UI_VALUE_OK);}
    else upd_big(CRX,BIG_Y,"--",UI_VALUE_NA);
}
void ui_update_sos(int a){
    if(s_sos==a) return;
    s_sos=a;
    if(s_page!=2) return;
    upd_med_dot(CLX,MED_Y1,a?UI_DOT_WARN:UI_DOT_NA,a?"SOS!":"--",a?UI_VALUE_WARN:UI_VALUE_NA);
}
void ui_update_fall(int f,int ok){
    if(s_fall==f&&s_fall_ok==ok) return;
    s_fall=f; s_fall_ok=ok;
    if(s_page!=2) return;
    if(ok){char b[16];snprintf(b,sizeof(b),"%d",f);upd_med_dot(CRX,MED_Y1,f?UI_DOT_WARN:UI_DOT_GOOD,b,f?UI_VALUE_WARN:UI_VALUE_GOOD);}
    else upd_med_dot(CRX,MED_Y1,UI_DOT_NA,"--",UI_VALUE_NA);
}
void ui_update_posture(float pitch,float roll,int ok){
    if(s_pitch==pitch&&s_roll==roll&&s_pose_ok==ok) return;
    s_pitch=pitch; s_roll=roll; s_pose_ok=ok;
    if(s_page!=2) return;
    if(ok){char b[24];snprintf(b,sizeof(b),"%+.0f/%+.0f",pitch,roll);upd_med_dot(CLX,MED_Y2,UI_DOT_GOOD,b,UI_VALUE_OK);}
    else upd_med_dot(CLX,MED_Y2,UI_DOT_NA,"--",UI_VALUE_NA);
}
void ui_update_sle(int ok){
    if(s_sle==ok) return;
    s_sle=ok;
    if(s_page!=2) return;
    upd_med_dot(CRX,MED_Y2,ok?UI_DOT_WARN:UI_DOT_GOOD,ok?"告警":"正常",ok?UI_VALUE_WARN:UI_VALUE_GOOD);
}
