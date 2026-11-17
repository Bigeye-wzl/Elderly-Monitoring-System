/*
 * 星闪网关 UART 接收模块
 *
 * 从星闪网关接收 JSON 数据并解析
 * 支持格式:
 *   [MPU]{"ax":...,"ay":...,...,"fall":1}
 *   [SPO2]{"spo2":98,"heart_rate":72,"sos":0}
 *
 * 硬件: EUART0_M0 (RX=PB6, TX=PB7), 115200 8N1
 */
#include "iot_uart.h"
#include "iot_errno.h"
#include "lz_hardware.h"
#include "mpu_uart.h"
#include "los_task.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* UART 配置 */
#define MPU_UART_ID         EUART0_M0   /* PB6=RX, PB7=TX */
#define MPU_UART_BAUD       115200
#define UART0_BASE          ((volatile uint32_t *)0x40070000U)

/* libhardware.a 预编译符号, 头文件里没有声明 */
extern void HAL_UART_EnableLoopback(uint32_t *uart_base);
extern void HAL_UART_DisableLoopback(uint32_t *uart_base);

/* 接收缓冲 */
#define RX_BUF_SIZE         256
#define JSON_MAX_LEN        256     /* MPU JSON 极值 ~175B, 160 会截断丢帧 */

static uint8_t s_rx_buf[RX_BUF_SIZE];
static uint32_t s_rx_len = 0;
static mpu_data_t s_last_mpu = {0};
static spo2_data_t s_last_spo2 = {0};

/* 诊断计数器: 区分"没收到字节" vs "收到但解析失败" */
static uint32_t s_diag_bytes = 0;       /* 累计收到字节数 */
static uint32_t s_diag_mpu = 0;         /* 解析成功的 MPU 帧数 */
static uint32_t s_diag_spo2 = 0;        /* 解析成功的 SPO2 帧数 */
static uint32_t s_diag_parse_fail = 0;  /* 解析失败的 JSON 数 */
static uint32_t s_diag_tick = 0;        /* 调用次数, 用于定时打印 */

void mpu_uart_get_stats(uint32_t *bytes, uint32_t *mpu, uint32_t *spo2, uint32_t *fail)
{
    if (bytes) *bytes = s_diag_bytes;
    if (mpu)   *mpu   = s_diag_mpu;
    if (spo2)  *spo2  = s_diag_spo2;
    if (fail)  *fail  = s_diag_parse_fail;
}

/* ============================================================
 * JSON 简易解析器 (不依赖外部库)
 * ============================================================ */

static int json_get_int(const char *json, const char *key, int *value)
{
    char pattern[32];
    const char *p;
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    p = strstr(json, pattern);
    if (!p) return -1;
    p += strlen(pattern);
    *value = atoi(p);
    return 0;
}

static int json_get_float(const char *json, const char *key, float *value)
{
    char pattern[32];
    const char *p;
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    p = strstr(json, pattern);
    if (!p) return -1;
    p += strlen(pattern);
    *value = (float)atof(p);
    return 0;
}

/* 解析 MPU JSON */
static int parse_mpu_json(const char *json, mpu_data_t *data)
{
    int val;
    float fval;

    if (strstr(json, "\"ax\"") == NULL) return -1;

    if (json_get_int(json, "ax", &val) == 0) data->ax = (int16_t)val;
    if (json_get_int(json, "ay", &val) == 0) data->ay = (int16_t)val;
    if (json_get_int(json, "az", &val) == 0) data->az = (int16_t)val;
    if (json_get_int(json, "gx", &val) == 0) data->gx = (int16_t)val;
    if (json_get_int(json, "gy", &val) == 0) data->gy = (int16_t)val;
    if (json_get_int(json, "gz", &val) == 0) data->gz = (int16_t)val;
    if (json_get_float(json, "pitch", &fval) == 0) data->pitch = fval;
    if (json_get_float(json, "roll", &fval) == 0) data->roll = fval;
    if (json_get_int(json, "fall", &val) == 0) data->fall = val;

    data->valid = 1;
    return 0;
}

/* 解析 SPO2 JSON */
static int parse_spo2_json(const char *json, spo2_data_t *data)
{
    int val;

    if (strstr(json, "\"spo2\"") == NULL) return -1;

    if (json_get_int(json, "spo2", &val) == 0) data->spo2 = val;
    if (json_get_int(json, "heart_rate", &val) == 0) data->heart_rate = val;
    if (json_get_int(json, "sos", &val) == 0) data->sos = val;

    data->valid = 1;
    return 0;
}

/* ============================================================
 * UART 初始化
 * ============================================================ */

