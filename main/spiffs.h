#ifndef _SPIFFS_H_
#define _SPIFFS_H_

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_spiffs.h"

#define MAX_ACCOUNT_LENGTH 50
#define MAX_PASSWORD_LENGTH 50
#define FILE_PATH "/spiffs/account.txt"

// 初始化 SPIFFS
esp_err_t init_spiffs();

// 存储账号密码到 SPIFFS
esp_err_t save_account(const char *account, const char *password);

// 检查文件中是否存在相同的账户或匹配的账号和密码
bool check_account(const char *account, const char *password);

// 打印文件中所有账号密码
void print_all_accounts();

#endif