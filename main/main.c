/*
 * SPDX-FileCopyrightText: 2023-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include "waveshare_rgb_lcd_port.h"
#include "lvgl_port.h"
#include "ui.h"
#include "wifi_sync.h"
#include "buzzer.h"

void app_main()
{
    waveshare_esp32_s3_rgb_lcd_init(); // Initialize the Waveshare ESP32-S3 RGB LCD
    // wavesahre_rgb_lcd_bl_on();  //Turn on the screen backlight
    // wavesahre_rgb_lcd_bl_off(); //Turn off the screen backlight

    // 初始化 SPIFFS
    esp_err_t ret = init_spiffs();
    if (ret != ESP_OK) {
        return;
    }

    // 初始化NVS
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // NVS分区被截断，需要擦除并重试
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    vTaskDelay(pdMS_TO_TICKS(20));

    // 启动 WiFi 连接 + SNTP 时间同步(异步任务,后台自动连接和重连)
    wifi_sync_start();

    // 初始化蜂鸣器 PWM(从 SPIFFS 读取上次保存的音量)
    buzzer_init();

    // 读取并应用上次保存的熄屏时间
    // 【必须在 init_spiffs() 成功之后】熄屏时间保存在 SPIFFS 中,
    // 若在挂载前读取会失败并静默退回默认值 10 秒,
    // 表现为"设置好的熄屏时间重启后丢失"。
    int saved_timeout = screen_timeout_load();
    lvgl_port_set_screen_timeout(saved_timeout);
    ESP_LOGI("app", "已加载熄屏时间: %d 秒", saved_timeout);

    ESP_LOGI(TAG, "Display LVGL demos");
    // Lock the mutex due to the LVGL APIs are not thread-safe
    if (lvgl_port_lock(-1)) {
        // lv_demo_stress();
        // lv_demo_benchmark();
        // lv_demo_music();
        // lv_demo_widgets();
        // example_lvgl_demo_ui();
        // Release the mutex
        ui_init();
        lvgl_port_unlock();
    }

    // // 卸载 SPIFFS
    // esp_vfs_spiffs_unregister(NULL);
    // ESP_LOGI("SPIFFS", "SPIFFS unmounted");
}
