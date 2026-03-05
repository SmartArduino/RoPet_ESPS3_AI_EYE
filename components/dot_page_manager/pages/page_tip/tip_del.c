#include "log_conf.h"
#include "page_cfg.h"
#include "esp_lvgl_port.h"

LV_FONT_DECLARE(font_puhui_16_4)

#define TOAST_TEXT_MAX 128

static lv_obj_t *s_label = NULL;

/* 这就是“每次要toast的文字”入口 */
static char s_label_text[TOAST_TEXT_MAX] = {0};

/* 实现生命周期 */
static void onCreate(page_t *page) {
    lvgl_port_lock(-1);

    page->obj = lv_obj_create(lv_screen_active());
    lv_obj_set_size(page->obj, LV_HOR_RES, LV_VER_RES);

    lv_obj_set_style_bg_color(page->obj, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(page->obj, LV_OPA_COVER, 0);   /* 确保不透明 */
    lv_obj_set_style_radius(page->obj, LV_HOR_RES / 2, 0); // 整圆
    lv_obj_set_style_clip_corner(page->obj, true, 0);      // 打开裁剪

    /* 居中 label */

    s_label = lv_label_create(page->obj);
    lv_obj_set_style_text_font(s_label, &font_puhui_16_4, 0);
    lv_obj_set_style_text_color(s_label, lv_color_white(), 0);
    lv_obj_set_width(s_label, LV_HOR_RES - 40);                    /* 左右各留 20 px 边距 */
    lv_obj_set_style_text_align(s_label, LV_TEXT_ALIGN_CENTER, 0); /* 多行居中 */
    lv_label_set_long_mode(s_label, LV_LABEL_LONG_WRAP);           /* 超长自动换行 */
    lv_obj_align(s_label, LV_ALIGN_CENTER, 0, 40);
    lv_label_set_text(s_label, "是否删除该文件，单击删除，双击取消");

    lvgl_port_unlock();
}
static void onShow(page_t *page) {
    lvgl_port_lock(-1);
    lv_obj_clear_flag(page->obj, LV_OBJ_FLAG_HIDDEN);
    lvgl_port_unlock();

}

/* 模板结构体 */
static const page_impl_t page_impl = {
    .onCreate = onCreate,
    .onShow = onShow,
    /* 其余可留 NULL */
};

const page_impl_t *page_tip_del_impl(void) {
    return &page_impl;
}