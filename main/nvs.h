#ifndef _NVS_H_
#define _NVS_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"

// 将文本存进NVS
esp_err_t save_text_to_nvs(const char *key, const char *text);

// 从NVS读取文本
esp_err_t read_text_from_nvs(char *buffer, size_t buffer_size, const char *key);

// 删除文本
esp_err_t delete_save_account(const char *key);

void remain_pwd();

#endif