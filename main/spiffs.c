#include "spiffs.h"

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