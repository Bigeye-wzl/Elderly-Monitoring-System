/*
 * 声光报警模块 — 蜂鸣器 + RGB LED
 *
 * 通晓 TX-SMART-R 板载:
 *   蜂鸣器: GPIO0_PC5 = EPWMDEV_PWM5_M0 (无源, PWM 3KHz 50%)
 *   RGB LED: R=PB5  G=PD0  B=PB4
 * ⚠ PC5 不要当 GPIO 用，只能走 PWM5
 *
 * 声光联动:
 *   无报警   → 绿灯常亮
 *   SOS      → 红灯快闪 200ms + 蜂鸣器急促
 *   FALL     → 红灯中闪 500ms + 蜂鸣器较慢
 *
 * 报警源:
 *   ALERT_SRC_SOS  — SOS 按键 (急促滴滴声, 200ms 节拍)
 *   ALERT_SRC_FALL — 摔倒检测 (较慢滴滴声, 500ms 节拍)
 */
#ifndef ALERT_H
#define ALERT_H

typedef enum {
    ALERT_SRC_SOS = 0,
    ALERT_SRC_FALL,
    ALERT_SRC_FIRE,
    ALERT_SRC_NUM
} alert_src_t;

/*
 * 初始化蜂鸣器 PWM + 启动报警节拍任务
 * 返回 0=成功, -1=失败
 */
int alert_init(void);

/*
 * 设置/清除一个报警源。active=1 开始响, 0 停止
 * 多个源可同时置位，SOS 节拍优先
 */
void alert_set(alert_src_t src, int active);

/* 任一报警源激活返回 1，全静默返回 0 */
int alert_is_active(void);

/* 立即停止所有报警 */
void alert_stop_all(void);

#endif /* ALERT_H */
