#include "anim_utils.h"
#include "lvgl.h"
#include "esp_lvgl_port.h"
#include "log_conf.h"

/* 滑动动画执行函数 */
static void slide_anim_exec(void *var, int32_t v) {
    lv_obj_t *obj = (lv_obj_t*)var;
    lv_obj_set_x(obj, v);
}

/* 淡入淡出动画执行函数 */
static void fade_anim_exec(void *var, int32_t v) {
    lv_obj_t *obj = (lv_obj_t*)var;
    lv_obj_set_style_opa(obj, v, 0);
}

/* 执行页面切换动画 */
void anim_utils_page_switch(page_t *old_page, page_t *new_page, 
                           anim_type_t anim, uint32_t time) {
    lv_anim_t a;
    
    switch (anim) {
        case ANIM_TYPE_SLIDE_LEFT: {
            lvgl_port_lock(-1);
            // 新页面从右侧进入
            lv_obj_set_x(new_page->obj, LV_HOR_RES);
            lv_obj_clear_flag(new_page->obj, LV_OBJ_FLAG_HIDDEN);
            
            // 旧页面滑出
            if (old_page) {
                lv_anim_init(&a);
                lv_anim_set_var(&a, old_page->obj);
                lv_anim_set_values(&a, 0, -LV_HOR_RES);
                lv_anim_set_time(&a, time);
                lv_anim_set_exec_cb(&a, slide_anim_exec);
                lv_anim_start(&a);
            }
            
            // 新页面滑入
            lv_anim_init(&a);
            lv_anim_set_var(&a, new_page->obj);
            lv_anim_set_values(&a, LV_HOR_RES, 0);
            lv_anim_set_time(&a, time);
            lv_anim_set_exec_cb(&a, slide_anim_exec);
            lv_anim_set_ready_cb(&a, (lv_anim_ready_cb_t)new_page->onShow);
            lv_anim_start(&a);
            lvgl_port_unlock();
            break;
        }
          // 新增：SLIDE_RIGHT 逻辑（与LEFT相反）
        case ANIM_TYPE_SLIDE_RIGHT: {
            lvgl_port_lock(-1);
            // 新页面从左侧进入（初始位置-屏幕宽度）
            lv_obj_set_x(new_page->obj, -LV_HOR_RES);
            lv_obj_clear_flag(new_page->obj, LV_OBJ_FLAG_HIDDEN);
            
            // 旧页面滑出到右侧
            if (old_page) {
                lv_anim_init(&a);
                lv_anim_set_var(&a, old_page->obj);
                lv_anim_set_values(&a, 0, LV_HOR_RES);
                lv_anim_set_time(&a, time);
                lv_anim_set_exec_cb(&a, slide_anim_exec);
                lv_anim_set_user_data(&a, old_page);
                lv_anim_set_ready_cb(&a, (lv_anim_ready_cb_t)new_page->onHide);
                lv_anim_start(&a);
            }
            
            // 新页面滑入到中间
            lv_anim_init(&a);
            lv_anim_set_var(&a, new_page->obj);
            lv_anim_set_values(&a, -LV_HOR_RES, 0);
            lv_anim_set_time(&a, time);
            lv_anim_set_exec_cb(&a, slide_anim_exec);
            lv_anim_set_user_data(&a, new_page);
            lv_anim_set_ready_cb(&a,(lv_anim_ready_cb_t)new_page->onHide);
            lv_anim_start(&a);
            lvgl_port_unlock();
            break;
        }
        
        case ANIM_TYPE_FADE: {
            lvgl_port_lock(-1);
            // 设置初始透明度
            lv_obj_set_style_opa(new_page->obj, LV_OPA_TRANSP, 0);
            lv_obj_clear_flag(new_page->obj, LV_OBJ_FLAG_HIDDEN);
            
            // 旧页面淡出
            if (old_page) {
                lv_anim_init(&a);
                lv_anim_set_var(&a, old_page->obj);
                lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_TRANSP);
                lv_anim_set_time(&a, time);
                lv_anim_set_exec_cb(&a, fade_anim_exec);
                lv_anim_start(&a);
            }
            
            // 新页面淡入
            lv_anim_init(&a);
            lv_anim_set_var(&a, new_page->obj);
            lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
            lv_anim_set_time(&a, time);
            lv_anim_set_exec_cb(&a, fade_anim_exec);
            lv_anim_set_ready_cb(&a, (lv_anim_ready_cb_t)new_page->onShow);
            lv_anim_start(&a);
            lvgl_port_unlock();
            break;
        }
        
        default:
            break;
    }
}