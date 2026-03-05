#include "page_manager.h"
#include "lvgl.h"
#include <string.h>
#include "esp_lvgl_port.h"

#define TOAST_TEXT_MAX 128

static lv_obj_t *s_label = NULL;
static lv_timer_t *s_timer = NULL;

/* 这就是“每次要toast的文字”入口 */
static char s_toast_text[TOAST_TEXT_MAX] = {0};
static uint32_t s_toast_ms = 3000;

static void toast_timeout_cb(lv_timer_t *t) {
    // 回到上一页（更符合 toast）
    // 如果历史栈可能为空
    if (!page_manager_go_back(ANIM_TYPE_NONE)) {
        page_manager_switch_to("home", ANIM_TYPE_NONE);
    }
}

/* 生命周期 */
static void onCreate(page_t *page) {
    lvgl_port_lock(-1);
    page->obj = lv_obj_create(lv_screen_active());
    lv_obj_set_size(page->obj, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_opa(page->obj, LV_OPA_TRANSP, 0); // 透明背景（像 overlay）
    lv_obj_clear_flag(page->obj, LV_OBJ_FLAG_SCROLLABLE);

    // 中间一个 label
    s_label = lv_label_create(page->obj);
    lv_obj_center(s_label);

    lvgl_port_unlock();
}

static void onShow(page_t *page) {
    lvgl_port_lock(-1);
    lv_obj_clear_flag(page->obj, LV_OBJ_FLAG_HIDDEN);
    lvgl_port_unlock();

    if (s_label) {
        strncpy(s_toast_text, (char *)page->stash->ptr, TOAST_TEXT_MAX - 1);
        s_toast_text[TOAST_TEXT_MAX - 1] = '\0'; // 确保字符串以 null 字符结尾
        lvgl_port_lock(-1);
        lv_label_set_text(s_label, s_toast_text);
        lv_obj_center(s_label);
        lvgl_port_unlock();
    }

    // 重置定时器（连续 toast 会刷新时间）
    if (s_timer) {
        lv_timer_del(s_timer);
        s_timer = NULL;
    }
    s_timer = lv_timer_create(toast_timeout_cb, s_toast_ms, NULL);
}

static void onHide(page_t *page) {
    if (s_timer) {
        lv_timer_del(s_timer);
        s_timer = NULL;
    }

    // TODO 释放 stash数据
}

/* 模板结构体 */
static const page_impl_t page_impl = {
    .onCreate = onCreate,
    .onShow = onShow,
    .onHide = onHide,
};

const page_impl_t *page_toast_impl(void) {
    return &page_impl;
}
