/*
 * 界面6: 绘图应用 (二进制存储版)
 * 滑动屏幕画线,支持保存/清除,断电后可恢复
 */

#include "ui_Screen6.h"
#include "../ui.h"
#include "../../spiffs.h"
#include "../../lvgl_port.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "../../buzzer.h"          // 蜂鸣器驱动

// 画布尺寸
#define CANVAS_W 480
#define CANVAS_H 320

// 绘图文件路径
#define DRAWING_FILE "/spiffs/drawing.bin"

// 调色板颜色数量(黑、红、绿、蓝、黄、橙)
#define PALETTE_COLOR_COUNT 6

// 文件头: 12 字节
// [0..3] magic "DRAW"
// [4..5] version (little-endian uint16)
// [6..7] width  (little-endian uint16)
// [8..9] height (little-endian uint16)
// [10..11] reserved

lv_obj_t *ui_Screen6 = NULL;
lv_obj_t *ui_exitbtu6 = NULL;
lv_obj_t *ui_canvas_draw = NULL;
lv_obj_t *ui_btn_save_draw = NULL;
lv_obj_t *ui_btn_clear_draw = NULL;

static lv_color_t *canvas_buf = NULL;
static lv_point_t last_point = {-1, -1};

// 保存状态（防止重复保存）
static volatile bool is_saving = false;

// 当前画笔颜色（默认黑色）
static lv_color_t current_color = {0}; // 初始化为黑色（全0）

// 当前选中的颜色按钮（用于取消高亮）
static lv_obj_t *selected_color_btn = NULL;

// 调色板颜色表（放在静态存储里：颜色按钮的 user_data 直接指向表中元素，
// 不再逐个动态申请内存，也就不存在"退出界面时漏释放"的问题）
static lv_color_t s_palette[PALETTE_COLOR_COUNT];

// --------------------- 二进制绘图文件读写 ---------------------

/**
 * @brief 保存画布为二进制格式（只存非白色像素）
 *        文件头包含魔数、版本、尺寸，便于校验
 *        使用 8KB 缓冲区批量写入，速度快，不阻塞 UI
 */
static bool drawing_save_impl(const lv_color_t *buf)
{
    FILE *fp = fopen(DRAWING_FILE, "wb");
    if (fp == NULL)
    {
        ESP_LOGE("DRAW", "无法创建绘图文件");
        return false;
    }

    // 写文件头
    uint8_t header[12] = {
        'D', 'R', 'A', 'W',
        0x01, 0x00, // version = 1
        (CANVAS_W & 0xFF), (CANVAS_W >> 8) & 0xFF,
        (CANVAS_H & 0xFF), (CANVAS_H >> 8) & 0xFF,
        0x00, 0x00 // reserved
    };
    fwrite(header, 1, 12, fp);

    // 8KB 输出缓冲区，批量 fwrite，极少触碰 Flash
    uint8_t *wbuf = (uint8_t *)malloc(8192);
    if (wbuf == NULL)
    {
        fclose(fp);
        return false;
    }

    int wp = 0;
    int count = 0;

    for (int y = 0; y < CANVAS_H; y++)
    {
        for (int x = 0; x < CANVAS_W; x++)
        {
            lv_color_t c = buf[y * CANVAS_W + x];
            if (c.full != 0xFFFF)
            { // 非白像素才存
                // 小端序写入: x(2) + y(2) + color(2)
                wbuf[wp++] = x & 0xFF;
                wbuf[wp++] = (x >> 8) & 0xFF;
                wbuf[wp++] = y & 0xFF;
                wbuf[wp++] = (y >> 8) & 0xFF;
                wbuf[wp++] = c.full & 0xFF;
                wbuf[wp++] = (c.full >> 8) & 0xFF;
                count++;

                // 缓冲区快满时一次性写入
                if (wp > 8192 - 64)
                {
                    fwrite(wbuf, 1, wp, fp);
                    wp = 0;
                }
            }
        }
        // 每 40 行让出 CPU，防止后台任务饿死 LVGL
        if ((y % 40) == 0)
        {
            vTaskDelay(1);
        }
    }

    if (wp > 0)
    {
        fwrite(wbuf, 1, wp, fp);
    }

    free(wbuf);
    fclose(fp);
    ESP_LOGI("DRAW", "二进制保存 %d 个像素", count);
    return true;
}

