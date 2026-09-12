/*
 * WiFi 连接 + SNTP 时间同步 + 息屏时间配置
 * 开机自动连接指定热点,获取网络时间
 */

#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "nvs_flash.h"
#include "wifi_sync.h"
#include "spiffs.h"

static const char *TAG_WIFI = "wifi_sync";

// 事件组:WiFi 连接状态
static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_RECONNECT_BIT  BIT2   // 断开事件置位,由后台任务负责延时重连

static bool s_wifi_connected = false;
static bool s_sntp_synced = false;
static bool s_sntp_started = false;   // SNTP 客户端是否已启动(只启动一次,之后用 sntp_restart 催)

// 重连退避:连续失败时把重试间隔逐渐拉长,避免密集重连反而惹恼 AP
#define RECONNECT_DELAY_MIN_MS   2000    // 首次重连等待
#define RECONNECT_DELAY_MAX_MS   15000   // 退避上限

// SNTP 同步完成回调
static void sntp_sync_notification(struct timeval *tv)
{
    s_sntp_synced = true;

    // 把同步到的具体时间打出来,这样"到底同步成功没有"在串口里一眼可见
    time_t now = (tv != NULL) ? tv->tv_sec : time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    ESP_LOGI(TAG_WIFI, "SNTP 时间同步成功: %04d-%02d-%02d %02d:%02d:%02d",
             t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
             t.tm_hour, t.tm_min, t.tm_sec);
}

