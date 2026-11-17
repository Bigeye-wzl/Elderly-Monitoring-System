#ifndef SENSORS_H
#define SENSORS_H

/* PIR 人体感应 */
void pir_init(void);
int  pir_read(void);            /* 1=有人 0=无人 */

/* MQ2 可燃气体 */
void  gas_init(void);
int   gas_read_raw(void);       /* ADC 原始值 0~1023，-1=读失败 */
int   gas_is_alarm(int raw);    /* 1=告警 0=正常 */
float gas_raw_to_voltage(int raw);

/* SHT30 温湿度 */
void sht30_init(void);
int  sht30_read(float *temp, float *humi);  /* 返回 0x03=都成功 */

/* 门磁传感器（GPIO0_PB3） */
void door_init(void);
int  door_read(void);           /* 1=门开 0=门关 */

/* 火焰传感器（GPIO0_PC4，DO 数字量） */
void flame_init(void);
int  flame_read(void);          /* 1=有火 0=正常 */

#endif /* SENSORS_H */
