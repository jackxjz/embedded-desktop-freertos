/*
 * SPDX-FileCopyrightText: 2023-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 * 说明：本文件是ESP32平台上LVGL图形库与RGB LCD屏幕、触摸屏的适配层代码
 * 主要功能：实现LVGL渲染数据到屏幕的刷新、触摸输入处理、多缓冲区管理等核心功能
 */

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_touch.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "lvgl.h"
#include "lvgl_port.h"
#include "waveshare_rgb_lcd_port.h"   // 背光开关控制
#include "wifi_sync.h"                 // 熄屏时间配置读写

static const char *TAG_LVGL = "lv_port";                      // 日志输出标签
static SemaphoreHandle_t lvgl_mux;                       // LVGL互斥锁（保证线程安全，因LVGL API非线程安全）
static TaskHandle_t lvgl_task_handle = NULL;             // LVGL主任务句柄

lv_indev_t *g_lvgl_indev = NULL;                         // 全局触摸输入设备句柄(供外部绑定光标等使用)

// 熄屏亮屏相关
static uint32_t last_touch_time_ms = 0;   // 最后触摸时间戳(ms)
static bool screen_is_on = true;          // 当前屏幕是否亮屏
static uint32_t screen_timeout_ms = 10000;  // 熄屏超时时间(默认10秒,可由设置界面修改)

/* -------------------------- 屏幕旋转相关函数 -------------------------- */
#if EXAMPLE_LVGL_PORT_ROTATION_DEGREE != 0  // 如果配置了屏幕旋转（非0度），编译以下代码

/**
 * @brief 获取下一个帧缓冲区（双缓冲切换用）
 * @param panel_handle LCD面板句柄
 * @return 下一个要使用的帧缓冲区地址
 * 说明：双缓冲机制中，交替使用两个缓冲区避免刷新撕裂，一个用于LVGL渲染，一个用于屏幕显示
 */
static void *get_next_frame_buffer(esp_lcd_panel_handle_t panel_handle)
{
    static void *next_fb = NULL;                          // 下一个帧缓冲区指针
    static void *fb[2] = { NULL };                        // 存储两个帧缓冲区地址的数组

    if (next_fb == NULL) {  // 首次调用时初始化缓冲区
        // 从LCD驱动获取两个帧缓冲区地址
        ESP_ERROR_CHECK(esp_lcd_rgb_panel_get_frame_buffer(panel_handle, 2, &fb[0], &fb[1]));
        next_fb = fb[1];  // 初始化为第二个缓冲区
    } else {
        // 交替切换缓冲区（fb[0] <-> fb[1]）
        next_fb = (next_fb == fb[0]) ? fb[1] : fb[0];
    }
    return next_fb;
}

/**
 * @brief 旋转并复制像素数据（处理屏幕旋转时的像素映射）
 * @param from 源像素缓冲区（LVGL渲染的原始数据）
 * @param to 目标像素缓冲区（旋转后输出到屏幕的数据）
 * @param x_start/x_end 刷新区域的X起始/结束坐标
 * @param y_start/y_end 刷新区域的Y起始/结束坐标
 * @param w/h 屏幕原始宽/高
 * @param rotation 旋转角度（90/180/270）
 * 说明：硬件不支持旋转时，通过软件计算像素位置实现旋转，性能略低但兼容性好
 */
IRAM_ATTR static void rotate_copy_pixel(const uint16_t *from, uint16_t *to, uint16_t x_start, uint16_t y_start, uint16_t x_end, uint16_t y_end, uint16_t w, uint16_t h, uint16_t rotation)
{
    int from_index = 0;  // 源缓冲区像素索引
    int to_index = 0;    // 目标缓冲区像素索引
    int to_index_const = 0;  // 目标缓冲区常量索引（优化计算）

    switch (rotation) {
    case 90:  // 顺时针旋转90度
        to_index_const = (w - x_start - 1) * h;  // 计算基础索引
        for (int from_y = y_start; from_y < y_end + 1; from_y++) {
            from_index = from_y * w + x_start;  // 源像素位置
            to_index = to_index_const + from_y;  // 目标像素位置
            for (int from_x = x_start; from_x < x_end + 1; from_x++) {
                *(to + to_index) = *(from + from_index);  // 复制像素
                from_index++;
                to_index -= h;  // 90度旋转时的像素偏移规律
            }
        }
        break;
    case 180:  // 旋转180度
        to_index_const = h * w - x_start - 1;
        for (int from_y = y_start; from_y < y_end + 1; from_y++) {
            from_index = from_y * w + x_start;
            to_index = to_index_const - from_y * w;
            for (int from_x = x_start; from_x < x_end + 1; from_x++) {
                *(to + to_index) = *(from + from_index);
                from_index++;
                to_index--;  // 180度旋转时的像素偏移规律
            }
        }
        break;
    case 270:  // 顺时针旋转270度（或逆时针90度）
        to_index_const = (x_start + 1) * h - 1;
        for (int from_y = y_start; from_y < y_end + 1; from_y++) {
            from_index = from_y * w + x_start;
            to_index = to_index_const - from_y;
            for (int from_x = x_start; from_x < x_end + 1; from_x++) {
                *(to + to_index) = *(from + from_index);
                from_index++;
                to_index += h;  // 270度旋转时的像素偏移规律
            }
        }
        break;
    default:
        break;  // 不支持的角度不处理
    }
}
#endif /* EXAMPLE_LVGL_PORT_ROTATION_DEGREE */


