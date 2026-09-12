/*
 * 界面0: 启动界面
 * 上电后先显示 Logo(JackOS) + 版本号 + 进度条，进度条走满后自动淡入登录界面(界面1)。
 *
 * 实现要点:
 *   1. 文字全部使用 ASCII + Montserrat 字体，不用中文字体 ui_font_Font1。
 *      因为 ui_font_Font1 是按需生成的子集字体，字符集里没有"版/本"等字，
 *      写中文会直接乱码（除非重新生成字体文件）。这里从根上避开这个坑。
 *   2. Logo 用纯文字 "JackOS" 代替图片，不额外占用 Flash/PSRAM。
 *   3. 版本号统一取自 ui_Screen0.h 的 APP_VERSION_STR，改版本只需改一处。
 */

#include "ui_Screen0.h"
#include "../ui.h"

// 启动界面停留时长与进度条刷新周期
#define SPLASH_DURATION_MS   1500   // 进度条从 0 走到 100% 的总时长
#define SPLASH_TICK_MS       30     // 进度条刷新周期

// 配色
#define SPLASH_BG_COLOR      0xF5F5F5
#define SPLASH_LOGO_COLOR    0x1565C0
#define SPLASH_TEXT_COLOR    0x666666
#define SPLASH_TRACK_COLOR   0xDDDDDD
#define SPLASH_BAR_COLOR     0x1565C0

lv_obj_t * ui_Screen0 = NULL;

static lv_obj_t * ui_splash_logo = NULL;        // "JackOS" 字样
static lv_obj_t * ui_splash_version = NULL;     // 版本号
static lv_obj_t * ui_splash_hint = NULL;        // 启动提示文字
static lv_obj_t * ui_splash_bar = NULL;         // 进度条
static lv_timer_t * ui_splash_timer = NULL;     // 进度条推进定时器
static uint32_t ui_splash_elapsed_ms = 0;       // 已经过去的时长

/**
 * @brief 进度条推进定时器：推进进度，走满后切到登录界面
 */
static void splash_timer_cb(lv_timer_t * timer)
{
    ui_splash_elapsed_ms += SPLASH_TICK_MS;

    int pct = (int)(ui_splash_elapsed_ms * 100 / SPLASH_DURATION_MS);
    if (pct > 100) pct = 100;
    if (ui_splash_bar != NULL) {
        lv_bar_set_value(ui_splash_bar, pct, LV_ANIM_OFF);
    }

    if (ui_splash_elapsed_ms >= SPLASH_DURATION_MS) {
        // 进度走满：先删掉定时器，再切到登录界面
        lv_timer_del(timer);
        ui_splash_timer = NULL;

        // 【注意】这里只切换界面，不销毁 ui_Screen0。
        // 带过场动画的切换在动画期间仍会引用旧界面(prev_scr)，
        // 若在此处立刻销毁，动画回调就会访问已释放的对象。
        // 启动界面只有几个静态对象，保留下来代价可以忽略。
        _ui_screen_change(&ui_Screen1, LV_SCR_LOAD_ANIM_FADE_ON, 300, 0, &ui_Screen1_screen_init);
    }
}

void ui_Screen0_screen_init(void)
{
    ui_Screen0 = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_Screen0, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_Screen0, lv_color_hex(SPLASH_BG_COLOR), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_Screen0, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);

    // Logo：直接用 "JackOS" 字样代替图片，省掉一张启动图
    ui_splash_logo = lv_label_create(ui_Screen0);
    lv_label_set_text(ui_splash_logo, "JackOS");
    lv_obj_set_style_text_font(ui_splash_logo, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_splash_logo, lv_color_hex(SPLASH_LOGO_COLOR), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(ui_splash_logo, LV_ALIGN_CENTER, 0, -70);

    // 版本号：纯 ASCII，拼接 APP_VERSION_STR，改版本只改头文件一处
    ui_splash_version = lv_label_create(ui_Screen0);
    lv_label_set_text(ui_splash_version, "v" APP_VERSION_STR);
    lv_obj_set_style_text_font(ui_splash_version, &lv_font_montserrat_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_splash_version, lv_color_hex(SPLASH_TEXT_COLOR), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(ui_splash_version, LV_ALIGN_CENTER, 0, -30);

    // 进度条：轨道浅灰、指示器蓝色，两端圆角
    ui_splash_bar = lv_bar_create(ui_Screen0);
    lv_obj_set_size(ui_splash_bar, 260, 12);
    lv_obj_align(ui_splash_bar, LV_ALIGN_CENTER, 0, 30);
    lv_bar_set_range(ui_splash_bar, 0, 100);
    lv_bar_set_value(ui_splash_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_radius(ui_splash_bar, LV_RADIUS_CIRCLE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui_splash_bar, LV_RADIUS_CIRCLE, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_splash_bar, lv_color_hex(SPLASH_TRACK_COLOR), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_splash_bar, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_splash_bar, lv_color_hex(SPLASH_BAR_COLOR), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_splash_bar, LV_OPA_COVER, LV_PART_INDICATOR | LV_STATE_DEFAULT);

    // 启动提示
    ui_splash_hint = lv_label_create(ui_Screen0);
    lv_label_set_text(ui_splash_hint, "Loading...");
    lv_obj_set_style_text_font(ui_splash_hint, &lv_font_montserrat_12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_splash_hint, lv_color_hex(SPLASH_TEXT_COLOR), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(ui_splash_hint, LV_ALIGN_CENTER, 0, 70);

    // 启动进度条推进定时器
    ui_splash_elapsed_ms = 0;
    ui_splash_timer = lv_timer_create(splash_timer_cb, SPLASH_TICK_MS, NULL);
}

void ui_Screen0_screen_destroy(void)
{
    // 定时器必须先删，否则它下一拍就会去访问已经被删除的进度条
    if (ui_splash_timer != NULL) {
        lv_timer_del(ui_splash_timer);
        ui_splash_timer = NULL;
    }

    if (ui_Screen0) lv_obj_del(ui_Screen0);

    // NULL screen variables
    ui_Screen0 = NULL;
    ui_splash_logo = NULL;
    ui_splash_version = NULL;
    ui_splash_hint = NULL;
    ui_splash_bar = NULL;
    ui_splash_elapsed_ms = 0;
}
