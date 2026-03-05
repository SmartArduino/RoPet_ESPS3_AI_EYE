#include "progress.h"
#include "log_conf.h"
#include "page_cfg.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_lvgl_port.h"

#define HTTP_DOWNLOAD_TIP_CARD_STAY_MS 5000 // 提示屏停留时间

static lv_obj_t *s_lv_progress;
static lv_obj_t *s_lv_progress_bar;
static lv_obj_t *s_lv_progress_label;

static TaskHandle_t lvgl_progress_task_handle = NULL; // 进度条任务句柄
static SemaphoreHandle_t s_prog_exit_sem = NULL;      // 进度任务退出信号

static uint8_t progress_percent = 0; // 当前下载进度百分比

static bool s_paused = false;

/**
    31          24 23        16 15        8 7        0
    +--------------+------------+-----------+----------+
    |   cmd (8b)   |  reason    |  reserved |  value   |
    |              |  (8b)      |  (8b)     |  (8b)    |
    +--------------+------------+-----------+----------+
 */
#define UI_NOTIFY(cmd, reason, val) \
    (((uint32_t)(cmd) & 0xFF) << 24 | ((uint32_t)(reason) & 0xFF) << 16 | ((uint32_t)(val) & 0xFF))

#define UI_NOTIFY_CMD(x) \
    ((ui_prog_cmd_t)(((x) >> 24) & 0xFF))
#define UI_NOTIFY_REASON(x) \
    ((ui_prog_fail_reason_t)(((x) >> 16) & 0xFF))
#define UI_NOTIFY_VAL(x) \
    ((uint8_t)((x) & 0xFF))

/**
 * @brief 设置进度条任务的显示内容
 */
static void ui_prog_set_label(const char *txt) {
    if (!s_lv_progress_label) return;
    lv_label_set_text(s_lv_progress_label, txt);
    lv_obj_align_to(
        s_lv_progress_label,
        s_lv_progress_bar,
        LV_ALIGN_OUT_BOTTOM_MID,
        0,
        8);

    lv_refr_now(NULL);
}

/**
 * @brief 进度条更新任务
 */