/**
 * @brief 从二进制文件加载绘图数据
 *        先校验文件头（魔数、版本、尺寸），再逐像素恢复
 */
static bool drawing_load(lv_color_t *buf, int buf_size)
{
    if (buf == NULL)
        return false;
    FILE *fp = fopen(DRAWING_FILE, "rb");
    if (fp == NULL)
    {
        // 文件不存在，不算错误，保持白色背景
        return false;
    }

    uint8_t header[12];
    if (fread(header, 1, 12, fp) != 12)
    {
        fclose(fp);
        return false;
    }

    // 魔数校验
    if (header[0] != 'D' || header[1] != 'R' || header[2] != 'A' || header[3] != 'W')
    {
        ESP_LOGW("DRAW", "绘图文件头不匹配，忽略旧文件");
        fclose(fp);
        return false;
    }

    // 尺寸校验（防止换分辨率后读错）
    uint16_t f_w = header[6] | (header[7] << 8);
    uint16_t f_h = header[8] | (header[9] << 8);
    if (f_w != CANVAS_W || f_h != CANVAS_H)
    {
        ESP_LOGW("DRAW", "画布尺寸不匹配(%dx%d)，忽略旧文件", f_w, f_h);
        fclose(fp);
        return false;
    }

    // 先刷白底
    memset(buf, 0xFF, buf_size);

    uint8_t pixel[6];
    int loaded = 0;
    while (fread(pixel, 1, 6, fp) == 6)
    {
        uint16_t x = pixel[0] | (pixel[1] << 8);
        uint16_t y = pixel[2] | (pixel[3] << 8);
        uint16_t color = pixel[4] | (pixel[5] << 8);
        if (x < CANVAS_W && y < CANVAS_H)
        {
            buf[y * CANVAS_W + x].full = color;
            loaded++;
        }
    }

    fclose(fp);
    ESP_LOGI("DRAW", "二进制加载 %d 个像素", loaded);
    return true;
}

// --------------------- 后台保存任务 ---------------------

/**
 * @brief 后台保存任务，执行实际的文件写入
 *        完成后自动销毁任务，并弹出结果提示框
 */
static void save_drawing_task(void *pv)
{
    lv_color_t *buf_copy = (lv_color_t *)pv;
    bool ok = drawing_save_impl(buf_copy);
    free(buf_copy);

    // 回到 LVGL 线程安全地更新 UI
    if (lvgl_port_lock(-1))
    {
        if (ui_Screen6)
        { // 确保屏幕没被销毁
            if (ok)
                show_message_box("提示", "保存成功!");
            else
                show_message_box("错误", "保存失败!");
        }
        // is_saving = false;
        lvgl_port_unlock();
    }
    vTaskDelete(NULL);
}

// --------------------- 颜色选择回调 ---------------------

/**
 * @brief 颜色按钮点击事件：切换画笔颜色，高亮当前选中
 */
static void color_btn_click_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    lv_color_t color = *(lv_color_t *)lv_obj_get_user_data(btn);

    // 取消上一个选中的高亮
    if (selected_color_btn)
    {
        lv_obj_set_style_border_width(selected_color_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    }

    // 高亮当前按钮（添加白色边框）
    lv_obj_set_style_border_width(btn, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(btn, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);

    selected_color_btn = btn;
    current_color = color;
}

// --------------------- 触摸画线 ---------------------

/**
 * @brief 画布触摸事件回调：按下画点，拖动连线，松开重置
 */
static void canvas_draw_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *canvas = lv_event_get_target(e);
    lv_indev_t *indev = lv_indev_get_act();
    if (indev == NULL)
        return;

    lv_point_t point;
    lv_indev_get_point(indev, &point);

    lv_area_t canvas_coords;
    lv_obj_get_coords(canvas, &canvas_coords);

    lv_coord_t x = point.x - canvas_coords.x1;
    lv_coord_t y = point.y - canvas_coords.y1;
    if (x < 0 || x >= CANVAS_W || y < 0 || y >= CANVAS_H)
    {
        last_point.x = -1;
        return;
    }

    if (code == LV_EVENT_PRESSED)
    {
        // 按下:画一个点（使用当前颜色）
        lv_canvas_set_px(canvas, x, y, current_color);
        last_point.x = x;
        last_point.y = y;
    }
    else if (code == LV_EVENT_PRESSING)
    {
        // 拖动:画线连接 last_point -> (x,y)（使用当前颜色）
        if (last_point.x >= 0)
        {
            lv_point_t points[2] = {last_point, {x, y}};
            lv_draw_line_dsc_t line_dsc;
            lv_draw_line_dsc_init(&line_dsc);
            line_dsc.color = current_color;
            line_dsc.width = 2;
            line_dsc.round_start = 1;
            line_dsc.round_end = 1;
            lv_canvas_draw_line(canvas, points, 2, &line_dsc);
        }
        last_point.x = x;
        last_point.y = y;
    }
    else if (code == LV_EVENT_RELEASED)
    {
        // 松开:重置 last_point
        last_point.x = -1;
    }
}