/* -------------------------- 防撕裂刷新相关函数 -------------------------- */
#if LVGL_PORT_AVOID_TEAR_ENABLE  // 如果启用防撕裂功能，编译以下代码
#if LVGL_PORT_DIRECT_MODE  // 直接模式（LVGL渲染数据直接输出到屏幕）
#if EXAMPLE_LVGL_PORT_ROTATION_DEGREE != 0  // 且屏幕有旋转

/**
 * @brief 存储需要刷新的"脏区域"信息（只刷新变化的区域，提高效率）
 */
typedef struct {
    uint16_t inv_p;  // 无效区域的数量
    uint8_t inv_area_joined[LV_INV_BUF_SIZE];  // 标记区域是否合并
    lv_area_t inv_areas[LV_INV_BUF_SIZE];  // 无效区域的坐标范围
} lv_port_dirty_area_t;

/**
 * @brief 刷新状态（部分刷新/全量刷新）
 */
typedef enum {
    FLUSH_STATUS_PART,  // 只刷新部分区域
    FLUSH_STATUS_FULL   // 刷新整个屏幕
} lv_port_flush_status_t;

/**
 * @brief 刷新探测结果（决定如何复制数据）
 */
typedef enum {
    FLUSH_PROBE_PART_COPY,  // 只复制部分区域
    FLUSH_PROBE_SKIP_COPY,  // 跳过复制（缓冲区已同步）
    FLUSH_PROBE_FULL_COPY   // 复制整个屏幕
} lv_port_flush_probe_t;

static lv_port_dirty_area_t dirty_area;  // 脏区域实例


/**
 * @brief 保存当前的脏区域信息（用于多缓冲区同步）
 */
static void flush_dirty_save(lv_port_dirty_area_t *dirty_area)
{
    lv_disp_t *disp = _lv_refr_get_disp_refreshing();  // 获取当前刷新的显示屏
    dirty_area->inv_p = disp->inv_p;  // 保存无效区域数量
    for (int i = 0; i < disp->inv_p; i++) {
        dirty_area->inv_area_joined[i] = disp->inv_area_joined[i];  // 保存合并状态
        dirty_area->inv_areas[i] = disp->inv_areas[i];  // 保存区域坐标
    }
}

/**
 * @brief 探测脏区域，决定数据复制策略（避免撕裂的核心逻辑）
 */
static lv_port_flush_probe_t flush_copy_probe(lv_disp_drv_t *drv)
{
    static lv_port_flush_status_t prev_status = FLUSH_STATUS_PART;  // 上一次刷新状态
    lv_port_flush_status_t cur_status;  // 当前刷新状态
    lv_port_flush_probe_t probe_result;  // 探测结果
    lv_disp_t *disp_refr = _lv_refr_get_disp_refreshing();  // 当前刷新的显示屏

    // 计算需要刷新的区域大小
    uint32_t flush_ver = 0;  // 垂直方向大小
    uint32_t flush_hor = 0;  // 水平方向大小
    for (int i = 0; i < disp_refr->inv_p; i++) {
        if (disp_refr->inv_area_joined[i] == 0) {  // 找到第一个未合并的区域
            flush_ver = disp_refr->inv_areas[i].y2 + 1 - disp_refr->inv_areas[i].y1;
            flush_hor = disp_refr->inv_areas[i].x2 + 1 - disp_refr->inv_areas[i].x1;
            break;
        }
    }

    // 判断当前是部分刷新还是全量刷新
    cur_status = ((flush_ver == drv->ver_res) && (flush_hor == drv->hor_res)) ? 
                 FLUSH_STATUS_FULL : FLUSH_STATUS_PART;

    // 根据历史状态决定复制策略（核心：保证缓冲区数据与屏幕显示同步）
    if (prev_status == FLUSH_STATUS_FULL) {
        if (cur_status == FLUSH_STATUS_PART) {
            probe_result = FLUSH_PROBE_FULL_COPY;  // 上一次全刷，本次部分刷：需全量复制
        } else {
            probe_result = FLUSH_PROBE_SKIP_COPY;  // 连续全刷：跳过复制（缓冲区已同步）
        }
    } else {
        probe_result = FLUSH_PROBE_PART_COPY;  // 上一次部分刷：只复制变化区域
    }
    prev_status = cur_status;  // 更新历史状态
    return probe_result;
}

/**
 * @brief 获取下一个用于刷新的缓冲区
 */
static inline void *flush_get_next_buf(void *panel_handle)
{
    return get_next_frame_buffer(panel_handle);
}

/**
 * @brief 复制脏区域数据到目标缓冲区（多缓冲区同步）
 */
static void flush_dirty_copy(void *dst, void *src, lv_port_dirty_area_t *dirty_area)
{
    lv_coord_t x_start, x_end, y_start, y_end;
    for (int i = 0; i < dirty_area->inv_p; i++) {
        if (dirty_area->inv_area_joined[i] == 0) {  // 只处理未合并的区域
            x_start = dirty_area->inv_areas[i].x1;
            x_end = dirty_area->inv_areas[i].x2;
            y_start = dirty_area->inv_areas[i].y1;
            y_end = dirty_area->inv_areas[i].y2;
            // 旋转并复制数据（屏幕旋转时需要）
            rotate_copy_pixel(src, dst, x_start, y_start, x_end, y_end, LV_HOR_RES, LV_VER_RES, EXAMPLE_LVGL_PORT_ROTATION_DEGREE);
        }
    }
}

/**
 * @brief LVGL显示刷新回调函数（带旋转和防撕裂的版本）
 * 说明：LVGL渲染完成后会调用此函数，将数据输出到屏幕
 */
static void flush_callback(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t) drv->user_data;
    const int offsetx1 = area->x1;  // 刷新区域X起始
    const int offsetx2 = area->x2;  // 刷新区域X结束
    const int offsety1 = area->y1;  // 刷新区域Y起始
    const int offsety2 = area->y2;  // 刷新区域Y结束
    void *next_fb = NULL;  // 下一个帧缓冲区
    lv_port_flush_probe_t probe_result = FLUSH_PROBE_PART_COPY;  // 默认探测结果
    lv_disp_t *disp = lv_disp_get_default();  // 默认显示屏

    // 如果是最后一个需要刷新的区域（整帧刷新完成）
    if (lv_disp_flush_is_last(drv)) {
        if (drv->full_refresh) {  // 如果需要全量刷新
            drv->full_refresh = 0;  // 重置标志

            // 获取下一个缓冲区，旋转并复制全屏数据
            next_fb = flush_get_next_buf(panel_handle);
            rotate_copy_pixel((uint16_t *)color_map, next_fb, offsetx1, offsety1, offsetx2, offsety2, LV_HOR_RES, LV_VER_RES, EXAMPLE_LVGL_PORT_ROTATION_DEGREE);

            // 切换屏幕显示的缓冲区（输出到屏幕）
            esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, next_fb);

            // 等待当前缓冲区显示完成（通过信号量同步，避免撕裂）
            ulTaskNotifyValueClear(NULL, ULONG_MAX);
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

            // 同步另一个缓冲区的脏区域（保证多缓冲区数据一致）
            flush_dirty_copy(flush_get_next_buf(panel_handle), color_map, &dirty_area);
            flush_get_next_buf(panel_handle);
        } else {
            // 探测复制策略
            probe_result = flush_copy_probe(drv);

            if (probe_result == FLUSH_PROBE_FULL_COPY) {  // 需要全量复制
                flush_dirty_save(&dirty_area);  // 保存当前脏区域

                // 触发全量刷新（递归调用本函数）
                drv->full_refresh = 1;
                disp->rendering_in_progress = false;
                lv_disp_flush_ready(drv);  // 标记当前刷新完成
                lv_refr_now(_lv_refr_get_disp_refreshing());  // 强制全刷
            } else {  // 部分复制或跳过
                next_fb = flush_get_next_buf(panel_handle);
                flush_dirty_save(&dirty_area);  // 保存脏区域
                flush_dirty_copy(next_fb, color_map, &dirty_area);  // 复制脏区域

                // 切换缓冲区并显示
                esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, next_fb);

                // 等待显示完成
                ulTaskNotifyValueClear(NULL, ULONG_MAX);
                ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

                // 如果是部分复制，同步另一个缓冲区
                if (probe_result == FLUSH_PROBE_PART_COPY) {
                    flush_dirty_save(&dirty_area);
                    flush_dirty_copy(flush_get_next_buf(panel_handle), color_map, &dirty_area);
                    flush_get_next_buf(panel_handle);
                }
            }
        }
    }

    lv_disp_flush_ready(drv);  // 通知LVGL刷新完成
}

