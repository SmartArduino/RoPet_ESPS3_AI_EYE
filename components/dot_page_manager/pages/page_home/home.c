#include "log_conf.h"
#include "page_cfg.h"
#include "page_manager.h"
#include "esp_lvgl_port.h"
/**
 * HOME主页长期存在，用于显示动图或图片
 */

static void onCreate(page_t *page);
static void onCreated(page_t *page);
static void onShow(page_t *page);

/* 实现生命周期 */
static void onCreate(page_t *page) {
    lvgl_port_lock(-1);

    page_manager_t *p = page_manager_get();
    page->obj = lv_obj_create(p->screen_param->screen);
    lv_obj_set_size(page->obj, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(page->obj, lv_color_white(), 0);
    lv_obj_set_style_pad_all(page->obj, 0, 0);
    lv_obj_set_style_border_width(page->obj, 0, 0);
    lv_obj_set_scrollbar_mode(page->obj, LV_SCROLLBAR_MODE_OFF); // 不显示滚动条
    lv_obj_set_scroll_dir(page->obj, LV_DIR_NONE);               // 禁止任何方向滚动

    // 创建图片控件
    page->second_obj = lv_image_create(page->obj);
    lv_obj_center(page->second_obj);

    lvgl_port_unlock();

    if (page->onCreated) page->onCreated(page);
}

static void onCreated(page_t *page) {
    /* 播放动画 */
}

static void onShow(page_t *page) {
    lvgl_port_lock(-1);
    lv_obj_clear_flag(page->obj, LV_OBJ_FLAG_HIDDEN);
    lvgl_port_unlock();
    PM_LOGI(">>>   Page Home onShow   >>>");
}

static void onHide(page_t *page) {
    PM_LOGI("<<<   Page Home onHide   <<<");
}

/* 模板结构体 */
static const page_impl_t page_impl = {
    .onCreate = onCreate,
    .onCreated = onCreated,
    .onShow = onShow,
    .onHide = onHide,
    /* 其余可留 NULL */
};

/**
 * 主页的页面注册
 */
const page_impl_t *page_home_impl(void) {
    return &page_impl;
}
