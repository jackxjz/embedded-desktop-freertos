/*
 * SPDX-FileCopyrightText: 2023-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include "waveshare_rgb_lcd_port.h"
#include "ui.h"
#include "wifi_sync.h"

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
