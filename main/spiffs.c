#include "spiffs.h"
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>

// 初始化 SPIFFS
esp_err_t init_spiffs() {

    // 初始化 SPIFFS
    ESP_LOGI("SPIFFS", "Initializing SPIFFS");

    // 配置 SPIFFS 文件系统
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",            // 文件系统的挂载路径
        .partition_label = NULL,           // 使用默认的 SPIFFS 分区标签
        .max_files = 5,                    // 最大同时打开的文件数
        .format_if_mount_failed = true     // 如果挂载失败，则格式化文件系统
    };

    // 使用上述配置初始化并挂载 SPIFFS 文件系统
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE("SPIFFS", "Failed to mount or format filesystem");// 挂载或格式化失败
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE("SPIFFS", "Failed to find SPIFFS partition");// 未找到 SPIFFS 分区
        } else {
            ESP_LOGE("SPIFFS", "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));// 初始化 SPIFFS 失败
        }
        return ret;
    }

    // 获取 SPIFFS 分区的信息
    size_t total = 0, used = 0;
    ret = esp_spiffs_info(NULL, &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGE("SPIFFS", "Failed to get SPIFFS partition information (%s)", esp_err_to_name(ret));// 获取分区信息失败，格式化
    } else {
        ESP_LOGI("SPIFFS", "Partition size: total: %d, used: %d", total, used);// 分区大小信息
    }

    // 检查报告的分区大小信息的一致性
    if (used > total) {
        ESP_LOGW("SPIFFS", "Number of used bytes cannot be larger than total. Performing SPIFFS_check()."); // 已使用字节数不能大于总字节数，执行 SPIFFS_check
        //如果 ESP32-S3 在文件系统操作期间断电，可能会导致 SPIFFS 损坏。但是仍可通过 esp_spiffs_check 函数恢复文件系统。
        ret = esp_spiffs_check(conf.partition_label);
        if (ret != ESP_OK) {
            ESP_LOGE("SPIFFS", "SPIFFS_check() failed (%s)", esp_err_to_name(ret)); // SPIFFS_check 失败
        } else {
            ESP_LOGI("SPIFFS", "SPIFFS_check() successful"); // SPIFFS_check 成功
        }
    }
    return ret;
}

// 存储账号密码到 SPIFFS
esp_err_t save_account(const char *account, const char *password) {
    ESP_LOGI("SPIFFS", "Opening file"); // 打开文件
    FILE *file = fopen(FILE_PATH, "a");
    if (file == NULL) {
        ESP_LOGE("SPIFFS", "Failed to open file for writing");// 打开文件失败
        return ESP_FAIL;
    }

    fprintf(file, "%s,%s\n", account, password); // 写入文件
    fclose(file);
    ESP_LOGI("SPIFFS", "File written"); // 文件写入成功
    return ESP_OK;
}

// 检查文件中是否存在相同的账户或匹配的账号和密码
bool check_account(const char *account, const char *password) {
    ESP_LOGI("SPIFFS", "Reading file"); // 读取文件
    FILE *file = fopen(FILE_PATH, "r");
    if (file == NULL) {
        ESP_LOGE("SPIFFS", "Failed to open file for reading");// 打开文件失败
        return false;
    }

    char line[MAX_ACCOUNT_LENGTH + MAX_PASSWORD_LENGTH + 2];
    while (fgets(line, sizeof(line), file) != NULL) {
        char existing_account[MAX_ACCOUNT_LENGTH];
        char existing_password[MAX_PASSWORD_LENGTH];
        sscanf(line, "%[^,],%s", existing_account, existing_password);

        // 去除密码末尾的换行符
        size_t len = strlen(existing_password);
        if (len > 0 && existing_password[len-1] == '\n') {
            existing_password[len-1] = '\0';
        }

        // 检查是否存在相同的账户
        if (strcmp(existing_account, account) == 0) {
            if (password != NULL) {
                // 如果传入了密码，检查密码是否匹配
                if (strcmp(existing_password, password) == 0) {
                    fclose(file);
                    return true;
                }
            } else {
                // 如果没有传入密码，说明只检查账户是否存在
                fclose(file);
                return true;
            }
        }
    }
    fclose(file);
    return false;
}

// 打印文件中所有账号密码
void print_all_accounts() {
    ESP_LOGI("SPIFFS", "开始读取所有账号密码...");
    
    // 打开文件（路径需与保存时一致）
    FILE *file = fopen(FILE_PATH, "r");
    if (file == NULL) {
        ESP_LOGE("SPIFFS", "无法打开文件查看内容");
        return;
    }

    char line[MAX_ACCOUNT_LENGTH + MAX_PASSWORD_LENGTH + 2];
    int line_num = 1;
    // 逐行读取并打印
    while (fgets(line, sizeof(line), file) != NULL) {
        // 去除行尾的换行符（方便打印）
        line[strcspn(line, "\n")] = '\0';
        ESP_LOGI("SPIFFS", "第%d行内容: %s", line_num, line);
        line_num++;
    }

    fclose(file);
    ESP_LOGI("SPIFFS", "账号密码读取完毕");
}

