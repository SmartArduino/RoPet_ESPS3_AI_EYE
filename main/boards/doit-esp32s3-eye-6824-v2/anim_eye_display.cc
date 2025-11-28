#include "anim_eye_display.h"

#include <vector>
#include <algorithm>
#include <font_awesome_symbols.h>
#include <esp_log.h>
#include <esp_err.h>
#include <esp_lvgl_port.h>
#include <esp_heap_caps.h>
#include "assets/lang_config.h"
#include <cstring>
#include "settings.h"

#include "board.h"
#if CONFIG_SUPPORT_MINI_PROGRAMS_REPLACE_PSD
#include "doit_file.h"
#endif

#define TAG "AnimEyeDisplay"

AnimEyeDisplay::AnimEyeDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                               int width, int height, int offset_x, int offset_y, bool mirror_x,
                               bool mirror_y, bool swap_xy, DisplayFonts fonts)
    : SpiLcdDisplay(panel_io, panel, width, height, offset_x, offset_y, mirror_x, mirror_y, swap_xy,
                    fonts)
{
    SetupAnimContainer();
};

void AnimEyeDisplay::SetupAnimContainer()
{
#if CONFIG_SUPPORT_MINI_PROGRAMS_REPLACE_PSD
    // show_container_：显示图片
    auto screen = lv_screen_active();
    psd_container_ = lv_obj_create(screen);
    lv_obj_set_size(psd_container_, width_, height_);
    lv_obj_set_style_bg_color(psd_container_, current_theme_.background, 0);
    lv_obj_set_style_pad_all(psd_container_, 0, 0);
    lv_obj_set_style_border_width(psd_container_, 0, 0);
    lv_obj_set_scrollbar_mode(psd_container_, LV_SCROLLBAR_MODE_OFF); // 不显示滚动条
    lv_obj_set_scroll_dir(psd_container_, LV_DIR_NONE);               // 禁止任何方向滚动
    lv_obj_add_flag(psd_container_, LV_OBJ_FLAG_HIDDEN);              // 默认隐藏
    /* 图片对象 */
    psd_obj_ = lv_image_create(psd_container_);

    lv_obj_center(psd_obj_);
#if CONFIG_LCD_ST77916_360X360
    doit_file_init(psd_obj_, 368, 368);
#elif CONFIG_LCD_GC9A01_240X240 || CONFIG_LCD_ST7796_240X240
    doit_file_init(psd_obj_, 240, 240);
#elif CONFIG_LCD_GC9A01_160X160
    doit_file_init(psd_obj_, 160, 160);
#endif

#if CONFIG_SUPPORT_MINI_PROGRAMS_REPLACE_PSD
    UpdateAnimContainer();
#endif

#endif
}

#if CONFIG_SUPPORT_MINI_PROGRAMS_REPLACE_PSD
void AnimEyeDisplay::UpdateAnimContainer()
{
    DisplayLockGuard lock(this);
    doit_file_decode(); // 解码并显示图片
    lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(psd_container_, LV_OBJ_FLAG_HIDDEN);
}

#if CONFIG_USE_PSD_MULTIPLE
void AnimEyeDisplay::SetPSD(uint8_t psd_order)
{
    doit_file_psd_set(psd_order);
}
#endif
#endif