static void lvgl_progress_task(void *arg) {
    uint32_t msg = 0;

    while (true) {
        if (xTaskNotifyWait(0, 0xFFFFFFFF, &msg, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        ui_prog_cmd_t cmd = UI_NOTIFY_CMD(msg);
        ui_prog_fail_reason_t reason = UI_NOTIFY_REASON(msg);
        uint8_t val = UI_NOTIFY_VAL(msg);

        bool need_exit = false;
        uint32_t delay_ms = 0;

        lvgl_port_lock(-1);

        switch (cmd) {
        case UI_PROG_CMD_PROGRESS:
            if (val > 100) val = 100;
            lv_bar_set_value(s_lv_progress_bar, val, LV_ANIM_OFF);
            {
                char buf[32];
                snprintf(buf, sizeof(buf), "download-%u %% (1/2)", val);
                PM_LOGI("Download:%d%%", val);
                ui_prog_set_label(buf);
            }
            break;

        case UI_PROG_CMD_FILE_WRITE:
            if (val > 100) val = 100;
            lv_bar_set_value(s_lv_progress_bar, val, LV_ANIM_OFF);
            {
                char buf[32];
                snprintf(buf, sizeof(buf), "writing-%u %% (2/2)", val);
                PM_LOGI("Writing:%d%%", val);
                ui_prog_set_label(buf);
            }
            break;

        case UI_PROG_CMD_PAUSE:
            s_paused = true;
            ui_prog_set_label("Paused: network lost");
            break;

        case UI_PROG_CMD_RESUME:
            s_paused = false;
            ui_prog_set_label("Resuming...");
            break;

        case UI_PROG_CMD_FAIL:
            s_paused = true;
            delay_ms = HTTP_DOWNLOAD_TIP_CARD_STAY_MS;
            need_exit = true;
            switch (reason) {
            case UI_PROG_FAIL_NET_DISCONNECT: ui_prog_set_label("NetWork Error"); break;
            case UI_PROG_FAIL_NO_SPACE: ui_prog_set_label("Space is not enough"); break;
            case UI_PROG_FAIL_WRITE_ERROR: ui_prog_set_label("Write Error"); break;
            case UI_PROG_FAIL_UNKNOWN: ui_prog_set_label("Unknow Error"); break;

            default: ui_prog_set_label("Unknow Error"); break;
            }
            break;

        case UI_PROG_CMD_DONE:
            lv_bar_set_value(s_lv_progress_bar, 100, LV_ANIM_OFF);
            ui_prog_set_label("100 %");
            delay_ms = 1000;
            need_exit = true;
            break;

        case UI_PROG_CMD_CANCEL:
            need_exit = true;
            break;

        default:
            break;
        }

        lvgl_port_unlock();

        if (delay_ms) {
            vTaskDelay(pdMS_TO_TICKS(delay_ms)); // 放到锁外
        }
        if (need_exit) {
            break;
        }
    }

    // 通知外部：我已经完成“延时/退出”
    if (s_prog_exit_sem) {
        xSemaphoreGive(s_prog_exit_sem);
    }

    lvgl_progress_task_handle = NULL;
    vTaskDelete(NULL);
}

/* 实现生命周期 */
static void onCreate(page_t *page) {
    lvgl_port_lock(-1);
    page->obj = lv_obj_create(lv_screen_active());
    lv_obj_set_size(page->obj, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_radius(page->obj, LV_HOR_RES / 2, 0); // 整圆
    lv_obj_set_style_clip_corner(page->obj, true, 0);      // 打开裁剪

    /* 1. 全屏容器 */
    s_lv_progress = lv_obj_create(page->obj);
    lv_obj_center(s_lv_progress);
    lv_obj_set_size(s_lv_progress, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(s_lv_progress, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_lv_progress, LV_OPA_30, 0);    /* 30% 黑色蒙版 */
    lv_obj_clear_flag(s_lv_progress, LV_OBJ_FLAG_CLICKABLE); /* 允许点击下层 */
    lv_obj_set_scrollbar_mode(s_lv_progress, LV_SCROLLBAR_MODE_OFF);

    /* 2. 进度条 */
    s_lv_progress_bar = lv_bar_create(s_lv_progress);
    lv_obj_set_size(s_lv_progress_bar, LV_VER_RES * 0.7, LV_HOR_RES / 10);
    lv_obj_center(s_lv_progress_bar);
    lv_bar_set_range(s_lv_progress_bar, 0, 100);
    /* 3. 百分比标签 */
    s_lv_progress_label = lv_label_create(s_lv_progress);
    lv_obj_align_to(
        s_lv_progress_label,
        s_lv_progress_bar,
        LV_ALIGN_OUT_BOTTOM_MID, // 在进度条正下方
        0,
        8 // 下移 8px
    );
    lv_label_set_text(s_lv_progress_label, "0 %");

    /* 4. 样式：黑条+黑字，可改成任意色 */
    static lv_style_t style_bg, style_indic;
    lv_style_init(&style_bg);
    lv_style_set_bg_color(&style_bg, lv_color_black());
    lv_style_set_border_color(&style_bg, lv_color_black());
    lv_style_set_border_width(&style_bg, 1);

    lv_style_init(&style_indic);
    lv_style_set_bg_color(&style_indic, lv_color_black());

    lv_obj_add_style(s_lv_progress_bar, &style_bg, LV_PART_MAIN);
    lv_obj_add_style(s_lv_progress_bar, &style_indic, LV_PART_INDICATOR);

    lvgl_port_unlock();
}
static void onShow(page_t *page) {
    lvgl_port_lock(-1);
    lv_obj_clear_flag(page->obj, LV_OBJ_FLAG_HIDDEN);
    lvgl_port_unlock();

    if (!s_prog_exit_sem) {
        s_prog_exit_sem = xSemaphoreCreateBinary();
    }
    // 复位为“未退出”
    if (s_prog_exit_sem) {
        xSemaphoreTake(s_prog_exit_sem, 0);
    }

    /* 启动LVGL进度任务 */
    if (pdPASS != xTaskCreate(lvgl_progress_task, "lvgl_progress", 4608 + 1024, NULL, tskIDLE_PRIORITY + 1, &lvgl_progress_task_handle)) {
        PM_LOGE("Failed to create lvgl_progress_task");
    }
}

static void onHide(page_t *page) {
    progress_percent = 0; // 重置进度
    lvgl_port_lock(-1);
    lv_label_set_text(s_lv_progress_label, "0 %");
    lvgl_port_unlock();

    // 删除进度任务
    if (lvgl_progress_task_handle) {
        vTaskDelete(lvgl_progress_task_handle);
        lvgl_progress_task_handle = NULL;
    }
}

/* 模板结构体 */
static const page_impl_t page_impl = {
    .onCreate = onCreate,
    .onShow = onShow,
    .onHide = onHide,
    /* 其余可留 NULL */
};

const page_impl_t *page_progress_impl(void) {
    return &page_impl;
}

/**
 * @brief 下面是供外部更新进度条的函数
 */
void ui_prog_update(uint8_t percent) {
    if (!lvgl_progress_task_handle) return;
    if (percent > 100) percent = 100;
    xTaskNotify(lvgl_progress_task_handle, UI_NOTIFY(UI_PROG_CMD_PROGRESS, UI_PROG_FAIL_NONE, percent), eSetValueWithOverwrite);
}

void ui_prog_update_write(uint8_t percent) {
    if (!lvgl_progress_task_handle) return;
    if (percent > 100) percent = 100;
    xTaskNotify(lvgl_progress_task_handle, UI_NOTIFY(UI_PROG_CMD_FILE_WRITE, UI_PROG_FAIL_NONE, percent), eSetValueWithOverwrite);
}

void ui_prog_pause(void) {
    if (!lvgl_progress_task_handle) return;
    xTaskNotify(lvgl_progress_task_handle, UI_NOTIFY(UI_PROG_CMD_PAUSE, UI_PROG_FAIL_NONE, 0), eSetValueWithOverwrite);
}

void ui_prog_resume(void) {
    if (!lvgl_progress_task_handle) return;
    xTaskNotify(lvgl_progress_task_handle, UI_NOTIFY(UI_PROG_CMD_RESUME, UI_PROG_FAIL_NONE, 0), eSetValueWithOverwrite);
}

void ui_prog_fail(ui_prog_fail_reason_t reason) {
    if (!lvgl_progress_task_handle) return;
    xTaskNotify(lvgl_progress_task_handle, UI_NOTIFY(UI_PROG_CMD_FAIL, reason, 0), eSetValueWithOverwrite);
}

bool ui_prog_fail_wait(ui_prog_fail_reason_t reason, uint32_t timeout_ms) {
    if (!lvgl_progress_task_handle) return false;

    // 发 fail 通知
    xTaskNotify(lvgl_progress_task_handle,
                UI_NOTIFY(UI_PROG_CMD_FAIL, reason, 0),
                eSetValueWithOverwrite);

    // 等待进度任务完成内部 delay 后退出
    if (s_prog_exit_sem) {
        return xSemaphoreTake(s_prog_exit_sem, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
    }
    return false;
}

void ui_prog_done(void) {
    if (!lvgl_progress_task_handle) return;
    xTaskNotify(lvgl_progress_task_handle, UI_NOTIFY(UI_PROG_CMD_DONE, UI_PROG_FAIL_NONE, 100), eSetValueWithOverwrite);
}