#else  // 屏幕无旋转时的刷新回调（简化版）

static void flush_callback(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t) drv->user_data;
    const int offsetx1 = area->x1;
    const int offsetx2 = area->x2;
    const int offsety1 = area->y1;
    const int offsety2 = area->y2;

    // 最后一个区域刷新时，切换缓冲区并等待显示完成
    if (lv_disp_flush_is_last(drv)) {
        esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, color_map);
        ulTaskNotifyValueClear(NULL, ULONG_MAX);
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    }

    lv_disp_flush_ready(drv);  // 通知LVGL刷新完成
}
#endif /* EXAMPLE_LVGL_PORT_ROTATION_DEGREE */

#elif LVGL_PORT_FULL_REFRESH && LVGL_PORT_LCD_RGB_BUFFER_NUMS == 2  // 全量刷新+2个缓冲区

static void flush_callback(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t) drv->user_data;
    const int offsetx1 = area->x1;
    const int offsetx2 = area->x2;
    const int offsety1 = area->y1;
    const int offsety2 = area->y2;

    /* 切换当前显示的帧缓冲区并输出到屏幕 */
    esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, color_map);

    /* 等待当前帧缓冲区传输完成（防撕裂） */
    ulTaskNotifyValueClear(NULL, ULONG_MAX);
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    lv_disp_flush_ready(drv); // 通知LVGL刷新完成
}
#elif LVGL_PORT_FULL_REFRESH && LVGL_PORT_LCD_RGB_BUFFER_NUMS == 3

#if EXAMPLE_LVGL_PORT_ROTATION_DEGREE == 0  // 无旋转时的三缓冲区管理
static void *lvgl_port_rgb_last_buf = NULL;  // 上一帧显示的缓冲区
static void *lvgl_port_rgb_next_buf = NULL;  // 下一帧要显示的缓冲区
static void *lvgl_port_flush_next_buf = NULL;  // 准备刷新的缓冲区
#endif

/**
 * @brief 三缓冲区模式下的刷新回调（全量刷新）
 */
void flush_callback(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t) drv->user_data;
    const int offsetx1 = area->x1;
    const int offsetx2 = area->x2;
    const int offsety1 = area->y1;
    const int offsety2 = area->y2;

#if EXAMPLE_LVGL_PORT_ROTATION_DEGREE != 0  // 有旋转时
    void *next_fb = get_next_frame_buffer(panel_handle);  // 获取下一个缓冲区

    /* 将LVGL渲染的数据旋转后复制到下一帧缓冲区 */
    rotate_copy_pixel((uint16_t *)color_map, next_fb, offsetx1, offsety1, offsetx2, offsety2, LV_HOR_RES, LV_VER_RES, EXAMPLE_LVGL_PORT_ROTATION_DEGREE);

    /* 切换屏幕显示的缓冲区 */
    esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, next_fb);
#else  // 无旋转时
    drv->draw_buf->buf1 = color_map;  // 绑定LVGL渲染缓冲区
    drv->draw_buf->buf2 = lvgl_port_flush_next_buf;  // 绑定下一刷新缓冲区
    lvgl_port_flush_next_buf = color_map;  // 更新刷新缓冲区

    /* 切换屏幕显示的缓冲区 */
    esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, color_map);

    lvgl_port_rgb_next_buf = color_map;  // 更新下一帧缓冲区
#endif

    lv_disp_flush_ready(drv);  // 通知LVGL刷新完成
}
#endif

#else  // 未启用防撕裂功能时的刷新回调
/*
作用：LVGL 的显示刷新回调函数，负责将渲染后的图像数据发送到 LCD 屏幕。
核心参数：
area：需要刷新的区域坐标（x1,y1,x2,y2）
color_map：LVGL 渲染后的像素数据缓冲区
依赖配置：
LVGL_PORT_AVOID_TEAR_ENABLE：是否启用防撕裂功能（通过多缓冲区同步实现）
LVGL_PORT_FULL_REFRESH：是否强制全屏幕刷新
EXAMPLE_LVGL_PORT_ROTATION_DEGREE：屏幕旋转角度（0/90/180/270 度）
调整建议：
若屏幕显示异常（如旋转角度不对），修改EXAMPLE_LVGL_PORT_ROTATION_DEGREE
若画面闪烁，启用LVGL_PORT_AVOID_TEAR_ENABLE并确保 VSYNC 信号正常
*/