int mpu_uart_init(void)
{
    /*
     * 直接用 LzUartInit 绕过 IoTUartInit。
     *
     * 原因: IoTUartInit 对 UART0 有特殊路径:
     *   LzUartDeinit(0) + HAL_UART_DeInit(0x40070000) + LzUartInit(0)
     * HAL_UART_DeInit 做寄存器级硬件反初始化(可能关时钟/禁 RX)，
     * LzUartInit 调 HAL_UART_Init 但未必完全恢复 → UART0 RX 只收到噪声。
     * 控制台用 UART1 从不走这条路，所以控制台正常。
     *
     * PinctrlSet + LzUartInit 等价于 IoTUartInit 的有效部分，
     * 唯一区别是跳过了 HAL_UART_DeInit。
     */
    UartAttribute attr = {0};
    unsigned int ret;

    PinctrlSet(GPIO0_PB6, MUX_FUNC2, PULL_UP, DRIVE_KEEP);
    PinctrlSet(GPIO0_PB7, MUX_FUNC2, PULL_UP, DRIVE_KEEP);
    printf("[UART] PB6/PB7 pinmux -> UART (PULL_UP)\n");

    attr.baudRate = MPU_UART_BAUD;
    attr.dataBits = UART_DATA_BIT_8;
    attr.stopBits = UART_STOP_BIT_1;
    attr.parity   = UART_PARITY_NONE;
    attr.rxBlock  = UART_BLOCK_STATE_NONE_BLOCK;
    attr.txBlock  = UART_BLOCK_STATE_NONE_BLOCK;

    /* 先 Deinit 清残留状态，再 Init — 但不碰 HAL_UART_DeInit */
    LzUartDeinit(0);
    ret = LzUartInit(0, &attr);
    if (ret != 0) {
        printf("[UART] LzUartInit fail ret:%u\n", ret);
        return -1;
    }

    /* 读 USR 寄存器诊断: bit3=RX FIFO Not Empty, bit0=Busy */
    {
        volatile uint32_t *usr = (volatile uint32_t *)0x4007007CU;
        uint32_t v = *usr;
        printf("[UART] USR=0x%08X (rx_not_empty=%d busy=%d)\n",
               v, (v >> 3) & 1, v & 1);
    }

    printf("[UART] LzUartInit(0) ok (PB6=RX, PB7=TX, %d baud) — bypassed HAL_UART_DeInit\n",
           MPU_UART_BAUD);

    /* --- 内部回环自检: TX→RX 内部短路, 不需要外部接线 --- */
    {
        uint8_t tx[] = "LOOP";
        uint8_t rx[16] = {0};
        unsigned int w, r;
        volatile uint32_t *usr;

        HAL_UART_EnableLoopback((uint32_t *)0x40070000U);
        w = LzUartWrite(0, tx, 4);
        LOS_Msleep(5);
        r = LzUartRead(0, rx, sizeof(rx));
        HAL_UART_DisableLoopback((uint32_t *)0x40070000U);

        usr = (volatile uint32_t *)0x4007007CU;
        printf("[UART] LOOPBACK write=%u read=%u data='%.*s' USR=0x%08X\n",
               w, r, (int)r, rx, *usr);

        if (r == 4 && rx[0]=='L' && rx[1]=='O' && rx[2]=='O' && rx[3]=='P') {
            printf("[UART] *** LOOPBACK PASS *** UART0 driver OK, problem is EXTERNAL (wire/pin)\n");
        } else {
            printf("[UART] *** LOOPBACK FAIL *** UART0 driver broken, problem is INTERNAL\n");
        }
    }

    /* --- 外部回环自检: 用杜邦线把 PB7(TX) 短接到 PB6(RX) --- */
    {
        uint8_t tx[] = "EXT1";
        uint8_t rx[16] = {0};
        unsigned int w, r;
        volatile uint32_t *usr;

        printf("[UART] EXTERNAL LOOPBACK: connect PB7(TX) to PB6(RX) with a jumper wire NOW!\n");
        printf("[UART] waiting 5s for you to connect the wire...\n");
        LOS_Msleep(5000);

        w = LzUartWrite(0, tx, 4);
        LOS_Msleep(10);
        r = LzUartRead(0, rx, sizeof(rx));

        usr = (volatile uint32_t *)0x4007007CU;
        printf("[UART] EXT LOOPBACK write=%u read=%u data='%.*s' USR=0x%08X\n",
               w, r, (int)r, rx, *usr);

        if (r == 4 && rx[0]=='E' && rx[1]=='X' && rx[2]=='T' && rx[3]=='1') {
            printf("[UART] *** EXT LOOPBACK PASS *** PB6/PB7 physical pins OK, wire OK!\n");
            printf("[UART] >>> Problem is GATEWAY side: GP8 not reaching PB6 <<<\n");
        } else if (r > 0) {
            printf("[UART] *** EXT LOOPBACK PARTIAL *** got %u bytes but wrong data\n", r);
            printf("[UART] >>> Wire connects WRONG pins! Check PB6/PB7 location <<<\n");
        } else {
            printf("[UART] *** EXT LOOPBACK FAIL *** no data at all\n");
            printf("[UART] >>> Either wire not connected, or PB6/PB7 are the wrong pins <<<\n");
        }
    }

    /* --- 波特率自动扫描: 找出网关实际波特率 ---
     * 正确波特率下应收到 ASCII: 5B='[' (开头 [MPU]/[SPO2])
     * 错误波特率下全是乱码 */
    {
        int baud_list[] = {2400, 4800, 9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600};
        int num_rates = sizeof(baud_list) / sizeof(baud_list[0]);
        int best_baud = MPU_UART_BAUD;
        int best_score = -1;

        printf("[UART] === BAUD SCAN: looking for 0x5B='[' ===\n");
        printf("[UART] (gateway must be sending data now!)\n");

        for (int b = 0; b < num_rates; b++) {
            UartAttribute a = {0};
            a.baudRate = baud_list[b];
            a.dataBits = UART_DATA_BIT_8;
            a.stopBits = UART_STOP_BIT_1;
            a.parity   = UART_PARITY_NONE;
            a.rxBlock  = UART_BLOCK_STATE_NONE_BLOCK;
            a.txBlock  = UART_BLOCK_STATE_NONE_BLOCK;

            LzUartDeinit(0);
            LzUartInit(0, &a);

            /* 收集 2 秒数据 */
            LOS_Msleep(2000);

            uint8_t buf[128] = {0};
            unsigned int total = 0;
            for (int t = 0; t < 8; t++) {
                unsigned int r = LzUartRead(0, buf + total, sizeof(buf) - total);
                if (r == 0) { LOS_Msleep(50); continue; }
                total += r;
                if (total >= sizeof(buf)) break;
            }

            /* 评分: 0x5B 出现次数 + 是否有连续可打印 ASCII */
            int score = 0;
            for (unsigned int j = 0; j < total; j++) {
                if (buf[j] == 0x5B) score += 3;       /* '[' 前缀 */
                if (buf[j] == '{') score += 2;         /* JSON 开始 */
                if (buf[j] == '"') score += 1;         /* JSON 引号 */
            }

            printf("[BAUD] %6d: recv=%3u score=%3d HEX:", baud_list[b], total, score);
            int show = total > 32 ? 32 : (int)total;
            for (int j = 0; j < show; j++) printf(" %02X", buf[j]);
            if (total > 32) printf(" ...");
            printf("\n");

            if (score > best_score) {
                best_score = score;
                best_baud = baud_list[b];
            }
        }

        /* 恢复到最佳波特率 */
        {
            UartAttribute a = {0};
            a.baudRate = (best_score > 0) ? best_baud : MPU_UART_BAUD;
            a.dataBits = UART_DATA_BIT_8;
            a.stopBits = UART_STOP_BIT_1;
            a.parity   = UART_PARITY_NONE;
            a.rxBlock  = UART_BLOCK_STATE_NONE_BLOCK;
            a.txBlock  = UART_BLOCK_STATE_NONE_BLOCK;
            LzUartDeinit(0);
            LzUartInit(0, &a);
            printf("[UART] *** BAUD SCAN DONE: best=%d score=%d ***\n",
                   a.baudRate, best_score);
            if (best_score <= 0) {
                printf("[UART] >>> No valid data found! Gateway may not be sending. <<<\n");
            } else if (a.baudRate != MPU_UART_BAUD) {
                printf("[UART] >>> MISMATCH! Gateway is at %d, not %d <<<\n",
                       a.baudRate, MPU_UART_BAUD);
            }
        }
    }

    return 0;
}

