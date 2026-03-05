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
#include "ext_http_download.h"
#include "ext_fat_svc.h"
#include "ext_vpg_svc.h"
#include "page_manager.h"
#include "dot_mp.h"
#include "progress.h"
#endif

#define TAG "AnimEyeDisplay"

AnimEyeDisplay::AnimEyeDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                               int width, int height, int offset_x, int offset_y, bool mirror_x,
                               bool mirror_y, bool swap_xy, DisplayFonts fonts) :
    LcdDisplay(panel_io, panel, fonts, width, height) {
    // Basic panel clear to white
    std::vector<uint16_t> buffer(width_, 0xFFFF);
    for (int y = 0; y < height_; y++) {
        esp_lcd_panel_draw_bitmap(panel_, 0, y, width_, y + 1, buffer.data());
    }

    // Power on panel
    ESP_LOGI(TAG, "Turning display on");
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_, true));

    // Init LVGL and port
    ESP_LOGI(TAG, "Initialize LVGL library");
    lv_init();

    ESP_LOGI(TAG, "Initialize LVGL port");
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    port_cfg.task_priority = 1;
    port_cfg.timer_period_ms = 50;
    lvgl_port_init(&port_cfg);

    ESP_LOGI(TAG, "Adding LCD display");
    const lvgl_port_display_cfg_t display_cfg = {
        .io_handle = panel_io_,
        .panel_handle = panel_,
        .control_handle = nullptr,
        .buffer_size = static_cast<uint32_t>(width_ * 100),
        .double_buffer = false,
        .trans_size = 0,
        .hres = static_cast<uint32_t>(width_),
        .vres = static_cast<uint32_t>(height_),
        .monochrome = false,
        .rotation = {
            .swap_xy = swap_xy,
            .mirror_x = mirror_x,
            .mirror_y = mirror_y,
        },
        .color_format = LV_COLOR_FORMAT_RGB565,
        .flags = {
            .buff_dma = 1,
            .buff_spiram = 0,
            .sw_rotate = 0,
            .swap_bytes = 1,
            .full_refresh = 0,
            .direct_mode = 0,
        },
    };

    display_ = lvgl_port_add_disp(&display_cfg);
    if (display_ == nullptr) {
        ESP_LOGE(TAG, "Failed to add display");
        return;
    }

    if (offset_x != 0 || offset_y != 0) {
        lv_display_set_offset(display_, offset_x, offset_y);
    }

    // Build base UI before PSD overlay
    SetupUI();
    // SetupAnimContainer();
    MpuInit();
};

void AnimEyeDisplay::SetupAnimContainer() {
#if CONFIG_SUPPORT_MINI_PROGRAMS_REPLACE_PSD
    // show_container_：显示图片
    auto screen = lv_screen_active();
    // lv_obj_t *cust_scr = lv_obj_create(NULL); // 创建一个自定义的屏幕
    psd_container_ = lv_obj_create(screen);
    lv_obj_set_size(psd_container_, width_, height_);
    lv_obj_set_style_bg_color(psd_container_, current_theme_.background, 0);
    lv_obj_set_style_pad_all(psd_container_, 0, 0);
    lv_obj_set_style_border_width(psd_container_, 0, 0);
    lv_obj_set_scrollbar_mode(psd_container_, LV_SCROLLBAR_MODE_OFF); // 不显示滚动条
    lv_obj_set_scroll_dir(psd_container_, LV_DIR_NONE);               // 禁止任何方向滚动
    lv_obj_add_flag(psd_container_, LV_OBJ_FLAG_HIDDEN);              // 默认隐藏
    lv_obj_center(psd_obj_);
    /* 图片对象 */
    psd_obj_ = lv_image_create(psd_container_);

    lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);       // 隐藏小智的UI
    lv_obj_clear_flag(psd_container_, LV_OBJ_FLAG_HIDDEN); // 显示我自己的UI

#endif
}

void AnimEyeDisplay::MpuInit() {
    ESP_LOGI(TAG, "UI initialized with width: %d, height: %d", width_, height_);
    /* 2.上传功能初始化 */

    dot_fat_init(); // 初始化文件系统

    dot_http_dl_init([](uint8_t precent, uint8_t stage) { // http下载回调函数
        if (stage == 1)                                   // 下载进度
            ui_prog_update(precent);                      // 通知下载进度
        else                                              // 写入进度
            ui_prog_update_write(precent);                // 通知下载进度

    });
    // 初始化UI
    lv_obj_t *cust_scr = lv_obj_create(NULL); // 创建一个自定义的屏幕
    page_manager_init(cust_scr, width_, height_);

    /* 3.初始化解码器 */
    dot_vpg_decode_init();

    /* 开始解码显示vpg数据 */
    dot_mp_start_play();
}
