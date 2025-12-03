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

#if CONFIG_USE_ANIM_EYE
#include "avi_anim.h"
extern "C"
{
    lv_display_t *display_0;
}
// 心情
extern const uint8_t xq_start[] asm("_binary_xq_avi_start");
extern const uint8_t xq_end[] asm("_binary_xq_avi_end");

// 海洋
extern const uint8_t hy_start[] asm("_binary_hy_avi_start");
extern const uint8_t hy_end[] asm("_binary_hy_avi_end");

// 梦境
extern const uint8_t mj_start[] asm("_binary_mj_avi_start");
extern const uint8_t mj_end[] asm("_binary_mj_avi_end");

// 彩虹
extern const uint8_t ch_start[] asm("_binary_ch_avi_start");
extern const uint8_t ch_end[] asm("_binary_ch_avi_end");

// 海洋
extern const uint8_t hy_start[] asm("_binary_hy_avi_start");
extern const uint8_t hy_end[] asm("_binary_hy_avi_end");

// 梦境
extern const uint8_t mj_start[] asm("_binary_mj_avi_start");
extern const uint8_t mj_end[] asm("_binary_mj_avi_end");

// 彩虹
extern const uint8_t ch_start[] asm("_binary_ch_avi_start");
extern const uint8_t ch_end[] asm("_binary_ch_avi_end");
#endif

#define TAG "AnimEyeDisplay"

// Color definitions for dark theme
#define DARK_BACKGROUND_COLOR lv_color_hex(0x121212)       // Dark background
#define DARK_TEXT_COLOR lv_color_white()                   // White text
#define DARK_CHAT_BACKGROUND_COLOR lv_color_hex(0x1E1E1E)  // Slightly lighter than background
#define DARK_USER_BUBBLE_COLOR lv_color_hex(0x1A6C37)      // Dark green
#define DARK_ASSISTANT_BUBBLE_COLOR lv_color_hex(0x333333) // Dark gray
#define DARK_SYSTEM_BUBBLE_COLOR lv_color_hex(0x2A2A2A)    // Medium gray
#define DARK_SYSTEM_TEXT_COLOR lv_color_hex(0xAAAAAA)      // Light gray text
#define DARK_BORDER_COLOR lv_color_hex(0x333333)           // Dark gray border
#define DARK_LOW_BATTERY_COLOR lv_color_hex(0xFF0000)      // Red for dark mode

// Color definitions for light theme
#define LIGHT_BACKGROUND_COLOR lv_color_white()            // White background
#define LIGHT_TEXT_COLOR lv_color_black()                  // Black text
#define LIGHT_CHAT_BACKGROUND_COLOR lv_color_hex(0xE0E0E0) // Light gray background
#define LIGHT_USER_BUBBLE_COLOR lv_color_hex(0x95EC69)     // WeChat green
#define LIGHT_ASSISTANT_BUBBLE_COLOR lv_color_white()      // White
#define LIGHT_SYSTEM_BUBBLE_COLOR lv_color_hex(0xE0E0E0)   // Light gray
#define LIGHT_SYSTEM_TEXT_COLOR lv_color_hex(0x666666)     // Dark gray text
#define LIGHT_BORDER_COLOR lv_color_hex(0xE0E0E0)          // Light gray border
#define LIGHT_LOW_BATTERY_COLOR lv_color_black()           // Black for light mode

// Define dark theme colors
const ThemeColors DARK_THEME = {
    .background = DARK_BACKGROUND_COLOR,
    .text = DARK_TEXT_COLOR,
    .chat_background = DARK_CHAT_BACKGROUND_COLOR,
    .user_bubble = DARK_USER_BUBBLE_COLOR,
    .assistant_bubble = DARK_ASSISTANT_BUBBLE_COLOR,
    .system_bubble = DARK_SYSTEM_BUBBLE_COLOR,
    .system_text = DARK_SYSTEM_TEXT_COLOR,
    .border = DARK_BORDER_COLOR,
    .low_battery = DARK_LOW_BATTERY_COLOR};

// Define light theme colors
const ThemeColors LIGHT_THEME = {
    .background = LIGHT_BACKGROUND_COLOR,
    .text = LIGHT_TEXT_COLOR,
    .chat_background = LIGHT_CHAT_BACKGROUND_COLOR,
    .user_bubble = LIGHT_USER_BUBBLE_COLOR,
    .assistant_bubble = LIGHT_ASSISTANT_BUBBLE_COLOR,
    .system_bubble = LIGHT_SYSTEM_BUBBLE_COLOR,
    .system_text = LIGHT_SYSTEM_TEXT_COLOR,
    .border = LIGHT_BORDER_COLOR,
    .low_battery = LIGHT_LOW_BATTERY_COLOR};

LV_FONT_DECLARE(font_awesome_30_4);

AnimEyeDisplay::AnimEyeDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                               int width, int height, int offset_x, int offset_y, bool mirror_x,
                               bool mirror_y, bool swap_xy, DisplayFonts fonts)
    : SpiLcdDisplay(panel_io, panel, width, height, offset_x, offset_y, mirror_x, mirror_y, swap_xy,
                    fonts)
{
    SetupAnimContainer();
};

/**
 * @brief 设置用于显示素材的容器
 * 
 */
void AnimEyeDisplay::SetupAnimContainer()
{
#if CONFIG_USE_ANIM_EYE
#if CONFIG_LCD_ST77916_360X360
    avi_anim_init(360, 360);
#elif CONFIG_LCD_GC9A01_240X240 || CONFIG_LCD_ST7796_240X240
    avi_anim_init(240, 240);
#elif CONFIG_LCD_GC9A01_160X160
    avi_anim_init(160, 160);
#endif
    // 或非循环播放（只播放一次）
    avi_anim_play_memory(true, xq_start, xq_end - xq_start);
#endif

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

/**
 * @brief 用于显示素材
 *
 */
#if CONFIG_SUPPORT_MINI_PROGRAMS_REPLACE_PSD
void AnimEyeDisplay::UpdateAnimContainer()
{
    DisplayLockGuard lock(this);
    doit_file_decode(); // 解码并显示图片
    lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(psd_container_, LV_OBJ_FLAG_HIDDEN);
}


/**
 * @brief 用于切换到下一个素材
 *
 * @param psd_order 素材顺序
 */
#if CONFIG_USE_PSD_MULTIPLE
void AnimEyeDisplay::SetPSD(uint8_t psd_order)
{
    doit_file_psd_set(psd_order);
}
#endif
#endif