/* ============================================================
 * 数据读取与解析
 *
 * 返回值:
 *   0 = 解析到 MPU 数据
 *   1 = 解析到 SPO2 数据
 *  -1 = 无数据或解析失败
 * ============================================================ */

int mpu_uart_read(mpu_data_t *mpu, spo2_data_t *spo2)
{
    uint8_t tmp[256];  /* MPU JSON ~175B + 前缀 + \n, 128 会截断长帧 */
    int n, i, retry;
    static char line_buf[JSON_MAX_LEN];
    static uint32_t line_len = 0;
    static char prefix[8] = {0};  /* [MPU] or [SPO2] */
    static int prefix_len = 0;
    int batch_result = -1;  /* 本批次结果: 0=MPU, 1=SPO2, 2=两者 */

    /* 最多重试 10 次，每次等 5ms */
    for (retry = 0; retry < 10; retry++) {
        n = LzUartRead(0, tmp, sizeof(tmp));
        if (n <= 0) {
            /* 周期性寄存器诊断: 确认 UART0 硬件状态 */
            s_diag_tick++;
            if (s_diag_tick == 100 || s_diag_tick % 600 == 0) {
                volatile uint32_t *usr = (volatile uint32_t *)0x4007007CU;
                volatile uint32_t *lsr = (volatile uint32_t *)0x40070014U;
                volatile uint32_t *iir = (volatile uint32_t *)0x40070008U;
                printf("[UART] diag tick=%u USR=0x%08X LSR=0x%08X IIR=0x%08X\n",
                       s_diag_tick, *usr, *lsr, *iir);
            }
            if (batch_result >= 0) return batch_result;  /* 已有结果, 不再等 */
            LOS_Msleep(5);
            continue;
        }

        s_diag_bytes += (uint32_t)n;

        for (i = 0; i < n; i++) {
            char c = (char)tmp[i];

            if (c == '[') {
                /* 前缀开始 [MPU] or [SPO2] */
                prefix_len = 0;
                prefix[prefix_len++] = c;
            } else if (prefix_len > 0 && line_len == 0 && prefix_len < 7 && c != '{' && c != '}' && c != '\n' && c != '\r') {
                /* 累积前缀字符: [MPU] / [SPO2], line_len==0 表示 JSON 尚未开始 */
                prefix[prefix_len++] = c;
                prefix[prefix_len] = '\0';
            } else if (c == '{') {
                /* JSON 开始 */
                line_len = 0;
                line_buf[line_len++] = c;
            } else if (c == '}') {
                /* JSON 结束 */
                if (line_len < JSON_MAX_LEN - 1) {
                    line_buf[line_len++] = c;
                }
                line_buf[line_len] = '\0';

                int result = -1;
                if (line_len > 10) {
                    if (strcmp(prefix, "[MPU]") == 0 || prefix_len == 0) {
                        if (mpu != NULL && parse_mpu_json(line_buf, mpu) == 0) {
                            memcpy(&s_last_mpu, mpu, sizeof(mpu_data_t));
                            result = 0;
                        }
                    } else if (strcmp(prefix, "[SPO2]") == 0) {
                        if (spo2 != NULL && parse_spo2_json(line_buf, spo2) == 0) {
                            memcpy(&s_last_spo2, spo2, sizeof(spo2_data_t));
                            result = 1;
                        }
                    } else {
                        if (mpu != NULL && parse_mpu_json(line_buf, mpu) == 0) {
                            memcpy(&s_last_mpu, mpu, sizeof(mpu_data_t));
                            result = 0;
                        } else if (spo2 != NULL && parse_spo2_json(line_buf, spo2) == 0) {
                            memcpy(&s_last_spo2, spo2, sizeof(spo2_data_t));
                            result = 1;
                        }
                    }
                }

                if (result < 0 && line_len > 10) s_diag_parse_fail++;
                line_len = 0;
                prefix_len = 0;
                prefix[0] = '\0';

                if (result == 0) {
                    s_diag_mpu++;
                    batch_result = (batch_result == 1) ? 2 : 0;
                } else if (result == 1) {
                    s_diag_spo2++;
                    batch_result = (batch_result == 0) ? 2 : 1;
                }
                /* 继续处理缓冲区里的后续帧, 不提前返回 */
            } else if (c == '\n' || c == '\r') {
                continue;
            } else if (line_len < JSON_MAX_LEN - 1) {
                line_buf[line_len++] = c;
            } else {
                line_len = 0;
            }
        }

        /* 这批数据处理完了 */
        if (batch_result >= 0) return batch_result;
    }

    return -1;
}

int mpu_uart_get_last_mpu(mpu_data_t *data)
{
    if (data == NULL || !s_last_mpu.valid) return -1;
    memcpy(data, &s_last_mpu, sizeof(mpu_data_t));
    return 0;
}

int mpu_uart_get_last_spo2(spo2_data_t *data)
{
    if (data == NULL || !s_last_spo2.valid) return -1;
    memcpy(data, &s_last_spo2, sizeof(spo2_data_t));
    return 0;
}