// ---------------- 用户文件管理 API 实现 ----------------

// 拼接用户文件完整路径:/spiffs/usr_<name>.txt
void get_user_file_path(const char *name, char *out_path, size_t out_size)
{
    snprintf(out_path, out_size, "%s/usr_%s.txt", USER_FILE_DIR, name);
}

// 检查用户文件是否已存在(使用 stat 接口)
bool is_user_file_exists(const char *name)
{
    char path[64] = {0};
    get_user_file_path(name, path, sizeof(path));
    struct stat st;
    return (stat(path, &st) == 0);   // stat 返回 0 表示文件存在
}

// 创建新的用户文件(空内容),先做重名检查
esp_err_t create_user_file(const char *name)
{
    // 参数校验:名字非空且长度不超限
    if (name == NULL || name[0] == '\0' || strlen(name) >= USER_FILE_NAME_MAX) {
        ESP_LOGE("SPIFFS", "无效的文件名");
        return ESP_ERR_INVALID_ARG;
    }

    // 重名检查
    if (is_user_file_exists(name)) {
        ESP_LOGW("SPIFFS", "文件已存在: usr_%s.txt", name);
        return ESP_ERR_INVALID_STATE;
    }

    char path[64] = {0};
    get_user_file_path(name, path, sizeof(path));

    // 以写方式创建空文件
    FILE *file = fopen(path, "w");
    if (file == NULL) {
        ESP_LOGE("SPIFFS", "创建文件失败: %s", path);
        return ESP_FAIL;
    }
    fclose(file);
    ESP_LOGI("SPIFFS", "文件创建成功: %s", path);
    return ESP_OK;
}

// 删除用户文件
esp_err_t delete_user_file(const char *name)
{
    if (name == NULL || name[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    char path[64] = {0};
    get_user_file_path(name, path, sizeof(path));

    if (!is_user_file_exists(name)) {
        ESP_LOGW("SPIFFS", "文件不存在: %s", path);
        return ESP_ERR_NOT_FOUND;
    }

    if (remove(path) != 0) {
        ESP_LOGE("SPIFFS", "删除文件失败: %s", path);
        return ESP_FAIL;
    }
    ESP_LOGI("SPIFFS", "文件已删除: %s", path);
    return ESP_OK;
}

// 读取用户文件全部内容
int read_user_file(const char *name, char *buf, size_t bufsize)
{
    if (name == NULL || buf == NULL || bufsize == 0) {
        return -1;
    }
    buf[0] = '\0';

    char path[64] = {0};
    get_user_file_path(name, path, sizeof(path));

    FILE *file = fopen(path, "r");
    if (file == NULL) {
        ESP_LOGE("SPIFFS", "打开文件失败: %s", path);
        return -1;
    }

    size_t total = fread(buf, 1, bufsize - 1, file);
    buf[total] = '\0';
    fclose(file);
    return (int)total;
}

// 列出所有用户文件名(去掉 usr_ 前缀和 .txt 后缀)
int get_user_file_list(char names[][USER_FILE_NAME_MAX], int max_count)
{
    if (names == NULL || max_count <= 0) {
        return 0;
    }

    DIR *dir = opendir(USER_FILE_DIR);
    if (dir == NULL) {
        ESP_LOGE("SPIFFS", "打开目录失败: %s", USER_FILE_DIR);
        return 0;
    }

    int count = 0;
    struct dirent *entry = NULL;
    while ((entry = readdir(dir)) != NULL && count < max_count) {
        // 注意: ESP-IDF SPIFFS 的 readdir 不设置 d_type(返回 DT_UNKNOWN),
        // 所以不能按 d_type 过滤,只按文件名规则过滤即可
        const char *fname = entry->d_name;

        // 名称必须以 "usr_" 开头并以 ".txt" 结尾
        if (strncmp(fname, "usr_", 4) != 0) {
            continue;
        }
        size_t len = strlen(fname);
        if (len < 8) {   // 最短: usr_x.txt = 8 字符
            continue;
        }
        if (strcmp(fname + len - 4, ".txt") != 0) {
            continue;
        }

        // 提取中间部分作为用户文件名
        size_t name_len = len - 4 - 4;   // 去掉 "usr_" 和 ".txt"
        if (name_len >= USER_FILE_NAME_MAX) {
            name_len = USER_FILE_NAME_MAX - 1;
        }
        memcpy(names[count], fname + 4, name_len);
        names[count][name_len] = '\0';
        count++;
    }

    closedir(dir);
    ESP_LOGI("SPIFFS", "列出用户文件 %d 个", count);
    return count;
}