#include "dot_mp.h"
#include "ext_fat_svc.h"
#include "ext_vpg_svc.h"
#include "ext_usb_disk_svc.h"
#include "page_manager.h"
#include "esp_lvgl_port.h"

static uint8_t s_platform_idx = 1; // 素材更换平台的索引

static bool s_is_switching_mode = false;     // 是否正在切换模式
static mpu_mode_t s_cur_mode = MPU_MODE_RUN; // 默认处于运行模式

#define TAG "dot_mp"

/* 声明一个工具函数：判断当前是否正显示“无素材”提示页 */
static bool is_showing_tip_usb_page(void) {
    page_manager_t *pm = page_manager_get();
    if (!pm->current_page) return false;
    return strcmp(pm->current_page->name, "tip_usb") == 0;
}

void switch_next_psd(void) {
    const char *path = dot_fs_next_file();
    if (!path || !path[0]) return;
    dot_mp_start_play();
    ESP_LOGI(TAG, "switch to psd: %s", path);
}

uint8_t dot_mp_get_platform_idx(void) {
    return s_platform_idx;
}

void dot_mp_set_platform_idx(uint8_t index) {
    s_platform_idx = index;
}

mpu_mode_t dot_mp_get_cur_mode() {
    return s_cur_mode;
}

void dot_mp_switchRunOrUSB() {
    if (s_is_switching_mode) { // 正在切换模式，跳过
        ESP_LOGI(TAG, "Currently switching mode, skipping");
        return;
    }
    if (s_cur_mode != MPU_MODE_USB && s_cur_mode != MPU_MODE_RUN) { // 不是运行或usb，跳过
        ESP_LOGI(TAG, "Current mode is neither RUN nor USB, skipping switch");
        return;
    }

    if (s_cur_mode == MPU_MODE_RUN) {
        ESP_LOGI(TAG, "Switching from RUN mode to USB mode");
        s_is_switching_mode = true;
        page_manager_switch_to("tip_usb", ANIM_TYPE_NONE); // 切换到USB模式页面
        dot_mp_stop_play();                                // 停止动画播放
        dot_fat_deinit();                                  // 关闭FAT文件系统
        dot_usb_disk_init();                               // 初始化USB磁盘
        s_cur_mode = MPU_MODE_USB;
        s_is_switching_mode = false;
    } else if (s_cur_mode == MPU_MODE_USB) {
        ESP_LOGI(TAG, "Switching from USB mode to RUN mode");
        s_is_switching_mode = true;
        dot_usb_disk_deinit();                // 关闭USB磁盘
        dot_fat_init();                       // 初始化FAT文件系统
        dot_fat_org_filder();                 // 重新整理文件资源
        page_manager_go_back(ANIM_TYPE_NONE); // 切换到工作页面
        dot_mp_start_play();                  // 重新播放vpg文件
        s_cur_mode = MPU_MODE_RUN;
        s_is_switching_mode = false;
    }
}

void dot_mp_start_play(void) {
    dot_mp_stop_play();
    /* 2. 看到底有没有文件 */
    bool has_file = false;
    const char *file_type = dot_fat_get_cur_file_type();

    /* 只要检测到合法文件，就先销毁提示页（如果存在） */
    if (strcmp(file_type, "unknown") != 0) {
        has_file = true;
    }

    /* 3. 没有素材 → 显示提示页（不压栈） */
    if (!has_file) {
        ESP_LOGW(TAG, "没有检测到素材，显示提示页");
        page_manager_switch_to_nohist("tip_no_psd", ANIM_TYPE_NONE);
        return; // 直接返回，后面播放逻辑不执行
    }

    /* 5. 正常播放当前索引对应的文件 */
    const char *cur_file = dot_fs_get_current_file_name();
    if (strcmp(file_type, "jpg") == 0) {
        float scale = 0;
        page_manager_t *pm = page_manager_get();
        if (pm->screen_param->width_ == 360 && pm->screen_param->height_ == 360)
            scale = 360.0f / 368.0f;
        else if (pm->screen_param->width_ == 240 && pm->screen_param->height_ == 240)
            scale = 240.0f / 368.0f;
        else if (pm->screen_param->width_ == 160 && pm->screen_param->height_ == 160)
            scale = 160.0f / 368.0f;
        uint16_t scale_val = (uint16_t)(scale * 256.0f + 0.5f); // +0.5 四舍五入
        lvgl_port_lock(-1);
        lv_image_set_scale(page_manager_get_home_img_obj(), scale_val);
        lvgl_port_unlock();
        dot_img_show(cur_file);
    } else if (strcmp(file_type, "vpg") == 0) {
        lvgl_port_lock(-1);
        lv_image_set_scale(page_manager_get_home_img_obj(), 256);
        lvgl_port_unlock();
        dot_vpg_start(cur_file);
    } else {
        ESP_LOGW(TAG, "unsupported format: %s", file_type);
    }

    /* 4. 有素材 → 如果提示页还在，就退掉它 */
    if (is_showing_tip_usb_page()) {
        page_manager_go_back(ANIM_TYPE_NONE); // 出栈，回到上一页（通常是 HOME）
    }

    if (strcmp(page_manager_get()->current_page->name, "home") != 0) { // 如果不在主页，强制进入
        page_manager_switch_to_nohist("home", ANIM_TYPE_NONE);
    }
}

void dot_mp_stop_play(void) {
    dot_vpg_stop();

    ESP_LOGI(TAG, "动画动画播放已停止");
}