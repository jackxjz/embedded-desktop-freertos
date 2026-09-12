// 启动界面(界面0)：Logo + 版本号 + 进度条
// 说明：本界面为手写实现，未经 SquareLine 生成——它只是开机的过渡页，
//       没有需要可视化编辑的复杂布局，手写反而更好维护。

#ifndef UI_SCREEN0_H
#define UI_SCREEN0_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

// 当前系统版本号（全局唯一的版本来源，后续"系统更新"应用可直接引用这里）
#define APP_VERSION_STR "4.5"

// SCREEN: ui_Screen0
extern lv_obj_t * ui_Screen0;
extern void ui_Screen0_screen_init(void);
extern void ui_Screen0_screen_destroy(void);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif
