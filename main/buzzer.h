#ifndef BUZZER_H
#define BUZZER_H

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// 蜂鸣器 PWM 配置
#define BUZZER_GPIO         6           // 蜂鸣器 PWM 控制引脚
#define BUZZER_LEDC_TIMER   LEDC_TIMER_0
#define BUZZER_LEDC_CHANNEL LEDC_CHANNEL_0
#define BUZZER_FREQ_HZ      1000         // 有源蜂鸣器驱动频率(低频,避免响应不及)
#define BUZZER_RESOLUTION   LEDC_TIMER_10_BIT   // 10bit 分辨率,占空比范围 0..1023

// 音量范围(0=静音, 100=最大)
#define BUZZER_VOLUME_MIN      0
#define BUZZER_VOLUME_MAX      100
#define BUZZER_VOLUME_DEFAULT  50

/**
 * @brief 初始化蜂鸣器 PWM
 *        内部会从 SPIFFS 读取上次保存的音量并应用
 */
esp_err_t buzzer_init(void);

/**
 * @brief 设置音量(0..100)
 *        0 = 静音(停止 PWM 输出)
 *        1..100 = 按比例设置占空比
 */
esp_err_t buzzer_set_volume(int volume);

/**
 * @brief 获取当前音量(0..100)
 */
int buzzer_get_volume(void);

/**
 * @brief 短鸣一声(用于按键反馈等)
 *        会以当前音量鸣响 100ms
 */
void buzzer_beep(void);

// --------------------- 音量持久化(SPIFFS) ---------------------

/**
 * @brief 从 SPIFFS 加载音量
 */
int buzzer_volume_load(void);

/**
 * @brief 保存音量到 SPIFFS
 */
esp_err_t buzzer_volume_save(int volume);

#ifdef __cplusplus
}
#endif

#endif
