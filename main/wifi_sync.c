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
#define WIFI_FAIL_BIT       BIT1
#define WIFI_RECONNECT_BIT  BIT2   // 断开事件置位,由后台任务负责延时重连

static bool s_wifi_connected = false;
static bool s_sntp_synced = false;

// SNTP 同步完成回调
static void sntp_sync_notification(struct timeval *tv)
{
    ESP_LOGI(TAG_WIFI, "SNTP 时间同步成功");
    s_sntp_synced = true;
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
            // 【注意】本回调运行在 esp_event_loop_create_default() 的系统事件任务上,
            // 绝对不能在这里 vTaskDelay:那会把整个事件循环冻住,期间所有事件
            // (IP 事件、后续 WiFi 事件、其它组件的事件)都派发不出去。
            // 这里只做"置位通知",真正的延时重连交给 wifi_sync_task 去做。
            ESP_LOGI(TAG_WIFI, "WiFi 断开,交由后台任务重连");
            xEventGroupSetBits(s_wifi_event_group, WIFI_RECONNECT_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
        ESP_LOGI(TAG_WIFI, "获取到 IP:" IPSTR, IP2STR(&event->ip_info.ip));
        s_wifi_connected = true;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

// SNTP 初始化
static void my_sntp_init(void)
{
    ESP_LOGI(TAG_WIFI, "正在初始化 SNTP...");
    // 设置中国时区 UTC+8(CST-8 表示东八区)
    setenv("TZ", "CST-8", 1);
    tzset();
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_setservername(1, "ntp.aliyun.com");
    sntp_set_time_sync_notification_cb(sntp_sync_notification);
    sntp_init();
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

    // 等待连接(最多 30 秒)
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                            pdFALSE, pdFALSE,
                                            pdMS_TO_TICKS(30000));
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG_WIFI, "已连接到 %s", WIFI_SSID);
    } else {
        ESP_LOGW(TAG_WIFI, "30 秒内未连上 WiFi,后台将继续重连");
    }

    // 启动 SNTP 同步
    my_sntp_init();

    // 【新增】等待 SNTP 同步完成（最多 15 秒）
    int wait_cnt = 0;
    while (!s_sntp_synced && wait_cnt < 30) {
        vTaskDelay(pdMS_TO_TICKS(500));
        wait_cnt++;
    }
    if (s_sntp_synced) {
        ESP_LOGI(TAG_WIFI, "时间同步完成，当前时间已更新");
    } else {
        ESP_LOGW(TAG_WIFI, "时间同步超时，后台将继续尝试");
    }

    // 常驻循环:只负责"被通知后延时重连"。
    // 这里阻塞等待事件位,不占 CPU;延时发生在本任务上下文中,
    // 不会再冻住系统事件循环。
    while (1) {
        EventBits_t ev = xEventGroupWaitBits(s_wifi_event_group,
                                             WIFI_RECONNECT_BIT,
                                             pdTRUE,      // 取出后自动清位
                                             pdFALSE,
                                             portMAX_DELAY);
        if (ev & WIFI_RECONNECT_BIT) {
            ESP_LOGI(TAG_WIFI, "5 秒后尝试重连...");
            vTaskDelay(pdMS_TO_TICKS(5000));
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