// WiFi 事件回调
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT) {
        if (event_id == WIFI_EVENT_STA_START) {
            esp_wifi_connect();
        } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
            s_wifi_connected = false;
            s_sntp_synced = false;
            // 打印断开原因码,否则"连不上"只能靠猜。常见值:
            //   201 = 找不到 AP(SSID 写错,或信号太弱)
            //    15 = 四次握手超时(通常是密码错)
            // 2/205 = 认证失败;  200 = beacon 超时
            wifi_event_sta_disconnected_t *disc = (wifi_event_sta_disconnected_t *)event_data;
            ESP_LOGW(TAG_WIFI, "WiFi 断开,原因码 %d,交由后台任务重连",
                     (disc != NULL) ? disc->reason : -1);
            // 【注意】本回调运行在 esp_event_loop_create_default() 的系统事件任务上,
            // 绝对不能在这里 vTaskDelay:那会把整个事件循环冻住,期间所有事件
            // (IP 事件、后续 WiFi 事件、其它组件的事件)都派发不出去。
            // 这里只做"置位通知",真正的延时重连交给 wifi_sync_task 去做。
            xEventGroupSetBits(s_wifi_event_group, WIFI_RECONNECT_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
        ESP_LOGI(TAG_WIFI, "获取到 IP:" IPSTR, IP2STR(&event->ip_info.ip));
        s_wifi_connected = true;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

// SNTP 初始化(启动客户端。只启动一次,重复调用直接返回)
static void my_sntp_init(void)
{
    if (s_sntp_started) {
        return;
    }

    ESP_LOGI(TAG_WIFI, "正在初始化 SNTP...");
    // 设置中国时区 UTC+8(CST-8 表示东八区)
    setenv("TZ", "CST-8", 1);
    tzset();
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    // 【服务器选择】主服务器改成 ntp.aliyun.com。
    // 原来主服务器是 pool.ntp.org —— 它在国内经常解析慢或者根本连不上,
    // 而原本写在索引 1 的备用服务器又因为 CONFIG_LWIP_SNTP_MAX_SERVERS=1
    // 被 sntp_setservername() 静默忽略(idx >= SNTP_MAX_SERVERS 直接 return),
    // 等于"主服务器不可靠 + 备用服务器无效",这才是时间迟迟同步不上的主因。
    sntp_setservername(0, "ntp.aliyun.com");
    sntp_setservername(1, "cn.pool.ntp.org");   // 备用:需 CONFIG_LWIP_SNTP_MAX_SERVERS >= 2 才生效
    sntp_set_time_sync_notification_cb(sntp_sync_notification);
    sntp_init();
    s_sntp_started = true;
}

// 催一次时间同步
// 为什么需要它:lwip 的 SNTP 客户端在请求失败后会按退避重试,退避间隔会越翻越大;
// 若设备曾长时间没网,退避可能已经拉得很长,此时刚连上网也要等很久才会重发请求。
// 网络恢复时主动 restart 一次,可以跳过退避、立刻重发。
static void my_sntp_kick(void)
{
    my_sntp_init();

    if (!s_sntp_synced) {
        if (sntp_restart()) {
            ESP_LOGI(TAG_WIFI, "已触发一次时间同步");
        }
    }
}

// WiFi + SNTP 任务
static void wifi_sync_task(void *arg)
{
    // 初始化 TCP/IP 协议栈和 WiFi
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // 注册事件回调
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    // 配置 STA
    wifi_config_t wifi_config = {0};
    strcpy((char *)wifi_config.sta.ssid, WIFI_SSID);
    strcpy((char *)wifi_config.sta.password, WIFI_PASSWORD);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG_WIFI, "WiFi 初始化完成,正在连接 %s ...", WIFI_SSID);

    // ==================== 常驻监督循环 ====================
    // 这个循环同时负责三件事,不再有"只在开机做一次"的步骤:
    //   1. 连上网络   → 启动/催一次时间同步
    //   2. 收到断线通知 → 退避后重连
    //   3. 长时间既没连上也没收到断线事件 → 兜底主动重试
    //
    // 【为什么要把原来那两段阻塞等待删掉】
    // 原代码是"等连接 30 秒 → sntp_init → 等同步 15 秒 → 进入死循环",
    // 那两段等待期间不会做任何重连。如果开机时那一把没连上,要等到大约
    // 47 秒之后才轮到第一次重连 —— 表现就是"按 reset 后半天连不上"。
    // 现在改成单一循环,重连最迟 2 秒后就开始。
    const uint32_t WAIT_TICK_MS   = 5000;   // 每 5 秒醒一次,既不忙等也能发现异常
    const uint32_t WATCHDOG_TICKS = 6;      // 连续 6 次(约 30 秒)毫无动静才兜底重连一次
    uint32_t retry_delay_ms = RECONNECT_DELAY_MIN_MS;
    uint32_t idle_ticks = 0;

    while (1) {
        EventBits_t ev = xEventGroupWaitBits(s_wifi_event_group,
                                             WIFI_CONNECTED_BIT | WIFI_RECONNECT_BIT,
                                             pdTRUE,      // 取出后自动清位
                                             pdFALSE,
                                             pdMS_TO_TICKS(WAIT_TICK_MS));

        // ---- 已连上:只需要关心"网络可用了,把时间同步催一下" ----
        // 这里以 s_wifi_connected 这个当前状态为准,而不是单纯看事件位。
        // 因为快速闪断时 CONNECTED 和 RECONNECT 可能同时置位,
        // 若按事件位优先处理就会把重连请求吞掉。
        if (s_wifi_connected) {
            idle_ticks = 0;
            retry_delay_ms = RECONNECT_DELAY_MIN_MS;
            if (ev & WIFI_CONNECTED_BIT) {
                ESP_LOGI(TAG_WIFI, "已连接到 %s", WIFI_SSID);
                // 网络可用就催一次时间同步。
                // 开机时没网、或中途断线重连,都会走到这里,
                // 所以不会再出现"后来连上网了,时间却一直不对"。
                my_sntp_kick();
            }
            continue;
        }

        // ---- 未连上 ----
        if (ev & WIFI_RECONNECT_BIT) {
            // 明确收到断线通知(正常重连路径):退避后重连
            idle_ticks = 0;
            ESP_LOGI(TAG_WIFI, "%u 秒后重连...", (unsigned)(retry_delay_ms / 1000));
            vTaskDelay(pdMS_TO_TICKS(retry_delay_ms));
            // 退避翻倍并封顶:连续失败时不要一直以最小间隔猛试
            retry_delay_ms *= 2;
            if (retry_delay_ms > RECONNECT_DELAY_MAX_MS) {
                retry_delay_ms = RECONNECT_DELAY_MAX_MS;
            }
            esp_wifi_connect();
        } else if (++idle_ticks >= WATCHDOG_TICKS) {
            // 既没连上、也没收到断线事件:兜底主动重试。
            // 正常情况下重连由断线事件驱动就够了,但万一那次连接尝试
            // 失败了却没发出事件(或事件被丢掉),就会永远卡住不再尝试,
            // 所以这里用一个宽松的看门狗补救。
            idle_ticks = 0;
            ESP_LOGW(TAG_WIFI, "超过 %u 秒仍未连接,主动重试...",
                     (unsigned)(WATCHDOG_TICKS * WAIT_TICK_MS / 1000));
            esp_wifi_connect();
        }
    }
    // 上面的循环是死循环,正常不会走到这里
    // vTaskDelete(NULL);
}

void wifi_sync_start(void)
{
    // 先创建事件组,再启动任务。
    // 【顺序很重要】事件组的创建必须早于事件回调的注册:回调里会用到
    // s_wifi_event_group,若任务还没跑到创建那一步就来了事件,
    // xEventGroupSetBits(NULL, ...) 会直接崩。
    s_wifi_event_group = xEventGroupCreate();
    assert(s_wifi_event_group != NULL);

    // 创建任务运行 WiFi(需要较大栈空间)
    xTaskCreate(wifi_sync_task, "wifi_sync", 6 * 1024, NULL, 5, NULL);
}

bool wifi_sync_is_connected(void)
{
    return s_wifi_connected;
}

void wifi_sync_get_time_str(char *buf, int buf_size)
{
    if (buf == NULL || buf_size <= 0) return;

    if (!s_sntp_synced) {
        // 时间未同步
        snprintf(buf, buf_size, "--:--");
        return;
    }

    time_t now = time(NULL);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    // 时区默认为 UTC+8(中国时区),在 menuconfig 中已设置
    strftime(buf, buf_size, "%H:%M", &timeinfo);
}

// --------------------- 息屏时间配置(SPIFFS持久化) ---------------------

#define SCREEN_TIMEOUT_FILE "/spiffs/screen_timeout.txt"

int screen_timeout_load(void)
{
    FILE *fp = fopen(SCREEN_TIMEOUT_FILE, "r");
    if (fp == NULL) {
        return SCREEN_TIMEOUT_DEFAULT;
    }
    char buf[16] = {0};
    int val = SCREEN_TIMEOUT_DEFAULT;
    if (fgets(buf, sizeof(buf), fp) != NULL) {
        val = atoi(buf);
    }
    fclose(fp);
    // 校验范围(必须是 10/20/30 之一)
    if (val < SCREEN_TIMEOUT_MIN) val = SCREEN_TIMEOUT_MIN;
    if (val > SCREEN_TIMEOUT_MAX) val = SCREEN_TIMEOUT_MAX;
    return val;
}

esp_err_t screen_timeout_save(int seconds)
{
    if (seconds < SCREEN_TIMEOUT_MIN || seconds > SCREEN_TIMEOUT_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    FILE *fp = fopen(SCREEN_TIMEOUT_FILE, "w");
    if (fp == NULL) {
        ESP_LOGE(TAG_WIFI, "保存息屏时间失败");
        return ESP_FAIL;
    }
    fprintf(fp, "%d", seconds);
    fclose(fp);
    return ESP_OK;
}