/**
 * @brief 基础刷新回调（无防撕裂，直接复制数据）
 */
void flush_callback(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t) drv->user_data;
    const int offsetx1 = area->x1;
    const int offsetx2 = area->x2;
    const int offsety1 = area->y1;
    const int offsety2 = area->y2;

    /* 直接将LVGL渲染的数据复制到屏幕缓冲区 */
    esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, color_map);

    lv_disp_flush_ready(drv);  // 通知LVGL刷新完成
}

#endif /* LVGL_PORT_AVOID_TEAR_ENABLE */


/*
作用：初始化 LVGL 的显示设备
核心参数：
disp_drv.hor_res/ver_res：屏幕水平 / 垂直分辨率
disp_drv.flush_cb：刷新回调函数指针
依赖配置：
LVGL_PORT_H_RES/V_RES：屏幕分辨率宏定义
LVGL_PORT_FULL_REFRESH：全刷新模式开关
调整建议：
修改分辨率宏以匹配实际屏幕
若内存不足，减小LVGL_PORT_BUFFER_HEIGHT
*/

/* -------------------------- 显示设备初始化 -------------------------- */

/**
 * @brief 初始化LVGL显示设备（绑定屏幕参数和缓冲区）
 * @param panel_handle LCD面板句柄
 * @return LVGL显示设备句柄
 */
static lv_disp_t *display_init(esp_lcd_panel_handle_t panel_handle)
{
    assert(panel_handle);  // 确保LCD句柄有效

    static lv_disp_draw_buf_t disp_buf = { 0 };  // LVGL绘制缓冲区
    static lv_disp_drv_t disp_drv = { 0 };       // LVGL显示驱动

    // 分配LVGL使用的绘制缓冲区
    void *buf1 = NULL;  // 主缓冲区
    void *buf2 = NULL;  // 备用缓冲区（双缓冲时使用）
    int buffer_size = 0;  // 缓冲区大小（像素数）

    ESP_LOGD(TAG_LVGL, "为LVGL分配缓冲区内存");
#if LVGL_PORT_AVOID_TEAR_ENABLE  // 启用防撕裂时，缓冲区需与屏幕同尺寸
    buffer_size = LVGL_PORT_H_RES * LVGL_PORT_V_RES;  // 缓冲区大小 = 分辨率（全屏）
#if (LVGL_PORT_LCD_RGB_BUFFER_NUMS == 3) && (EXAMPLE_LVGL_PORT_ROTATION_DEGREE == 0) && LVGL_PORT_FULL_REFRESH
    // 三缓冲区+无旋转+全刷新：获取3个缓冲区
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_get_frame_buffer(panel_handle, 3, &lvgl_port_rgb_last_buf, &buf1, &buf2));
    lvgl_port_rgb_next_buf = lvgl_port_rgb_last_buf;  // 初始化缓冲区指针
    lvgl_port_flush_next_buf = buf2;
#elif (LVGL_PORT_LCD_RGB_BUFFER_NUMS == 3) && (EXAMPLE_LVGL_PORT_ROTATION_DEGREE != 0)
    // 三缓冲区+有旋转：3个缓冲区中1个用于渲染，2个用于显示
    void *fbs[3];
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_get_frame_buffer(panel_handle, 3, &fbs[0], &fbs[1], &fbs[2]));
    buf1 = fbs[2];  // 第3个缓冲区作为LVGL渲染区
#else
    // 双缓冲区：获取2个缓冲区
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_get_frame_buffer(panel_handle, 2, &buf1, &buf2));
#endif
#else  // 未启用防撕裂时，可使用小缓冲区（节省内存）
    // 缓冲区高度为屏幕高度的一部分（如1/10），降低内存占用
    buffer_size = LVGL_PORT_H_RES * LVGL_PORT_BUFFER_HEIGHT;
    // 分配内存（带DMA属性，确保LCD控制器可直接访问）
    buf1 = heap_caps_malloc(buffer_size * sizeof(lv_color_t), LVGL_PORT_BUFFER_MALLOC_CAPS);
    assert(buf1);  // 确保内存分配成功
    ESP_LOGI(TAG_LVGL, "LVGL缓冲区大小: %dKB", buffer_size * sizeof(lv_color_t) / 1024);
