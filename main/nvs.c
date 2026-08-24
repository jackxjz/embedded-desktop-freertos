#include "nvs.h"

// 将文本存进NVS
esp_err_t save_text_to_nvs(const char *key, const char *text)
{
    nvs_handle_t nvs_handle;
    // 打开命名空间 "user"，读写模式
    esp_err_t err = nvs_open("user", NVS_READWRITE, &nvs_handle);
    if(err != ESP_OK) 
    {
        ESP_LOGE("nvs", "Failed to open NVS namespace: %s", esp_err_to_name(err));
        return err;
    }
    // 将文本写入NVS，键为key
    err = nvs_set_str(nvs_handle, key, text);
    if(err != ESP_OK)
    {
        ESP_LOGE("nvs", "write nvs faile");
        nvs_close(nvs_handle);
        return err;
    }

    // 提交更改，确保数据写入Flash
    err = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
    return err;
}

// 从NVS读取文本
esp_err_t read_text_from_nvs(char *buffer, size_t buffer_size, const char *key)
{
    nvs_handle_t nvs_handle;
    // 打开命名空间 "user"，读写模式
    esp_err_t err = nvs_open("user", NVS_READWRITE, &nvs_handle);
    if(err != ESP_OK) return err;

    // 获取存储的字符串长度
    size_t require_size = 0;
    err = nvs_get_str(nvs_handle, key, NULL, &require_size);
    if(err != ESP_OK)
    {
        ESP_LOGE("nvs", "read nvs faile");
        nvs_close(nvs_handle);
        return err;
    }

    // 确保缓冲区大小足够
    if(require_size > buffer_size)
    {
        nvs_close(nvs_handle);
        return ESP_ERR_INVALID_SIZE;
    }

    // 读取字符串到缓冲区
    err = nvs_get_str(nvs_handle, key, buffer, &require_size);
    nvs_close(nvs_handle);
    return err;
}

// 删除文本
esp_err_t delete_save_account(const char *key)
{
    nvs_handle_t nvs_handle;
    // 打开命名空间 "user"，读写模式
    esp_err_t err = nvs_open("user", NVS_READWRITE, &nvs_handle);
    if(err != ESP_OK) return err;

    // 根据关键字删除
    err = nvs_erase_key(nvs_handle, key);
    if(err != ESP_OK)
    {
        ESP_LOGE("nvs", "delete nvs faile");
        nvs_close(nvs_handle);
        return err;
    }

    // 提交更改
    err = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);

    // 重新打开再关闭，强制刷新缓存
    err = nvs_open("user", NVS_READWRITE, &nvs_handle);
    if(err == ESP_OK) {
        nvs_close(nvs_handle);
    }
    return err;
}