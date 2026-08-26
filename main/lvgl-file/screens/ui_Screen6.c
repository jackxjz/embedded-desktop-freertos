/*
 * 界面6: 绘图应用
 * 滑动屏幕画线,支持保存/清除,断电后可恢复
 */

#include "ui_Screen6.h"
#include "../ui.h"
#include "../../spiffs.h"
#include "../../lvgl_port.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"   // 用于 PSRAM 分配

// 画布尺寸(居中显示在 800x480 屏幕上)
#define CANVAS_W   480
#define CANVAS_H   320
#define CANVAS_BPP 16            // RGB565:16 bits per pixel

// 绘图文件路径
#define DRAWING_FILE "/spiffs/drawing.bin"

lv_obj_t * ui_Screen6 = NULL;
lv_obj_t * ui_exitbtu6 = NULL;
lv_obj_t * ui_canvas_draw = NULL;
lv_obj_t * ui_btn_save_draw = NULL;
lv_obj_t * ui_btn_clear_draw = NULL;

// 画布像素缓冲(用 PSRAM 分配,约 300KB)
static lv_color_t *canvas_buf = NULL;

// 上一次触摸点(用于画连续线段),-1 表示无效
static lv_point_t last_point = {-1, -1};

// --------------------- 绘图文件读写 ---------------------

// 加载绘图文件到 canvas_buf
// 保存画布：只记录非白色像素 (颜色值 != 0xFFFF)
static bool drawing_save(const lv_color_t *buf, int buf_size)
{
    if (buf == NULL) return false;
    FILE *fp = fopen(DRAWING_FILE, "w");
    if (fp == NULL) {
        ESP_LOGE("DRAW", "无法创建绘图文件");
        return false;
    }

    int count = 0;
    for (int y = 0; y < CANVAS_H; y++) {
        for (int x = 0; x < CANVAS_W; x++) {
            lv_color_t c = buf[y * CANVAS_W + x];
            // RGB565 白色 = 0xFFFF
            if (c.full != 0xFFFF) {
                fprintf(fp, "%d,%d,%04X\n", x, y, c.full);
                count++;
            }
        }
    }
    fclose(fp);
    ESP_LOGI("DRAW", "保存了 %d 个非空白像素", count);
    return true;
}

// 加载画布：先刷白，再根据坐标逐点恢复
static bool drawing_load(lv_color_t *buf, int buf_size)
{
    if (buf == NULL) return false;
    FILE *fp = fopen(DRAWING_FILE, "r");
    if (fp == NULL) {
        // 文件不存在，不算错误，保持白色背景
        return false;
    }

    // 先全部填充白色 (0xFFFF)
    memset(buf, 0xFF, buf_size);

    int x, y;
    unsigned int color;
    int loaded = 0;
    while (fscanf(fp, "%d,%d,%04X", &x, &y, &color) == 3) {
        if (x >= 0 && x < CANVAS_W && y >= 0 && y < CANVAS_H) {
            buf[y * CANVAS_W + x].full = (uint16_t)color;
            loaded++;
        }
    }
    fclose(fp);
    ESP_LOGI("DRAW", "加载了 %d 个像素", loaded);
    return true;
}

// --------------------- 触摸画线 ---------------------

// 画布触摸回调:按下开始,拖动画线,松开结束
static void canvas_draw_event_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * canvas = lv_event_get_target(e);
    lv_indev_t * indev = lv_indev_get_act();
    if (indev == NULL) return;

    lv_point_t point;
    lv_indev_get_point(indev, &point);

    // 获取画布在屏幕上的坐标
    lv_area_t canvas_coords;
    lv_obj_get_coords(canvas, &canvas_coords);

    // 将屏幕坐标转换为画布内坐标
    lv_coord_t x = point.x - canvas_coords.x1;
    lv_coord_t y = point.y - canvas_coords.y1;
    if (x < 0 || x >= CANVAS_W || y < 0 || y >= CANVAS_H) {
        // 出画布范围,重置 last_point
        last_point.x = -1;
        return;
    }

    if (code == LV_EVENT_PRESSED) {
        // 按下:画一个点
        lv_canvas_set_px(canvas, x, y, lv_color_hex(0x000000));
        last_point.x = x;
        last_point.y = y;
    } else if (code == LV_EVENT_PRESSING) {
        // 拖动:画线连接 last_point -> (x,y)
        if (last_point.x >= 0) {
            // 构造两个点的数组
            lv_point_t points[2] = {last_point, {x, y}};
            lv_draw_line_dsc_t line_dsc;
            lv_draw_line_dsc_init(&line_dsc);
            line_dsc.color = lv_color_hex(0x000000);
            line_dsc.width = 2;
            line_dsc.round_start = 1;
            line_dsc.round_end = 1;

            lv_canvas_draw_line(canvas, points, 2, &line_dsc);
        }
        last_point.x = x;
        last_point.y = y;
    } else if (code == LV_EVENT_RELEASED) {
        // 松开:重置 last_point
        last_point.x = -1;
    }
}

// --------------------- 按钮回调 ---------------------