#endif /* LVGL_PORT_AVOID_TEAR_ENABLE */

    // 初始化LVGL绘制缓冲区
    lv_disp_draw_buf_init(&disp_buf, buf1, buf2, buffer_size);

    ESP_LOGD(TAG_LVGL, "向LVGL注册显示驱动");
    lv_disp_drv_init(&disp_drv);  // 初始化显示驱动

    // 根据旋转角度设置屏幕分辨率（90/270度旋转时，宽高互换）
#if EXAMPLE_LVGL_PORT_ROTATION_90 || EXAMPLE_LVGL_PORT_ROTATION_270
    disp_drv.hor_res = LVGL_PORT_V_RES;  // 水平分辨率 = 原垂直分辨率
    disp_drv.ver_res = LVGL_PORT_H_RES;  // 垂直分辨率 = 原水平分辨率
#else
    disp_drv.hor_res = LVGL_PORT_H_RES;  // 水平分辨率
    disp_drv.ver_res = LVGL_PORT_V_RES;  // 垂直分辨率
#endif

    disp_drv.flush_cb = flush_callback;  // 绑定刷新回调函数
    disp_drv.draw_buf = &disp_buf;       // 绑定绘制缓冲区
    disp_drv.user_data = panel_handle;   // 传递LCD句柄到回调函数

#if LVGL_PORT_FULL_REFRESH
    disp_drv.full_refresh = 1;  // 启用全量刷新
#elif LVGL_PORT_DIRECT_MODE
    disp_drv.direct_mode = 1;   // 启用直接模式（数据直接输出）
#endif

    return lv_disp_drv_register(&disp_drv);  // 注册显示驱动并返回句柄
}

/*
作用：读取触摸屏坐标数据，并转换为 LVGL 能识别的格式。
核心参数：
data->point.x/y：触摸点坐标
data->state：触摸状态（按下 / 释放）
依赖配置：
esp_lcd_touch_get_coordinates()：底层触摸驱动 API
旋转相关宏：EXAMPLE_LVGL_PORT_ROTATION_90/180/270
调整建议：
若触摸位置偏移，检查esp_lcd_touch_set_swap_xy()和esp_lcd_touch_set_mirror_x/y()参数
若触摸无响应，确认触摸芯片型号及 I2C 地址是否正确
*/
/* -------------------------- 触摸输入处理 -------------------------- */

/**
 * @brief 读取触摸屏数据并转换为LVGL格式
 * @param indev_drv LVGL输入设备驱动
 * @param data 存储触摸数据的结构体
 */
static void touchpad_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data)
{
    esp_lcd_touch_handle_t tp = (esp_lcd_touch_handle_t)indev_drv->user_data;  // 触摸芯片句柄
    assert(tp);  // 确保触摸句柄有效

    uint16_t touchpad_x;  // 触摸X坐标
    uint16_t touchpad_y;  // 触摸Y坐标
    uint8_t touchpad_cnt = 0;  // 触摸点数量

    /* 从触摸芯片读取原始数据 */
    esp_lcd_touch_read_data(tp);

    /* 解析触摸坐标（最多读取1个触摸点） */
    bool touchpad_pressed = esp_lcd_touch_get_coordinates(tp, &touchpad_x, &touchpad_y, NULL, &touchpad_cnt, 1);

    if (touchpad_pressed && touchpad_cnt > 0) {  // 有触摸
        data->point.x = touchpad_x;  // 传递X坐标
        data->point.y = touchpad_y;  // 传递Y坐标
        data->state = LV_INDEV_STATE_PRESSED;  // 标记为按下状态
        ESP_LOGD(TAG_LVGL, "触摸位置: %d,%d", touchpad_x, touchpad_y);

        // 熄屏唤醒：如果当前黑屏，先亮屏
        if (!screen_is_on) {
            wavesahre_rgb_lcd_bl_on();
            screen_is_on = true;
        }
        last_touch_time_ms = lv_tick_get();  // 更新最后触摸时间
    } else {  // 无触摸
        data->state = LV_INDEV_STATE_RELEASED;  // 标记为释放状态
    }
}

// 屏幕超时熄屏定时器回调
static void screen_timeout_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (!screen_is_on) return;  // 已经熄屏了，不再处理

    uint32_t idle_time = lv_tick_get() - last_touch_time_ms;
    if (idle_time >= screen_timeout_ms) {   // 超过配置的熄屏时间
        ESP_LOGI(TAG_LVGL, "%lu 秒无触摸，自动熄屏", (unsigned long)(screen_timeout_ms / 1000));
        wavesahre_rgb_lcd_bl_off();
        screen_is_on = false;
    }
}