// --------------------- 按钮回调 ---------------------

/**
 * @brief 保存按钮：在后台任务中保存画布，避免阻塞 UI
 */
static void btn_save_draw_cb(lv_event_t *e)
{
    (void)e;
    if (canvas_buf == NULL || is_saving)
        return;

    int buf_size = CANVAS_W * CANVAS_H * sizeof(lv_color_t);

    // 快速复制画布快照（memcpy 300KB 只需约 1~2ms）
    lv_color_t *buf_copy = (lv_color_t *)malloc(buf_size);
    if (buf_copy == NULL)
    {
        show_message_box("错误", "内存不足，无法保存!");
        return;
    }
    memcpy(buf_copy, canvas_buf, buf_size);

    is_saving = true;

    // 启动后台任务执行耗时写入，主线程立即返回，UI 不被阻塞
    xTaskCreate(save_drawing_task, "save_draw", 4096, buf_copy, 5, NULL);
}

/**
 * @brief 清除按钮：重置画布为白色，并删除绘图文件
 */
static void btn_clear_draw_cb(lv_event_t *e)
{
    (void)e;
    if (ui_canvas_draw == NULL)
        return;
    lv_canvas_fill_bg(ui_canvas_draw, lv_color_hex(0xFFFFFF), LV_OPA_COVER);
    last_point.x = -1;
    // remove(DRAWING_FILE);
}

// 未保存确认消息框按钮回调
static void unsaved_msgbox_cb(lv_event_t *e)
{
    // 【关键修复】current_target 才是注册回调的消息框对象本身
    // 不要用 lv_obj_get_parent(lv_event_get_target(e))，那会拿到 content 容器
    lv_obj_t *mbox = lv_event_get_current_target(e);
    const char *btn_text = lv_msgbox_get_active_btn_text(mbox);

    if (btn_text && strcmp(btn_text, "不保存") == 0)
    {
        lv_msgbox_close(mbox);
        // 回桌面。ui_Screen3 在开机 ui_init() 时就已经建好了,
        // 这里不能无条件再调 ui_Screen3_screen_init():
        // 那会新建一整块 Screen3 并把旧对象丢掉,既泄漏界面对象,
        // 又会让每进出一次绘图页就多出一个时间刷新定时器和一个顶层键盘。
        if (ui_Screen3 == NULL) {
            ui_Screen3_screen_init();
        }
        // 用不带过场动画的 lv_scr_load 立即切换,切换是同步完成的,
        // 所以紧接着销毁 Screen6 不会误删动画里还被引用的旧屏幕。
        lv_scr_load(ui_Screen3);
        ui_Screen6_screen_destroy();
    }
    else
    {
        // 取消：只关闭消息框，保留编辑状态
        lv_msgbox_close(mbox);
    }
}

/**
 * @brief 退出按钮：返回桌面(界面3)
 */
