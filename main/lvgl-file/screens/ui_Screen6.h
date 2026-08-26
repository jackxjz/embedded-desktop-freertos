#ifndef UI_SCREEN6_H
#define UI_SCREEN6_H

#include "ui.h"

// SCREEN: ui_Screen6 (绘图应用)
extern void ui_Screen6_screen_init(void);
extern void ui_Screen6_screen_destroy(void);
extern lv_obj_t * ui_Screen6;
extern void ui_event_exitbtu6(lv_event_t * e);
extern lv_obj_t * ui_exitbtu6;
extern lv_obj_t * ui_canvas_draw;        // 绘图画布
extern lv_obj_t * ui_btn_save_draw;     // 保存按钮
extern lv_obj_t * ui_btn_clear_draw;    // 清除按钮

#endif