// 设置熄屏超时时间(秒),供设置界面调用
void lvgl_port_set_screen_timeout(int seconds)
{
    screen_timeout_ms = seconds * 1000;
}

// 获取当前熄屏超时时间(秒)
int lvgl_port_get_screen_timeout(void)
{
    return (int)(screen_timeout_ms / 1000);
}

/**
 * @brief 初始化LVGL输入设备（触摸屏）
 * @param tp 触摸芯片句柄
 * @return LVGL输入设备句柄
 */
static lv_indev_t *indev_init(esp_lcd_touch_handle_t tp)
{
    assert(tp);  // 确保触摸句柄有效

    static lv_indev_drv_t indev_drv_tp;  // LVGL输入设备驱动

    /* 初始化输入设备驱动 */
    lv_indev_drv_init(&indev_drv_tp);
    indev_drv_tp.type = LV_INDEV_TYPE_POINTER;  // 类型为指针（触摸屏）
    indev_drv_tp.read_cb = touchpad_read;      // 绑定触摸读取函数
    indev_drv_tp.user_data = tp;               // 传递触摸句柄到读取函数

    return lv_indev_drv_register(&indev_drv_tp);  // 注册输入设备
}


/* -------------------------- 系统时钟与任务 -------------------------- */

/**
 * @brief 周期性递增LVGL时钟（用于动画和定时器）
 * @param arg 未使用
 */
static void tick_increment(void *arg)
{
    /* 通知LVGL经过的毫秒数 */
    lv_tick_inc(LVGL_PORT_TICK_PERIOD_MS);
}

/**
 * @brief 初始化LVGL时钟（基于ESP32定时器）
 * @return 成功返回ESP_OK，失败返回错误码
 */
static esp_err_t tick_init(void)
{
    // 配置定时器参数（周期性触发）
    const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = &tick_increment,  // 定时回调函数
        .name = "LVGL tick"           // 定时器名称
    };
    esp_timer_handle_t lvgl_tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer));  // 创建定时器

    // 启动定时器（周期：LVGL_PORT_TICK_PERIOD_MS毫秒）
    return esp_timer_start_periodic(lvgl_tick_timer, LVGL_PORT_TICK_PERIOD_MS * 1000);  // 单位：微秒
}

/**
 * @brief LVGL主任务（处理渲染和事件）
 * @param arg 未使用
 */
static void lvgl_port_task(void *arg)
{
    ESP_LOGD(TAG_LVGL, "启动LVGL任务");

    uint32_t task_delay_ms = LVGL_PORT_TASK_MAX_DELAY_MS;  // 初始延迟时间
    while (1) {
        if (lvgl_port_lock(-1)) {  // 获取LVGL锁（-1表示无限等待）
            task_delay_ms = lv_timer_handler();  // 处理LVGL定时器和渲染
            lvgl_port_unlock();  // 释放锁
        }

        // 限制延迟时间在合理范围（避免任务长时间阻塞或频繁唤醒）
        if (task_delay_ms > LVGL_PORT_TASK_MAX_DELAY_MS) {
            task_delay_ms = LVGL_PORT_TASK_MAX_DELAY_MS;
        } else if (task_delay_ms < LVGL_PORT_TASK_MIN_DELAY_MS) {
            task_delay_ms = LVGL_PORT_TASK_MIN_DELAY_MS;
        }

        vTaskDelay(pdMS_TO_TICKS(task_delay_ms));  // 延迟等待
    }
}


/* -------------------------- 适配层总初始化 -------------------------- */

/**
 * @brief 初始化LVGL适配层（显示+触摸+任务）
 * @param lcd_handle LCD面板句柄
 * @param tp_handle 触摸芯片句柄（NULL表示不使用触摸）
 * @return 成功返回ESP_OK，失败返回错误码
 */
