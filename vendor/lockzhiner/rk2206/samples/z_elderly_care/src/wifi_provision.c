/*
 * NFC WiFi Provisioning - AP + HTTP + DNS captive portal
 * + comprehensive serial diagnostics for bind/connect debugging
 *
 * DIAG TAG: [wifi_diag]  — all diagnostic lines use this tag + ms timestamp
 * Business logic unchanged; diagnostics added only.
 */
#include "wifi_provision.h"
#include "ohos_init.h"
#include "kv_store.h"
#include "config_network.h"
#include "reset.h"
#include "los_task.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <errno.h>

#define LOG_TAG "[wifi_prov]"
#define TX_LOG(fmt, ...)  printf(LOG_TAG " " fmt "\n", ##__VA_ARGS__)

/* ============================================================
 * Diagnostic macros
 * ============================================================ */
#define DIAG_TAG "[wifi_diag]"

/* millisecond timestamp — LiteOS-M tick is 1000 Hz */
static inline unsigned int diag_ms(void) { return LOS_TickCountGet(); }

#define DIAG_LOG(fmt, ...) \
    printf("[%u][%s] " fmt "\n", diag_ms(), DIAG_TAG, ##__VA_ARGS__)

/* rate-limited log: only fires once per `interval_ms` per call-site */
#define DIAG_RATE_LOG(last_var, interval_ms, fmt, ...) do { \
    unsigned int _now = diag_ms(); \
    if (_now - (last_var) >= (interval_ms)) { \
        (last_var) = _now; \
        DIAG_LOG(fmt, ##__VA_ARGS__); \
    } \
} while(0)

/* log a syscall result: name, fd, ret, errno */
#define DIAG_RET(name, fd, ret) \
    DIAG_LOG("%s(fd=%d) ret=%d errno=%d(%s)", name, fd, ret, \
             errno, (ret < 0) ? "ERR" : "OK")

/* ============================================================
 * POSIX socket API (musl libc) — use system structs/constants.
 *
 * musl sockaddr_in (netinet/in.h):
 *   struct sockaddr_in { sa_family_t sin_family; in_port_t sin_port;
 *                        struct in_addr sin_addr; uint8_t sin_zero[8]; };
 *   sin_family is 2 bytes at offset 0 — NO sin_len field.
 *
 * Our previous custom BSD-style struct (sin_len + sin_family as u8+u8)
 * caused complete field misalignment: bind() saw sin_family=0x0210
 * instead of AF_INET(2) and failed with errno=5.
 *
 * SO_REUSEADDR=2, SOL_SOCKET=1 (musl values, not lwIP's 0x0004/0xfff).
 * ============================================================ */
#include <sys/socket.h>
#include <netinet/in.h>

/* lwIP close for sockets (unistd close may not release lwIP netconn) */
extern int lwip_close(int s);

/* htons / ntohs wrappers for existing call-sites */
#define HTONS(x) htons(x)
#define NTOHS(x) ntohs(x)

/*
 * AP interface IP 192.168.2.10 as u32 in the same byte order as
 * WifiLinkedInfo.ipAddress (LE host = network order bytes C0.A8.02.0A).
 */
#define AP_IP       0x0A02A8C0  /* 192.168.2.10 */

/* KV storage keys */
#define KV_SSID "ssid"
#define KV_PWD  "password"

static volatile int s_wifi_connected = 0;

/* server state for periodic diagnostics */
static volatile int s_http_fd = -1, s_http_listening = 0;
static volatile int s_dns_fd  = -1, s_dns_listening  = 0;
static volatile int s_tcp_conn_count = 0;
static volatile unsigned int s_http_recv_ok = 0, s_http_recv_err = 0;
static volatile unsigned int s_dns_recv_ok  = 0, s_dns_recv_err  = 0;

/* ============================================================
 * Async provisioning state machine
 * IDLE -> RECEIVED -> CONNECTING -> GOT_IP / FAILED
 * ============================================================ */
typedef enum {
    PROV_IDLE = 0,
    PROV_RECEIVED,       /* config parsed & stored in RAM */
    PROV_CONNECTING,     /* STA connect in progress */
    PROV_GOT_IP,         /* STA connected, IP acquired */
    PROV_FAILED,         /* STA connect failed after retries */
} prov_state_t;

static volatile prov_state_t s_prov_state = PROV_IDLE;
static char s_prov_ssid[33] = {0};
static char s_prov_pwd[65]  = {0};
static volatile int s_prov_trigger = 0;         /* 1 = new config pending */
static volatile unsigned int s_prov_retry = 0;
static volatile int s_prov_disc_reason = 0;

/* connection task waits on this */
static volatile int s_sta_task_running = 0;

static const char *prov_state_str(prov_state_t s)
{
    switch (s) {
    case PROV_IDLE:      return "IDLE";
    case PROV_RECEIVED:  return "RECEIVED";
    case PROV_CONNECTING:return "CONNECTING";
    case PROV_GOT_IP:    return "GOT_IP";
    case PROV_FAILED:    return "FAILED";
    default:             return "?";
    }
}

/* ============================================================
 * Diagnostic helpers
 * ============================================================ */

/* dump sockaddr_in in human-readable + hex form */
static void diag_dump_sockaddr(const char *tag, const struct sockaddr_in *sa)
{
    unsigned int ip = sa->sin_addr.s_addr;
    const unsigned char *p = (const unsigned char *)sa;
    int i;
    DIAG_LOG("%s: sin_family=%u sin_port=%u(%u host) ip=%u.%u.%u.%u",
        tag, sa->sin_family,
        sa->sin_port, NTOHS(sa->sin_port),
        ip & 0xFF, (ip >> 8) & 0xFF, (ip >> 16) & 0xFF, (ip >> 24) & 0xFF);
    printf("[%u][%s] %s hex:", diag_ms(), DIAG_TAG, tag);
    for (i = 0; i < (int)sizeof(*sa); i++) printf(" %02X", p[i]);
    printf("\n");
}

/* log bind with full context — retry with backoff, try INADDR_ANY then specific IP */
static int diag_bind(const char *svc, int fd, struct sockaddr_in *addr, int proto_is_tcp)
{
    int ret;
    int reuse = 1;
    int attempt;

    /* SO_REUSEADDR — lwIP value is 0x0004 */
    ret = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    DIAG_LOG("%s: setsockopt(SO_REUSEADDR) ret=%d errno=%d", svc, ret, errno);

    diag_dump_sockaddr(svc, addr);
    DIAG_LOG("%s: bind fd=%d addr=%u.%u.%u.%u:%u proto=%s",
        svc, fd,
        addr->sin_addr.s_addr & 0xFF, (addr->sin_addr.s_addr >> 8) & 0xFF,
        (addr->sin_addr.s_addr >> 16) & 0xFF, (addr->sin_addr.s_addr >> 24) & 0xFF,
        NTOHS(addr->sin_port),
        proto_is_tcp ? "TCP" : "UDP");

    /* retry up to 5 times with backoff */
    for (attempt = 1; attempt <= 5; attempt++) {
        errno = 0;
        ret = bind(fd, (struct sockaddr *)addr, sizeof(*addr));
        DIAG_LOG("%s: bind attempt %d ret=%d errno=%d", svc, attempt, ret, errno);
        if (ret == 0) return 0;

        /* if errno=5 (ERR_USE / netif not ready), try specific AP IP */
        if (attempt == 2 && addr->sin_addr.s_addr == 0) {
            addr->sin_addr.s_addr = AP_IP;
            DIAG_LOG("%s: retry with specific IP %u.%u.%u.%u",
                svc, addr->sin_addr.s_addr & 0xFF, (addr->sin_addr.s_addr >> 8) & 0xFF,
                (addr->sin_addr.s_addr >> 16) & 0xFF, (addr->sin_addr.s_addr >> 24) & 0xFF);
        }
        LOS_Msleep(1000);
    }

    DIAG_LOG("%s: BIND FAILED after 5 attempts — errno=%d", svc, errno);
    switch (errno) {
    case 5:   DIAG_LOG("%s: errno=5(ERR_USE) netif not ready or addr in use", svc); break;
    case 98:  DIAG_LOG("%s: errno=98(EADDRINUSE) port already bound", svc); break;
    case 99:  DIAG_LOG("%s: errno=99(EADDRNOTAVAIL) addr not local", svc); break;
    case 22:  DIAG_LOG("%s: errno=22(EINVAL) bad sockaddr", svc); break;
    default:  DIAG_LOG("%s: errno=%d", svc, errno); break;
    }
    return ret;
}

/* dump current heap status */
static void diag_heap(const char *tag)
{
    extern unsigned char *m_aucSysMem0;
    /* Must match LOS_MEM_POOL_STATUS exactly (has usageWaterLine if LOSCFG_MEM_WATERLINE=1).
     * Use 8 UINT32 slots to guarantee no overflow regardless of config. */
    unsigned int st[8];
    extern unsigned int LOS_MemInfoGet(void *pool, unsigned int *status);
    memset(st, 0, sizeof(st));
    LOS_MemInfoGet(m_aucSysMem0, st);
    DIAG_LOG("%s: heap used=%u free=%u maxfree=%u usedNodes=%u freeNodes=%u",
             tag, st[0], st[1], st[2], st[3], st[4]);
}

/* dump IP / netif summary */
static void diag_netif(const char *tag)
{
    WifiLinkedInfo info;
    memset(&info, 0, sizeof(info));
    if (GetLinkedInfo(&info) == WIFI_SUCCESS) {
        unsigned int ip = info.ipAddress;
        DIAG_LOG("%s: netif connState=%d ip=%u.%u.%u.%u rssi=%d bssid=%02X:%02X:%02X:%02X:%02X:%02X",
            tag, info.connState,
            ip & 0xFF, (ip >> 8) & 0xFF, (ip >> 16) & 0xFF, (ip >> 24) & 0xFF,
            info.rssi,
            info.bssid[0], info.bssid[1], info.bssid[2],
            info.bssid[3], info.bssid[4], info.bssid[5]);
    } else {
        DIAG_LOG("%s: GetLinkedInfo failed errno=%d", tag, errno);
    }
}

/* boot / reset diagnostics */
static void diag_boot(void)
{
    char boot_cnt[8] = {0};
    int cnt = 0;

    DIAG_LOG("========== BOOT DIAGNOSTICS ==========");
    DIAG_LOG("firmware: z_elderly_care %s %s", __DATE__, __TIME__);
    diag_heap("boot");

    /* boot counter in KV to detect reboot vs serial reopen */
    if (UtilsGetValue("bootcnt", boot_cnt, sizeof(boot_cnt) - 1) >= 0)
        cnt = atoi(boot_cnt);
    cnt++;
    snprintf(boot_cnt, sizeof(boot_cnt), "%d", cnt);
    UtilsSetValue("bootcnt", boot_cnt);
    DIAG_LOG("boot #%d (if this number resets, device rebooted; if same, serial reopened)", cnt);

    DIAG_LOG("AP config: SSID=%s PWD=%s CH=1", PROVISION_AP_SSID, PROVISION_AP_PWD);
    DIAG_LOG("AP target URL: http://%s:%d", PROVISION_HTTP_IP, PROVISION_HTTP_PORT);
    DIAG_LOG("AP IP constant: %u.%u.%u.%u",
        AP_IP & 0xFF, (AP_IP >> 8) & 0xFF, (AP_IP >> 16) & 0xFF, (AP_IP >> 24) & 0xFF);
}

/* ============================================================
 * KV storage: save/load/clear WiFi credentials
 * ============================================================ */
void wifi_provision_save(const char *ssid, const char *pwd)
{
    DIAG_LOG("kv_save: ssid=[%s] pwd_len=%u", ssid, (unsigned)strlen(pwd));
    if (UtilsSetValue(KV_SSID, (char *)ssid) != 0) { TX_LOG("save ssid fail"); return; }
    if (UtilsSetValue(KV_PWD, (char *)pwd) != 0) { TX_LOG("save pwd fail"); return; }
    TX_LOG("WiFi saved: %s", ssid);
}

void wifi_provision_clear(void)
{
    UtilsDeleteValue(KV_SSID);
    UtilsDeleteValue(KV_PWD);
    TX_LOG("WiFi cleared");
}

int wifi_provision_load(char *ssid, char *pwd)
{
    if (!ssid || !pwd) return 0;
    if (UtilsGetValue(KV_SSID, ssid, 32) < 0) return 0;
    if (UtilsGetValue(KV_PWD, pwd, 64) < 0) return 0;
    return 1;
}

/* ============================================================
 * HTTP provisioning page (HTML) — unchanged
 * ============================================================ */
static const char s_config_page[] =
    "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\n\r\n"
    "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>WiFi \xE9\x85\x8D\xE7\xBD\x91</title><style>"
    "body{font-family:sans-serif;background:#f0f4f8;margin:0;padding:20px}"
    ".c{background:#fff;border-radius:16px;padding:24px;max-width:400px;margin:40px auto;box-shadow:0 4px 20px rgba(0,0,0,.1)}"
    "h1{color:#2563eb;font-size:22px;text-align:center;margin:0 0 8px}"
    ".icon{text-align:center;font-size:48px;margin:16px 0}"
    "p{color:#666;text-align:center;font-size:14px}"
    "label{display:block;font-size:14px;margin:12px 0 4px;font-weight:bold}"
    "input{width:100%;padding:12px;border:2px solid #e2e8f0;border-radius:8px;font-size:16px;box-sizing:border-box}"
    "input:focus{border-color:#2563eb;outline:none}"
    "button{width:100%;padding:14px;background:#2563eb;color:#fff;border:none;border-radius:8px;font-size:16px;font-weight:bold;margin-top:20px;cursor:pointer}"
    "button:active{background:#1d4ed8}"
    ".tip{font-size:12px;color:#999;margin-top:16px;text-align:center}"
    "</style></head><body><div class=\"c\">"
    "<div class=\"icon\">\xF0\x9F\x93\xB1</div>"
    "<h1>WiFi \xE9\x85\x8D\xE7\xBD\x91</h1>"
    "<p>\xE8\xAF\xB7\xE8\xBE\x93\xE5\x85\xA5\xE5\xAE\xB6\xE9\x87\x8C\xE7\x9A\x84 WiFi \xE4\xBF\xA1\xE6\x81\xAF</p>"
    "<form action=\"/set\" method=\"get\">"
    "<label>\xE7\xBD\x91\xE7\xBB\x9C\xE5\x90\x8D\xEF\xBC\x88SSID\xEF\xBC\x89</label>"
    "<input name=\"ssid\" placeholder=\"\xE8\xAF\xB7\xE8\xBE\x93\xE5\x85\xA5 WiFi \xE5\x90\x8D\xE7\xA7\xB0\" required>"
    "<label>\xE5\xAF\x86\xE7\xA0\x81</label>"
    "<input name=\"pwd\" type=\"password\" placeholder=\"\xE8\xAF\xB7\xE8\xBE\x93\xE5\x85\xA5 WiFi \xE5\xAF\x86\xE7\xA0\x81\" required>"
    "<button type=\"submit\">\xE8\xBF\x9E\xE6\x8E\xA5</button>"
    "</form>"
    "<p class=\"tip\">\xE8\xBF\x9E\xE6\x8E\xA5\xE6\x88\x90\xE5\x8A\x9F\xE5\x90\x8E\xE8\xAE\xBE\xE5\xA4\x87\xE5\xB0\x86\xE8\x87\xAA\xE5\x8A\xA8\xE4\xB8\x8A\xE4\xBA\x91</p>"
    "</div></body></html>";

static const char s_ok_page[] =
    "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\n\r\n"
    "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>\xE8\xBF\x9E\xE6\x8E\xA5\xE4\xB8\xAD</title><style>body{font-family:sans-serif;background:#ecfdf5;margin:0;padding:20px}"
    ".c{background:#fff;border-radius:16px;padding:32px;max-width:400px;margin:60px auto;text-align:center}"
    "h1{color:#059669}p{color:#666;font-size:14px}</style></head><body><div class=\"c\">"
    "<h1>&#9989; \xE9\x85\x8D\xE7\xBD\x91\xE4\xBF\xA1\xE6\x81\xAF\xE5\xB7\xB2\xE6\x8E\xA5\xE6\x94\xB6</h1>"
    "<p>\xE8\xAE\xBE\xE5\xA4\x87\xE6\xAD\xA3\xE5\x9C\xA8\xE8\xBF\x9E\xE6\x8E\xA5 WiFi...</p>"
    "<p><b>\xE5\x8F\xAF\xE4\xBB\xA5\xE5\x85\xB3\xE9\x97\xAD\xE6\xAD\xA4\xE9\xA1\xB5\xE9\x9D\xA2</b></p>"
    "<p>\xE8\xBF\x9E\xE6\x8E\xA5\xE6\x97\xB6\xE7\x83\xAD\xE7\x82\xB9\xE4\xBC\x9A\xE6\x96\xAD\xE5\xBC\x80\xEF\xBC\x8C<br>"
    "\xE5\xA6\x82\xE6\x9E\x9C\xE5\xA4\xB1\xE8\xB4\xA5\xE4\xBC\x9A\xE8\x87\xAA\xE5\x8A\xA8\xE9\x87\x8D\xE6\x96\xB0\xE5\xBC\x80\xE5\x90\xAF\xE7\x83\xAD\xE7\x82\xB9\xEF\xBC\x8C<br>"
    "\xE5\x8F\xAF\xE4\xBB\xA5\xE9\x87\x8D\xE6\x96\xB0\xE5\xB0\x9D\xE8\xAF\x95\xE3\x80\x82</p>"
    "<p id=\"st\">state: ...</p>"
    "<script>"
    "var t=setInterval(function(){"
    "fetch('/status').then(r=>r.json()).then(j=>{"
    "document.getElementById('st').textContent='state: '+j.state;"
    "if(j.state=='GOT_IP'){document.title='Connected!';clearInterval(t);}"
    "if(j.state=='FAILED'){document.title='Failed';clearInterval(t);}"
    "}).catch(function(){})},2000);"
    "</script></div></body></html>";

/* URL decode: + -> space, %XX -> char */
static void url_decode(char *s)
{
    int i, j = 0, len = (int)strlen(s);
    for (i = 0; i < len; i++) {
        if (s[i] == '+') { s[j++] = ' '; }
        else if (s[i] == '%' && i + 2 < len) {
            char hex[3] = { s[i+1], s[i+2], 0 };
            s[j++] = (char)strtol(hex, NULL, 16);
            i += 2;
        } else { s[j++] = s[i]; }
    }
    s[j] = '\0';
}

/*
 * Find end of a URL query value.
 * Value terminates at '&', ' ', '\r', '\n', or end of string.
 * BUG FIX: previously only looked for '&', so the last parameter
 * (pwd) swallowed " HTTP/1.1\r\nHost: ..." and exceeded the length
 * check, causing parse_params to always return -1.
 */
static const char *find_value_end(const char *s)
{
    while (*s && *s != '&' && *s != ' ' && *s != '\r' && *s != '\n')
        s++;
    return s;
}

/* Parse GET /set?ssid=xxx&pwd=yyy — value-safe for last parameter */
static int parse_params(const char *req, char *ssid, int ssid_sz, char *pwd, int pwd_sz)
{
    const char *p = strstr(req, "GET /set?");
    const char *ss, *se, *ps, *pe;
    int sl, pl;
    if (!p) return -1;
    p += 9;

    ss = strstr(p, "ssid=");
    if (!ss) return -1;
    ss += 5;
    se = find_value_end(ss);
    sl = (int)(se - ss);
    if (sl <= 0 || sl >= ssid_sz) return -1;
    memcpy(ssid, ss, sl); ssid[sl] = '\0';
    url_decode(ssid);

    ps = strstr(p, "pwd=");
    if (!ps) return -1;
    ps += 4;
    pe = find_value_end(ps);
    pl = (int)(pe - ps);
    if (pl < 0 || pl >= pwd_sz) return -1;
    memcpy(pwd, ps, pl); pwd[pl] = '\0';
    url_decode(pwd);

    TX_LOG("parsed: ssid=[%s] pwd_len=%u", ssid, (unsigned)strlen(pwd));
    return 0;
}

/* ============================================================
 * DNS server — captive portal (with diagnostics)
 * ============================================================ */
static void dns_server_task(void *arg)
{
    (void)arg;
    int sfd;
    struct sockaddr_in addr, caddr;
    socklen_t clen;
    char buf[512], resp[512];
    int ret;
    unsigned int rl_dns = 0;   /* rate-limit for DNS query logs */

    DIAG_LOG("dns: starting DNS captive portal task");

    errno = 0;
    sfd = socket(AF_INET, SOCK_DGRAM, 0);
    DIAG_RET("dns.socket", sfd, sfd);
    if (sfd < 0) { DIAG_LOG("dns: SOCKET FAILED, abort"); return; }
    s_dns_fd = sfd;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = HTONS(53);
    addr.sin_addr.s_addr = 0;

    if (diag_bind("dns", sfd, &addr, 0) < 0) {
        DIAG_LOG("dns: BIND FAILED — DNS captive portal will NOT work, phone may disconnect");
        lwip_close(sfd);
        s_dns_fd = -1;
        return;
    }
    s_dns_listening = 1;
    DIAG_LOG("dns: listening on UDP 0.0.0.0:53 fd=%d", sfd);

    while (1) {
        clen = sizeof(caddr);
        ret = recvfrom(sfd, buf, sizeof(buf), 0, (struct sockaddr *)&caddr, &clen);
        if (ret < 0) {
            s_dns_recv_err++;
            DIAG_RATE_LOG(rl_dns, 5000, "dns: recvfrom error ret=%d errno=%d (total_err=%u)",
                          ret, errno, s_dns_recv_err);
            LOS_Msleep(50);
            continue;
        }
        if (ret < 12) {
            DIAG_RATE_LOG(rl_dns, 5000, "dns: short packet %d bytes, skip", ret);
            continue;
        }
        s_dns_recv_ok++;

        /* log query (rate-limited) */
        {
            static unsigned int rl_q = 0;
            unsigned short qtype = 0;
            char qname[64] = {0};
            int qi = 0, ni = 0;
            /* parse QNAME at offset 12 */
            int pos = 12;
            while (pos < ret && buf[pos] != 0 && ni < (int)sizeof(qname) - 1) {
                int lab = (unsigned char)buf[pos++];
                if (lab == 0) break;
                if (ni > 0) qname[ni++] = '.';
                while (lab-- > 0 && pos < ret && ni < (int)sizeof(qname) - 1)
                    qname[ni++] = buf[pos++];
            }
            pos++; /* skip null */
            if (pos + 1 < ret) qtype = (unsigned char)buf[pos] | ((unsigned char)buf[pos+1] << 8);
            DIAG_RATE_LOG(rl_q, 2000, "dns: query name=[%s] qtype=%u from %u.%u.%u.%u:%u len=%d",
                qname, qtype,
                caddr.sin_addr.s_addr & 0xFF, (caddr.sin_addr.s_addr >> 8) & 0xFF,
                (caddr.sin_addr.s_addr >> 16) & 0xFF, (caddr.sin_addr.s_addr >> 24) & 0xFF,
                NTOHS(caddr.sin_port), ret);
            (void)qi;
        }

        /* Build DNS response: echo query header + question, add 1 A record */
        memcpy(resp, buf, ret);
        resp[2] = 0x81;  /* QR=1, Opcode=0, AA=0, TC=0, RD=1 */
        resp[3] = 0x80;  /* RA=1, RCODE=0 */
        resp[6] = 0; resp[7] = 1;  /* ANCOUNT = 1 */
        {
            int pos = ret;
            resp[pos++] = 0xC0; resp[pos++] = 0x0C;  /* pointer to QNAME */
            resp[pos++] = 0x00; resp[pos++] = 0x01;  /* TYPE A */
            resp[pos++] = 0x00; resp[pos++] = 0x01;  /* CLASS IN */
            resp[pos++] = 0x00; resp[pos++] = 0x00; resp[pos++] = 0x00; resp[pos++] = 0x3C;  /* TTL 60 */
            resp[pos++] = 0x00; resp[pos++] = 0x04;  /* RDLENGTH 4 */
            resp[pos++] = (char)(AP_IP & 0xFF);
            resp[pos++] = (char)((AP_IP >> 8) & 0xFF);
            resp[pos++] = (char)((AP_IP >> 16) & 0xFF);
            resp[pos++] = (char)((AP_IP >> 24) & 0xFF);
            ret = sendto(sfd, resp, pos, 0, (struct sockaddr *)&caddr, clen);
            if (ret < 0) {
                s_dns_recv_err++;
                DIAG_RATE_LOG(rl_dns, 5000, "dns: sendto error ret=%d errno=%d", ret, errno);
            }
        }
    }
}

/* ============================================================
 * HTTP server (with diagnostics)
 * ============================================================ */
static void http_server(void)
{
    int sfd, cfd;
    struct sockaddr_in addr, caddr;
    socklen_t clen;
    char buf[1024];
    int ret;
    unsigned int rl_err = 0;

    DIAG_LOG("http: starting HTTP config server task");

    errno = 0;
    sfd = socket(AF_INET, SOCK_STREAM, 0);
    DIAG_RET("http.socket", sfd, sfd);
    if (sfd < 0) { DIAG_LOG("http: SOCKET FAILED, abort"); return; }
    s_http_fd = sfd;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = 0;
    addr.sin_port = HTONS(80);

    if (diag_bind("http", sfd, &addr, 1) < 0) {
        DIAG_LOG("http: BIND FAILED — config page will NOT open, check errno above");
        lwip_close(sfd);
        s_http_fd = -1;
        return;
    }

    errno = 0;
    ret = listen(sfd, 4);
    DIAG_RET("http.listen", sfd, ret);
    if (ret < 0) {
        DIAG_LOG("http: LISTEN FAILED errno=%d", errno);
        lwip_close(sfd);
        s_http_fd = -1;
        return;
    }
    s_http_listening = 1;
    DIAG_LOG("http: listening on TCP 0.0.0.0:80 fd=%d", sfd);

    while (1) {
        clen = sizeof(caddr);
        errno = 0;
        cfd = accept(sfd, (struct sockaddr *)&caddr, &clen);
        if (cfd < 0) {
            DIAG_RATE_LOG(rl_err, 5000, "http: accept error fd=%d errno=%d (total)", cfd, errno);
            LOS_Msleep(100);
            continue;
        }
        s_tcp_conn_count++;
        {
            unsigned int pip = caddr.sin_addr.s_addr;
            DIAG_LOG("http: ACCEPT fd=%d peer=%u.%u.%u.%u:%u conn#=%d",
                cfd, pip & 0xFF, (pip >> 8) & 0xFF, (pip >> 16) & 0xFF, (pip >> 24) & 0xFF,
                NTOHS(caddr.sin_port), s_tcp_conn_count);
        }

        memset(buf, 0, sizeof(buf));
        errno = 0;
        ret = recv(cfd, buf, sizeof(buf) - 1, 0);
        if (ret <= 0) {
            s_http_recv_err++;
            DIAG_LOG("http: recv fd=%d ret=%d errno=%d — peer %s (0=FIN/timeout, <0=error)",
                     cfd, ret, errno, ret == 0 ? "closed gracefully" : "error");
            lwip_close(cfd);
            s_tcp_conn_count--;
            continue;
        }
        s_http_recv_ok++;

        /* log request line (first line only) */
        {
            char line[80] = {0};
            int i;
            for (i = 0; i < ret && i < 79 && buf[i] != '\r' && buf[i] != '\n'; i++)
                line[i] = buf[i];
            DIAG_LOG("http: RECV fd=%d %d bytes req=[%s]", cfd, ret, line);
        }

        /* ---- route: GET /status ---- */
        if (strstr(buf, "GET /status") != NULL) {
            char json[256];
            int jlen;
            WifiLinkedInfo li;
            memset(&li, 0, sizeof(li));
            GetLinkedInfo(&li);
            jlen = snprintf(json, sizeof(json),
                "{\"state\":\"%s\",\"wifi_connected\":%d,\"ssid\":\"%s\","
                "\"rssi\":%d,\"bssid\":\"%02X:%02X:%02X:%02X:%02X:%02X\","
                "\"retry\":%u,\"disc_reason\":%d}",
                prov_state_str(s_prov_state), s_wifi_connected, s_prov_ssid,
                li.rssi,
                li.bssid[0], li.bssid[1], li.bssid[2],
                li.bssid[3], li.bssid[4], li.bssid[5],
                s_prov_retry, s_prov_disc_reason);
            char hdr[128];
            int hlen = snprintf(hdr, sizeof(hdr),
                "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
                "Connection: close\r\nContent-Length: %d\r\n\r\n", jlen);
            send(cfd, hdr, hlen, 0);
            send(cfd, json, jlen, 0);
            DIAG_LOG("http: /status state=%s wifi=%d", prov_state_str(s_prov_state), s_wifi_connected);
            lwip_close(cfd);
            s_tcp_conn_count--;
            continue;
        }

        /* ---- route: GET /set?ssid=xxx&pwd=yyy ---- */
        {
            char ssid[33] = {0}, pwd[65] = {0};
            if (parse_params(buf, ssid, sizeof(ssid), pwd, sizeof(pwd)) == 0) {
                /* validate: SSID 1~32, password 0 (open) or 8~63 */
                int sl = (int)strlen(ssid), pl = (int)strlen(pwd);
                if (sl < 1 || sl > 32 || (pl > 0 && (pl < 8 || pl > 63))) {
                    DIAG_LOG("http: /set INVALID ssid_len=%d pwd_len=%d", sl, pl);
                    const char *err = "HTTP/1.1 400 Bad Request\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\nInvalid SSID or password length";
                    send(cfd, err, strlen(err), 0);
                    lwip_close(cfd);
                    s_tcp_conn_count--;
                    continue;
                }

                /* dedup: if already connecting to same config, just re-send ok */
                if (s_prov_state == PROV_CONNECTING &&
                    strcmp(s_prov_ssid, ssid) == 0 && strcmp(s_prov_pwd, pwd) == 0) {
                    DIAG_LOG("http: /set DEDUP same config already connecting, ignore");
                    int slen = (int)strlen(s_ok_page);
                    send(cfd, s_ok_page, slen, 0);
                    lwip_close(cfd);
                    s_tcp_conn_count--;
                    continue;
                }

                /* store config in RAM, signal STA task */
                strncpy(s_prov_ssid, ssid, sizeof(s_prov_ssid) - 1);
                strncpy(s_prov_pwd, pwd, sizeof(s_prov_pwd) - 1);
                s_prov_state = PROV_RECEIVED;
                s_prov_trigger = 1;
                DIAG_LOG("http: /set RECEIVED ssid=[%s] pwd_len=%u -> trigger STA connect",
                         s_prov_ssid, (unsigned)strlen(s_prov_pwd));

                /* respond immediately — do NOT wait for GOT_IP */
                int slen = (int)strlen(s_ok_page);
                errno = 0;
                ret = send(cfd, s_ok_page, slen, 0);
                DIAG_LOG("http: SEND connecting_page fd=%d ret=%d/%d errno=%d", cfd, ret, slen, errno);
                lwip_close(cfd);
                s_tcp_conn_count--;
            } else {
                int slen = (int)strlen(s_config_page);
                errno = 0;
                ret = send(cfd, s_config_page, slen, 0);
                DIAG_LOG("http: SEND config_page fd=%d ret=%d/%d errno=%d code=200", cfd, ret, slen, errno);
                lwip_close(cfd);
                s_tcp_conn_count--;
            }
        }
    }
}

/* ============================================================
 * Periodic status dump (every 5 s, rate-limited)
 * ============================================================ */
static void diag_status_task(void *arg)
{
    (void)arg;
    unsigned int tick = 0;
    DIAG_LOG("diag: periodic status task started (every 5000ms)");
    while (1) {
        LOS_Msleep(5000);
        tick++;
        DIAG_LOG("--- status #%u ---", tick);
        diag_netif("stat");
        diag_heap("stat");
        DIAG_LOG("stat: http_fd=%d listening=%d tcp_conn=%d recv_ok=%u recv_err=%u",
                 s_http_fd, s_http_listening, s_tcp_conn_count,
                 s_http_recv_ok, s_http_recv_err);
        DIAG_LOG("stat: dns_fd=%d listening=%d recv_ok=%u recv_err=%u",
                 s_dns_fd, s_dns_listening, s_dns_recv_ok, s_dns_recv_err);
        DIAG_LOG("stat: wifi_connected=%d prov_state=%s prov_ssid=[%s] retry=%u",
                 s_wifi_connected, prov_state_str(s_prov_state), s_prov_ssid, s_prov_retry);
    }
}

/* ============================================================
 * AP mode task: start AP + DNS captive portal + HTTP server
 * ============================================================ */
static int sta_connect(const char *ssid, const char *pwd);  /* forward decl */

/*
 * Async STA connect task: waits for s_prov_trigger, then calls sta_connect().
 * Keeps AP + HTTP + DNS alive while connecting.
 * Flow: RECEIVED -> CONNECTING -> GOT_IP / FAILED
 *   On GOT_IP: save config to KV, set wifi_connected=1.
 *   On FAILED: set state=FAILED, keep AP for retry.
 */
static void sta_connect_task(void *arg)
{
    (void)arg;
    s_sta_task_running = 1;
    DIAG_LOG("sta_task: started, waiting for config trigger...");

    while (1) {
        /* sleep 500ms, check for trigger */
        LOS_Msleep(500);
        if (!s_prov_trigger) continue;

        /* claim the trigger */
        s_prov_trigger = 0;

        DIAG_LOG("sta_task: RECEIVED -> CONNECTING ssid=[%s] pwd_len=%u retry=%u",
                 s_prov_ssid, (unsigned)strlen(s_prov_pwd), s_prov_retry);
        s_prov_state = PROV_CONNECTING;

        /* try STA connect (up to 30s) */
        if (sta_connect(s_prov_ssid, s_prov_pwd) == 0) {
            /* success: save to KV for next boot, stay connected */
            wifi_provision_save(s_prov_ssid, s_prov_pwd);
            s_prov_state = PROV_GOT_IP;
            s_wifi_connected = 1;
            DIAG_LOG("sta_task: CONNECTING -> GOT_IP, config saved to KV");
        } else {
            s_prov_state = PROV_FAILED;
            s_prov_retry++;
            s_wifi_connected = 0;
            DIAG_LOG("sta_task: CONNECTING -> FAILED (retry #%u disc_reason=%d), restoring AP",
                     s_prov_retry, s_prov_disc_reason);

            /* restore AP: write config to Flash + re-enable hotspot */
            set_wifi_config_ssid(printf, (uint8_t *)PROVISION_AP_SSID);
            set_wifi_config_passwd(printf, (uint8_t *)PROVISION_AP_PWD);
            set_wifi_config_mode(printf, (uint8_t *)"AP");
            SetApModeOn();
            DIAG_LOG("sta_task: AP restored (SSID=%s), waiting for new config", PROVISION_AP_SSID);
        }
    }
}

static void ap_task(void *arg)
{
    (void)arg;
    unsigned int tid;
    TSK_INIT_PARAM_S task = {0};

    DIAG_LOG("ap_task: === AP config mode ===");
    DIAG_LOG("ap_task: SSID=%s PWD=%s URL=http://%s",
             PROVISION_AP_SSID, PROVISION_AP_PWD, PROVISION_HTTP_IP);

    /* step-by-step AP bringup */
    DIAG_LOG("ap_task: step 1/5 set_wifi_config_ssid");
    set_wifi_config_ssid(printf, (uint8_t *)PROVISION_AP_SSID);

    DIAG_LOG("ap_task: step 2/5 set_wifi_config_passwd");
    set_wifi_config_passwd(printf, (uint8_t *)PROVISION_AP_PWD);

    DIAG_LOG("ap_task: step 3/5 set_wifi_config_mode=AP");
    set_wifi_config_mode(printf, (uint8_t *)"AP");

    DIAG_LOG("ap_task: step 4/5 SetApModeOn()");
    SetApModeOn();

    DIAG_LOG("ap_task: step 5/5 wait for netif ready");
    {
        int i;
        for (i = 0; i < 20; i++) {
            WifiLinkedInfo li;
            memset(&li, 0, sizeof(li));
            if (GetLinkedInfo(&li) == WIFI_SUCCESS && li.ipAddress != 0) {
                DIAG_LOG("ap_task: netif ready after %d00ms ip=%u.%u.%u.%u",
                    i, li.ipAddress & 0xFF, (li.ipAddress >> 8) & 0xFF,
                    (li.ipAddress >> 16) & 0xFF, (li.ipAddress >> 24) & 0xFF);
                break;
            }
            LOS_Msleep(500);
        }
    }
    LOS_Msleep(1000);

    diag_netif("ap_after_on");
    diag_heap("ap_after_on");

    /* start periodic status */
    task.pfnTaskEntry = (TSK_ENTRY_FUNC)diag_status_task;
    task.uwStackSize = 8192;
    task.pcName = "diag_st";
    task.usTaskPrio = 26;
    LOS_TaskCreate(&tid, &task);

    /* DNS captive portal */
    task.pfnTaskEntry = (TSK_ENTRY_FUNC)dns_server_task;
    task.uwStackSize = 8192;
    task.pcName = "dns_cap";
    task.usTaskPrio = 21;
    LOS_TaskCreate(&tid, &task);

    /* async STA connect worker — watches s_prov_trigger */
    task.pfnTaskEntry = (TSK_ENTRY_FUNC)sta_connect_task;
    task.uwStackSize = 16384;
    task.pcName = "sta_conn";
    task.usTaskPrio = 22;
    LOS_TaskCreate(&tid, &task);

    /* HTTP config page (blocks forever in accept loop) */
    http_server();
}

/* ============================================================
 * STA connect (with diagnostics)
 * ============================================================ */
static int sta_connect(const char *ssid, const char *pwd)
{
    WifiLinkedInfo info;
    unsigned int retry;
    unsigned int ip, prev_ip = 0;
    int stable = 0;

    DIAG_LOG("sta: connecting to [%s] pwd_len=%u", ssid, (unsigned)strlen(pwd));
    set_wifi_config_route_ssid(printf, (uint8_t *)ssid);
    set_wifi_config_route_passwd(printf, (uint8_t *)pwd);

    /*
     * APSTA 信道冲突修复:
     * 单射频芯片无法同时在 AP 信道(1)和路由器信道上工作。
     * 必须先关 STA + AP，等 AP 彻底关闭、旧 IP 清理完毕，再开 STA。
     * 否则 WiFi 驱动在 BSSID 发现阶段卡死(recovery process 后无 AP BSSID)。
     */
    DIAG_LOG("sta: SetWifiModeOff + SetApModeOff (APSTA channel conflict fix)");
    SetWifiModeOff();
    LOS_Msleep(500);   /* 等 STA 彻底关闭 */

    SetApModeOff();

    /* 等 AP 彻底关闭 + 旧 IP(192.168.2.10)从 netif 清除 (排查点#4+#5) */
    {
        int i;
        for (i = 0; i < 20; i++) {
            WifiErrorCode ap_st = IsHotspotActive();
            if (ap_st != WIFI_HOTSPOT_ACTIVE) {
                DIAG_LOG("sta: AP confirmed inactive after %dms", i * 100);
                break;
            }
            LOS_Msleep(100);
        }
        if (i >= 20) {
            DIAG_LOG("sta: WARNING AP still active after 2000ms, force continue");
        }
    }
    LOS_Msleep(1000);  /* 额外等 netif 清理旧 IP/路由 */

    DIAG_LOG("sta: SetWifiModeOn()");
    SetWifiModeOn();

    retry = 30;
    while (retry--) {
        memset(&info, 0, sizeof(info));
        if (GetLinkedInfo(&info) == WIFI_SUCCESS) {
            if (info.connState == WIFI_CONNECTED && info.ipAddress != 0) {
                ip = info.ipAddress;
                if (ip == prev_ip) {
                    stable++;
                    if (stable >= 3) {
                        DIAG_LOG("sta: CONNECTED (stable) ip=%u.%u.%u.%u rssi=%d retry_left=%u",
                            ip & 0xFF, (ip >> 8) & 0xFF, (ip >> 16) & 0xFF, (ip >> 24) & 0xFF,
                            info.rssi, retry);
                        s_wifi_connected = 1;
                        return 0;
                    }
                } else {
                    stable = 0;
                    prev_ip = ip;
                    DIAG_LOG("sta: got ip=%u.%u.%u.%u rssi=%d, verifying... retry_left=%u",
                        ip & 0xFF, (ip >> 8) & 0xFF, (ip >> 16) & 0xFF, (ip >> 24) & 0xFF,
                        info.rssi, retry);
                }
            } else {
                DIAG_LOG("sta: waiting... connState=%d ip=%u retry_left=%u",
                    info.connState, info.ipAddress, retry);
                s_prov_disc_reason = info.connState;
            }
        } else {
            DIAG_LOG("sta: GetLinkedInfo fail errno=%d retry_left=%u", errno, retry);
        }
        LOS_Msleep(1000);
    }
    DIAG_LOG("sta: TIMEOUT after 30s");
    return -1;
}

/* ============================================================
 * Public API
 * ============================================================ */
int wifi_provision_start(void)
{
    char ssid[33] = {0}, pwd[65] = {0};

    diag_boot();

    DIAG_LOG("wifi_provision_start: checking KV for saved WiFi...");
    if (wifi_provision_load(ssid, pwd)) {
        DIAG_LOG("wifi_provision_start: found saved [%s] pwd_len=%u, trying STA",
                 ssid, (unsigned)strlen(pwd));
        strncpy(s_prov_ssid, ssid, sizeof(s_prov_ssid) - 1);
        strncpy(s_prov_pwd, pwd, sizeof(s_prov_pwd) - 1);
        s_prov_state = PROV_CONNECTING;
        if (sta_connect(ssid, pwd) == 0) {
            s_prov_state = PROV_GOT_IP;
            DIAG_LOG("wifi_provision_start: STA success, return 0");
            return 0;
        }
        s_prov_state = PROV_FAILED;
        DIAG_LOG("wifi_provision_start: STA failed, entering AP (credentials kept)");
    } else {
        DIAG_LOG("wifi_provision_start: no saved WiFi, entering AP");
    }

    {
        unsigned int tid;
        TSK_INIT_PARAM_S task = {0};
        task.pfnTaskEntry = (TSK_ENTRY_FUNC)ap_task;
        task.uwStackSize = 20480;
        task.pcName = "wifi_ap";
        task.usTaskPrio = 20;
        if (LOS_TaskCreate(&tid, &task) != LOS_OK) {
            DIAG_LOG("wifi_provision_start: ap_task create FAILED");
        } else {
            DIAG_LOG("wifi_provision_start: ap_task created tid=%u prio=20", tid);
        }
    }
    return -1;
}

int wifi_provision_is_connected(void)
{
    return s_wifi_connected;
}
