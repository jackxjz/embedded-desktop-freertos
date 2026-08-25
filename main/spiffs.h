#ifndef _SPIFFS_H_
#define _SPIFFS_H_

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_spiffs.h"

#define MAX_ACCOUNT_LENGTH 50
#define MAX_PASSWORD_LENGTH 50
#define FILE_PATH "/spiffs/account.txt"

// 用户文件相关常量
#define USER_FILE_NAME_MAX      32          // 用户文件名最大长度(不含扩展名和路径前缀)
#define USER_FILE_DIR           "/spiffs"   // 用户文件存放目录
#define USER_FILE_MAX_COUNT     9           // 桌面最多显示的文件数(3x3 网格)

// 初始化 SPIFFS
esp_err_t init_spiffs();

// 存储账号密码到 SPIFFS
esp_err_t save_account(const char *account, const char *password);

// 检查文件中是否存在相同的账户或匹配的账号和密码
bool check_account(const char *account, const char *password);

// 打印文件中所有账号密码
void print_all_accounts();

// ---------------- 用户文件管理 API ----------------

// 拼接用户文件完整路径(结果存入 out_path 缓冲区,长度需 >= 64)
void get_user_file_path(const char *name, char *out_path, size_t out_size);

// 检查用户文件是否已存在(按文件名重名校验)
bool is_user_file_exists(const char *name);

// 创建新的用户文件(空内容),会先做重名检查
// 返回 ESP_OK 成功,ESP_ERR_INVALID_STATE 表示已存在同名,ESP_FAIL 其他错误
esp_err_t create_user_file(const char *name);

// 删除用户文件
// 返回 ESP_OK 成功,ESP_ERR_NOT_FOUND 文件不存在
esp_err_t delete_user_file(const char *name);

// 读取用户文件全部内容到 buf
// 返回实际读取的字节数,-1 表示失败
int read_user_file(const char *name, char *buf, size_t bufsize);

// 列出所有用户文件名(不含 usr_ 前缀和 .txt 后缀)
// names:输出数组,每个元素长度需 >= USER_FILE_NAME_MAX
// max_count:数组最大容量
// 返回实际填入的文件数量
int get_user_file_list(char names[][USER_FILE_NAME_MAX], int max_count);

#endif