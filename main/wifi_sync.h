#ifndef WIFI_SYNC_H
#define WIFI_SYNC_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// WiFi 热点配置(用户指定)
#define WIFI_SSID           "JACK"
#define WIFI_PASSWORD       "12345678"

// 息屏时间范围(秒)
#define SCREEN_TIMEOUT_MIN      10
#define SCREEN_TIMEOUT_MAX       30
#define SCREEN_TIMEOUT_DEFAULT  10
#define SCREEN_TIMEOUT_STEP      10  // 10/20/30 三档

/**
 * @brief 启动 WiFi 连接 + SNTP 时间同步
 *        内部创建任务,连接热点后自动同步网络时间
 *        阻塞直到 WiFi 初始化完成(不一定已连上)
 */
void wifi_sync_start(void);

/**
 * @brief 检查 WiFi 是否已连接
 */
bool wifi_sync_is_connected(void);

/**
 * @brief 获取当前时间字符串(HH:MM),用于桌面显示
 *        如果时间未同步,返回 "--:--"
 * @param buf 输出缓冲
 * @param buf_size 缓冲大小
 */
void wifi_sync_get_time_str(char *buf, int buf_size);

// --------------------- 息屏时间配置(SPIFFS持久化) ---------------------

/**
 * @brief 从 SPIFFS 加载息屏时间(秒)
 *        文件不存在或读取失败时返回默认值
 */
int screen_timeout_load(void);

/**
 * @brief 保存息屏时间(秒)到 SPIFFS
 */
esp_err_t screen_timeout_save(int seconds);

#ifdef __cplusplus
}
#endif

#endif
