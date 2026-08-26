/*
 * 有源蜂鸣器 PWM 驱动
 * 有源蜂鸣器自带振荡电路,只需 PWM 开关控制
 * 频率设低(避免蜂鸣器来不及响应),占空比直接对应平均电压=音量
 */

#include <stdio.h>
#include <stdlib.h>
#include "driver/ledc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "buzzer.h"
#include "spiffs.h"

static const char *TAG_BUZZER = "buzzer";

static int s_volume = BUZZER_VOLUME_DEFAULT;   // 当前音量(0..100)
static bool s_inited = false;                    // 是否已初始化

#define BUZZER_VOLUME_FILE "/spiffs/buzzer_volume.txt"
#define BUZZER_DUTY_MAX    ((1 << 10) - 1)       // 10bit 最大占空比 1023

// 音量(0..100) → PWM占空比(0..BUZZER_DUTY_MAX)
// 有源蜂鸣器依靠PWM平均电压调节音量；此处做幅度减半处理，限制最大输出占空比，避免蜂鸣器过载刺耳
// 0 = 完全静音，数值越大平均电压越高，蜂鸣器响度越大
static uint32_t volume_to_duty(int volume)
{
    if (volume <= 0) return 0;
    if (volume > BUZZER_VOLUME_MAX) volume = BUZZER_VOLUME_MAX;
    // 映射到0~BUZZER_DUTY_MAX/2，把最大输出限制在半占空比
    return (uint32_t)(volume * (BUZZER_DUTY_MAX) / 2 ) / BUZZER_VOLUME_MAX;
}

esp_err_t buzzer_init(void)
{
    if (s_inited) return ESP_OK;

    // 配置 LEDC 定时器
    ledc_timer_config_t timer_cfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = BUZZER_RESOLUTION,
        .timer_num = BUZZER_LEDC_TIMER,
        .freq_hz = BUZZER_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t ret = ledc_timer_config(&timer_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_BUZZER, "定时器配置失败: %s", esp_err_to_name(ret));
        return ret;
    }

    // 配置 LEDC 通道
    ledc_channel_config_t ch_cfg = {
        .gpio_num = BUZZER_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = BUZZER_LEDC_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = BUZZER_LEDC_TIMER,
        .duty = 0,                    // 初始占空比为 0(静音)
        .hpoint = 0,
    };
    ret = ledc_channel_config(&ch_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_BUZZER, "通道配置失败: %s", esp_err_to_name(ret));
        return ret;
    }

    // 从 SPIFFS 读取上次保存的音量
    s_volume = buzzer_volume_load();
    ESP_LOGI(TAG_BUZZER, "蜂鸣器初始化完成,音量: %d", s_volume);

    // 初始状态:静音(不主动鸣响,等用户操作或调用 beep)
    ledc_set_duty(LEDC_LOW_SPEED_MODE, BUZZER_LEDC_CHANNEL, 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, BUZZER_LEDC_CHANNEL);

    s_inited = true;
    return ESP_OK;
}

esp_err_t buzzer_set_volume(int volume)
{
    if (volume < BUZZER_VOLUME_MIN) volume = BUZZER_VOLUME_MIN;
    if (volume > BUZZER_VOLUME_MAX) volume = BUZZER_VOLUME_MAX;
    s_volume = volume;

    if (!s_inited) return ESP_ERR_INVALID_STATE;

    uint32_t duty = volume_to_duty(volume);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, BUZZER_LEDC_CHANNEL, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, BUZZER_LEDC_CHANNEL);
    return ESP_OK;
}

int buzzer_get_volume(void)
{
    return s_volume;
}

void buzzer_beep(void)
{
    if (!s_inited || s_volume <= 0) return;

    // 以当前音量鸣响 100ms
    uint32_t duty = volume_to_duty(s_volume);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, BUZZER_LEDC_CHANNEL, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, BUZZER_LEDC_CHANNEL);
    vTaskDelay(pdMS_TO_TICKS(100));
    ledc_set_duty(LEDC_LOW_SPEED_MODE, BUZZER_LEDC_CHANNEL, 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, BUZZER_LEDC_CHANNEL);
}

// --------------------- 音量持久化(SPIFFS) ---------------------

int buzzer_volume_load(void)
{
    FILE *fp = fopen(BUZZER_VOLUME_FILE, "r");
    if (fp == NULL) {
        return BUZZER_VOLUME_DEFAULT;
    }
    char buf[16] = {0};
    int val = BUZZER_VOLUME_DEFAULT;
    if (fgets(buf, sizeof(buf), fp) != NULL) {
        val = atoi(buf);
    }
    fclose(fp);
    if (val < BUZZER_VOLUME_MIN) val = BUZZER_VOLUME_MIN;
    if (val > BUZZER_VOLUME_MAX) val = BUZZER_VOLUME_MAX;
    return val;
}

esp_err_t buzzer_volume_save(int volume)
{
    if (volume < BUZZER_VOLUME_MIN || volume > BUZZER_VOLUME_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    FILE *fp = fopen(BUZZER_VOLUME_FILE, "w");
    if (fp == NULL) {
        ESP_LOGE(TAG_BUZZER, "保存音量失败");
        return ESP_FAIL;
    }
    fprintf(fp, "%d", volume);
    fclose(fp);
    return ESP_OK;
}