// 保存按钮:把画布写入 SPIFFS
static void btn_save_draw_cb(lv_event_t * e)
{
    (void)e;
    if (canvas_buf == NULL) return;
    int buf_size = CANVAS_W * CANVAS_H * sizeof(lv_color_t);
    bool ok = drawing_save(canvas_buf, buf_size);
    if (ok) {
        // 直接复用桌面保存成功的消息框
        show_message_box("提示", "保存成功!");
    } else {
        show_message_box("错误", "保存失败!");
    }
}

// 清除按钮:重置画布为白色
static void btn_clear_draw_cb(lv_event_t * e)
{
    (void)e;
    if (ui_canvas_draw == NULL) return;
    lv_canvas_fill_bg(ui_canvas_draw, lv_color_hex(0xFFFFFF), LV_OPA_COVER);
    last_point.x = -1;
    // 删除 SPIFFS 中的绘图文件，使下次进入为全新白板
    remove(DRAWING_FILE);
}

// 退出按钮:返回桌面(界面3)
void ui_event_exitbtu6(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        ui_Screen3_screen_init();   // 回桌面
        lv_scr_load(ui_Screen3);
        ui_Screen6_screen_destroy();
    }
}

// --------------------- 界面初始化/销毁 ---------------------

void ui_Screen6_screen_init(void)
{
    ui_Screen6 = lv_obj_create(NULL);

    // 设置背景色
    lv_obj_set_style_bg_color(ui_Screen6, lv_color_hex(0xF0F0F0), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_Screen6, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    // 标题
    lv_obj_t * title = lv_label_create(ui_Screen6);
    lv_label_set_text(title, "绘图应用");
    lv_obj_set_style_text_font(title, &ui_font_Font1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    // 退出按钮(右上角)
    ui_exitbtu6 = lv_btn_create(ui_Screen6);
    lv_obj_set_width(ui_exitbtu6, 80);
    lv_obj_set_height(ui_exitbtu6, 40);
    lv_obj_align(ui_exitbtu6, LV_ALIGN_TOP_RIGHT, -10, 5);
    lv_obj_t * lbl_exit = lv_label_create(ui_exitbtu6);
    lv_label_set_text(lbl_exit, "退出");
    lv_obj_set_style_text_font(lbl_exit, &ui_font_Font1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_center(lbl_exit);
    lv_obj_add_event_cb(ui_exitbtu6, ui_event_exitbtu6, LV_EVENT_ALL, NULL);

    // 画布缓冲(优先用 PSRAM,约 307KB)
    int buf_size = CANVAS_W * CANVAS_H * sizeof(lv_color_t);
    canvas_buf = (lv_color_t *)heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (canvas_buf == NULL) {
        // PSRAM 不可用,退回普通 heap(可能因 OOM 失败)
        canvas_buf = (lv_color_t *)malloc(buf_size);
    }
    if (canvas_buf == NULL) {
        // 内存不足提示
        lv_obj_t * warn = lv_label_create(ui_Screen6);
        lv_label_set_text(warn, "内存不足,无法创建画布");
        lv_obj_center(warn);
        return;
    }

    // 创建画布
    ui_canvas_draw = lv_canvas_create(ui_Screen6);
    lv_canvas_set_buffer(ui_canvas_draw, canvas_buf, CANVAS_W, CANVAS_H, LV_IMG_CF_TRUE_COLOR);
    lv_canvas_fill_bg(ui_canvas_draw, lv_color_hex(0xFFFFFF), LV_OPA_COVER);
    // 画布居中
    lv_obj_align(ui_canvas_draw, LV_ALIGN_CENTER, 0, 20);
    // 画布需要可点击以接收触摸事件
    lv_obj_clear_flag(ui_canvas_draw, LV_OBJ_FLAG_SCROLLABLE);  // 不可滚动(否则长按变成滚动)
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
    lv_obj_t * lbl_save = lv_label_create(ui_btn_save_draw);
    lv_label_set_text(lbl_save, "保存");
    lv_obj_set_style_text_font(lbl_save, &ui_font_Font1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_center(lbl_save);
    lv_obj_add_event_cb(ui_btn_save_draw, btn_save_draw_cb, LV_EVENT_CLICKED, NULL);

    // 清除按钮(右下)
    ui_btn_clear_draw = lv_btn_create(ui_Screen6);
    lv_obj_set_width(ui_btn_clear_draw, 100);
    lv_obj_set_height(ui_btn_clear_draw, 40);
    lv_obj_align(ui_btn_clear_draw, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
    lv_obj_t * lbl_clear = lv_label_create(ui_btn_clear_draw);
    lv_label_set_text(lbl_clear, "清除");
    lv_obj_set_style_text_font(lbl_clear, &ui_font_Font1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_center(lbl_clear);
    lv_obj_add_event_cb(ui_btn_clear_draw, btn_clear_draw_cb, LV_EVENT_CLICKED, NULL);
}

void ui_Screen6_screen_destroy(void)
{
    if (ui_Screen6 == NULL) return;

    // 释放画布缓冲(注意:lv_canvas_set_buffer 后 LVGL 不拥有 buffer,需自己释放)
    if (canvas_buf != NULL) {
        free(canvas_buf);
        canvas_buf = NULL;
    }
    last_point.x = -1;

    // NULL screen variables
    ui_Screen6 = NULL;
    ui_exitbtu6 = NULL;
    ui_canvas_draw = NULL;
    ui_btn_save_draw = NULL;
    ui_btn_clear_draw = NULL;
}