void ui_event_exitbtu6(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED)
    {
        if (is_saving)
        {
            // 回桌面(同上:Screen3 已存在时不要重复初始化)
            if (ui_Screen3 == NULL) {
                ui_Screen3_screen_init();
            }
            lv_scr_load(ui_Screen3);
            ui_Screen6_screen_destroy();
            is_saving = false;
        }
        else
        {
            // 文件未保存关闭时，蜂鸣器响一声
            buzzer_beep();

            // 有未保存的修改,弹出确认框
            static const char *btns[] = {"不保存", "取消", ""};
            lv_obj_t *mbox = lv_msgbox_create(NULL, "提示", "画布未保存,是否放弃修改?", btns, false);
            lv_obj_set_style_text_font(mbox, &ui_font_Font1, LV_PART_MAIN);
            lv_obj_center(mbox);
            lv_obj_add_event_cb(mbox, unsaved_msgbox_cb, LV_EVENT_CLICKED, NULL);
        }
        return;
    }
}

// --------------------- 界面初始化/销毁 ---------------------

void ui_Screen6_screen_init(void)
{
    ui_Screen6 = lv_obj_create(NULL);

    // 设置背景色
    lv_obj_set_style_bg_color(ui_Screen6, lv_color_hex(0xF0F0F0), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_Screen6, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    // 初始化当前颜色为黑色
    current_color = lv_color_hex(0x000000);

    // 标题
    lv_obj_t *title = lv_label_create(ui_Screen6);
    lv_label_set_text(title, "绘图应用");
    lv_obj_set_style_text_font(title, &ui_font_Font1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    // 退出按钮(右上角)
    ui_exitbtu6 = lv_btn_create(ui_Screen6);
    lv_obj_set_width(ui_exitbtu6, 80);
    lv_obj_set_height(ui_exitbtu6, 40);
    lv_obj_align(ui_exitbtu6, LV_ALIGN_TOP_RIGHT, -10, 5);
    lv_obj_t *lbl_exit = lv_label_create(ui_exitbtu6);
    lv_label_set_text(lbl_exit, "退出");
    lv_obj_set_style_text_font(lbl_exit, &ui_font_Font1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_center(lbl_exit);
    lv_obj_add_event_cb(ui_exitbtu6, ui_event_exitbtu6, LV_EVENT_ALL, NULL);

    // --------------------- 颜色选择器（画布上方） ---------------------
    // 六色调色板:黑、红、绿、蓝、黄、橙
    static const uint32_t palette_hex[PALETTE_COLOR_COUNT] = {
        0x000000, // 黑
        0xFF0000, // 红
        0x00FF00, // 绿
        0x0000FF, // 蓝
        0xFFFF00, // 黄
        0xFF8000  // 橙
    };
    // 换算成 LVGL 颜色值填入静态颜色表(用户可切换的只有这 6 种颜色)
    for (int i = 0; i < PALETTE_COLOR_COUNT; i++) {
        s_palette[i] = lv_color_hex(palette_hex[i]);
    }

    // 创建颜色选择容器（水平排列）
    lv_obj_t *color_container = lv_obj_create(ui_Screen6);
    lv_obj_set_size(color_container, 300, 40); // 宽度足够容纳6个30px方块+间距
    lv_obj_align(color_container, LV_ALIGN_TOP_MID, 0, 50);
    lv_obj_set_style_bg_opa(color_container, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(color_container, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(color_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(color_container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(color_container, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(color_container, 8, LV_PART_MAIN);

    // 创建颜色按钮
    for (int i = 0; i < PALETTE_COLOR_COUNT; i++)
    {
        lv_obj_t *btn = lv_btn_create(color_container);
        lv_obj_set_size(btn, 30, 30);
        lv_obj_set_style_bg_color(btn, s_palette[i], LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_radius(btn, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        // 颜色值挂在按钮的 user_data 上,指向静态颜色表(无动态分配、无需释放)
        lv_obj_set_user_data(btn, &s_palette[i]);
        lv_obj_add_event_cb(btn, color_btn_click_cb, LV_EVENT_CLICKED, NULL);

        // 默认选中黑色（第一个）
        if (i == 0)
        {
            lv_obj_set_style_border_width(btn, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_color(btn, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
            selected_color_btn = btn;
        }
    }

    // 画布缓冲(优先用 PSRAM,约 307KB)
    int buf_size = CANVAS_W * CANVAS_H * sizeof(lv_color_t);
    canvas_buf = (lv_color_t *)heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (canvas_buf == NULL)
    {
        canvas_buf = (lv_color_t *)malloc(buf_size);
    }
    if (canvas_buf == NULL)
    {
        lv_obj_t *warn = lv_label_create(ui_Screen6);
        lv_label_set_text(warn, "内存不足,无法创建画布");
        lv_obj_center(warn);
        return;
    }

    // 创建画布
    ui_canvas_draw = lv_canvas_create(ui_Screen6);
    lv_canvas_set_buffer(ui_canvas_draw, canvas_buf, CANVAS_W, CANVAS_H, LV_IMG_CF_TRUE_COLOR);
    lv_canvas_fill_bg(ui_canvas_draw, lv_color_hex(0xFFFFFF), LV_OPA_COVER);
    // 画布居中，Y方向下移30像素以避开颜色选择器
    lv_obj_align(ui_canvas_draw, LV_ALIGN_CENTER, 0, 40);
    lv_obj_clear_flag(ui_canvas_draw, LV_OBJ_FLAG_SCROLLABLE); // 不可滚动(否则长按变成滚动)
    lv_obj_add_flag(ui_canvas_draw, LV_OBJ_FLAG_CLICKABLE);

    // 尝试加载上次保存的绘图
    drawing_load(canvas_buf, buf_size);
    // 触发 canvas 重绘以显示加载的内容
    lv_obj_invalidate(ui_canvas_draw);

    // 注册画布触摸事件
    lv_obj_add_event_cb(ui_canvas_draw, canvas_draw_event_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(ui_canvas_draw, canvas_draw_event_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(ui_canvas_draw, canvas_draw_event_cb, LV_EVENT_RELEASED, NULL);

    // 保存按钮(左下)
    ui_btn_save_draw = lv_btn_create(ui_Screen6);
    lv_obj_set_width(ui_btn_save_draw, 100);
    lv_obj_set_height(ui_btn_save_draw, 40);
    lv_obj_align(ui_btn_save_draw, LV_ALIGN_BOTTOM_LEFT, 10, -10);
    lv_obj_t *lbl_save = lv_label_create(ui_btn_save_draw);
    lv_label_set_text(lbl_save, "保存");
    lv_obj_set_style_text_font(lbl_save, &ui_font_Font1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_center(lbl_save);
    lv_obj_add_event_cb(ui_btn_save_draw, btn_save_draw_cb, LV_EVENT_CLICKED, NULL);

    // 清除按钮(右下)
    ui_btn_clear_draw = lv_btn_create(ui_Screen6);
    lv_obj_set_width(ui_btn_clear_draw, 100);
    lv_obj_set_height(ui_btn_clear_draw, 40);
    lv_obj_align(ui_btn_clear_draw, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
    lv_obj_t *lbl_clear = lv_label_create(ui_btn_clear_draw);
    lv_label_set_text(lbl_clear, "清除");
    lv_obj_set_style_text_font(lbl_clear, &ui_font_Font1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_center(lbl_clear);
    lv_obj_add_event_cb(ui_btn_clear_draw, btn_clear_draw_cb, LV_EVENT_CLICKED, NULL);
}

void ui_Screen6_screen_destroy(void)
{
    if (ui_Screen6 == NULL)
        return;

    // 防止后台保存任务往一个正在销毁的界面上弹提示框:
    // 先把标志清掉、把全局指针摘走,任务回来时看到 ui_Screen6 == NULL 就不弹了。
    // (保存任务与这里都在 LVGL 锁内执行,不会真正并发)
    is_saving = false;
    lv_obj_t *scr = ui_Screen6;
    ui_Screen6 = NULL;

    // 【顺序很关键】先删界面对象树,再释放画布缓冲。
    // 画布对象内部保存着 canvas_buf 的指针,如果先 free 再删对象,
    // 中间这段时间画布就是一块悬空指针。
    lv_obj_del(scr);

    // 释放画布缓冲(注意:lv_canvas_set_buffer 后 LVGL 不拥有 buffer,需自己释放)
    if (canvas_buf != NULL)
    {
        free(canvas_buf);
        canvas_buf = NULL;
    }

    // 清理其余全局状态
    last_point.x = -1;
    selected_color_btn = NULL;

    // NULL screen variables
    ui_exitbtu6 = NULL;
    ui_canvas_draw = NULL;
    ui_btn_save_draw = NULL;
    ui_btn_clear_draw = NULL;
}