esp_err_t lvgl_port_init(esp_lcd_panel_handle_t lcd_handle, esp_lcd_touch_handle_t tp_handle)
{
    lv_init();  // 初始化LVGL库
    ESP_ERROR_CHECK(tick_init());  // 初始化LVGL时钟

    // 初始化显示设备
    lv_disp_t *disp = display_init(lcd_handle);
    assert(disp);  // 确保显示初始化成功

    // 如果有触摸设备，初始化触摸输入
    if (tp_handle) {
        lv_indev_t *indev = indev_init(tp_handle);
        assert(indev);  // 确保触摸初始化成功
        g_lvgl_indev = indev;  // 保存到全局变量供外部(如光标绑定)使用
        g_lvgl_indev->driver->long_press_time = 1000;  // 长按触发时间设为 1000ms(1秒)

        // 根据屏幕旋转调整触摸坐标（保证触摸位置与显示匹配）
#if EXAMPLE_LVGL_PORT_ROTATION_90
        esp_lcd_touch_set_swap_xy(tp_handle, true);  // 90度旋转：交换X/Y坐标
        esp_lcd_touch_set_mirror_y(tp_handle, true);  // 镜像Y轴
#elif EXAMPLE_LVGL_PORT_ROTATION_180
        esp_lcd_touch_set_mirror_x(tp_handle, true);  // 180度旋转：镜像X轴
        esp_lcd_touch_set_mirror_y(tp_handle, true);  // 镜像Y轴
#elif EXAMPLE_LVGL_PORT_ROTATION_270
        esp_lcd_touch_set_swap_xy(tp_handle, true);  // 270度旋转：交换X/Y坐标
        esp_lcd_touch_set_mirror_x(tp_handle, true);  // 镜像X轴
#endif
    }

    // 创建LVGL互斥锁（递归锁，支持嵌套调用）
    lvgl_mux = xSemaphoreCreateRecursiveMutex();
    assert(lvgl_mux);  // 确保锁创建成功

    ESP_LOGI(TAG_LVGL, "创建LVGL任务");
    // 确定任务运行的CPU核心（-1表示不指定）
    BaseType_t core_id = (LVGL_PORT_TASK_CORE < 0) ? tskNO_AFFINITY : LVGL_PORT_TASK_CORE;
    // 创建LVGL主任务
    BaseType_t ret = xTaskCreatePinnedToCore(lvgl_port_task, "lvgl", 
                                             LVGL_PORT_TASK_STACK_SIZE, NULL,
                                             LVGL_PORT_TASK_PRIORITY, &lvgl_task_handle, core_id);
    if (ret != pdPASS) {
        ESP_LOGE(TAG_LVGL, "创建LVGL任务失败");
        return ESP_FAIL;
    }

    // 初始化时间戳并创建超时检测定时器（每500ms检查一次）
    last_touch_time_ms = lv_tick_get();
    lv_timer_create(screen_timeout_timer_cb, 500, NULL);

    // 从 SPIFFS 加载熄屏时间配置
    int timeout_sec = screen_timeout_load();
    screen_timeout_ms = timeout_sec * 1000;
    ESP_LOGI(TAG_LVGL, "熄屏时间: %d 秒", timeout_sec);

    return ESP_OK;
}


/* -------------------------- 线程安全与VSYNC同步 -------------------------- */

/**
 * @brief 锁定LVGL（保证线程安全）
 * @param timeout_ms 超时时间（毫秒，-1表示无限等待）
 * @return 成功返回true，超时返回false
 */
bool lvgl_port_lock(int timeout_ms)
{
    assert(lvgl_mux && "需先调用lvgl_port_init初始化");

    // 转换超时时间为FreeRTOS ticks
    const TickType_t timeout_ticks = (timeout_ms < 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    // 获取递归锁
    return xSemaphoreTakeRecursive(lvgl_mux, timeout_ticks) == pdTRUE;
}

void lvgl_port_unlock(void)
{
    assert(lvgl_mux && "lvgl_port_init must be called first"); // Ensure the mutex is initialized
    xSemaphoreGiveRecursive(lvgl_mux); // Release the mutex
}

bool lvgl_port_notify_rgb_vsync(void)
{
    BaseType_t need_yield = pdFALSE; // Flag to check if a yield is needed
#if LVGL_PORT_FULL_REFRESH && (LVGL_PORT_LCD_RGB_BUFFER_NUMS == 3) && (EXAMPLE_LVGL_PORT_ROTATION_DEGREE == 0)
    if (lvgl_port_rgb_next_buf != lvgl_port_rgb_last_buf) {
        lvgl_port_flush_next_buf = lvgl_port_rgb_last_buf; // Set next buffer for flushing
        lvgl_port_rgb_last_buf = lvgl_port_rgb_next_buf; // Update the last buffer
    }
#elif LVGL_PORT_AVOID_TEAR_ENABLE
    // Notify that the current RGB frame buffer has been transmitted
    xTaskNotifyFromISR(lvgl_task_handle, ULONG_MAX, eNoAction, &need_yield); // Notify the LVGL task
#endif
    return (need_yield == pdTRUE); // Return whether a yield is needed
}

void lvgl_port_force_screen_off(void)
{
    if (screen_is_on) {
        wavesahre_rgb_lcd_bl_off();
        screen_is_on = false;
        ESP_LOGI(TAG_LVGL, "主动熄屏");
    }
}