#include "ext_mp_ui_svc.h"

LV_FONT_DECLARE(font_puhui_20_4)
LV_IMG_DECLARE(icon_upload_80)
LV_IMG_DECLARE(icon_upload_u80)

#define HTTP_DOWNLOAD_TIP_CARD_STAY_MS 5000 // 提示屏停留时间
#define HTTP_DOWNLOAD_TIP "The currently downloaded file is too large."

/* ===============================LVGL 进度条/tip相关句柄========================= */
static lv_obj_t *s_psd_obj_ = NULL; // 用于显示的PSD对象,外部传入

static TaskHandle_t lvgl_progress_task_handle = NULL; // 进度条任务句柄
static lv_obj_t *s_lv_progress = NULL;                // 进度条对象
static lv_obj_t *s_lv_progress_bar = NULL;
static lv_obj_t *s_lv_progress_label = NULL;

static lv_obj_t *s_lv_tip_card = NULL;  /* 卡片容器 */
static lv_obj_t *s_lv_tip_label = NULL; /* 提示文字 */

static lv_obj_t *s_lv_new_screen = NULL; // 保存当前屏幕
static lv_obj_t *s_lv_old_screen = NULL; // 保存旧屏幕
/* 进度更新消息队列 */
// static QueueHandle_t progress_queue = NULL;
static SemaphoreHandle_t s_tip_done_sem = NULL;
/* 进度条数值 */

/* ========================================================================== */

static uint16_t s_screen_width = 0;
static uint16_t s_screen_height = 0;

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
    ((ui_cmd_t)(((x) >> 24) & 0xFF))
#define UI_NOTIFY_REASON(x) \
    ((ui_fail_reason_t)(((x) >> 16) & 0xFF))
#define UI_NOTIFY_VAL(x) \
    ((uint8_t)((x) & 0xFF))

/* 定时器回调：切回旧屏 + 自杀 */
static void tip_timer_cb(lv_timer_t *t) {
    if (s_lv_tip_card) {
        lv_obj_del(s_lv_tip_card);
        s_lv_tip_card = s_lv_tip_label = NULL;
    }

    lv_timer_delete(t);

    /* 通知调用者：tip 展示结束 */
    if (s_tip_done_sem) {
        xSemaphoreGive(s_tip_done_sem);
    }
}

/* 创建提示界面*/
static void ui_tip_show(const char *txt) {
    lvgl_port_lock(-1);
    PM_LOGI("Creating tip bar");
    if (s_lv_tip_card)
        return; /* 防止重复创建 */

    /* 1. 新屏幕：纯白背景 */
    s_lv_new_screen = lv_obj_create(NULL);
    if (!s_lv_new_screen) {
        PM_LOGW("LVGL not ready, skip tip overlay");
        return;
    }
    lv_obj_set_style_bg_color(s_lv_new_screen, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(s_lv_new_screen, LV_OPA_COVER, 0); /* 确保不透明 */

    /* 2. 卡片容器（尺寸保持原逻辑） */
    s_lv_tip_card = lv_obj_create(s_lv_new_screen);
    lv_obj_set_size(s_lv_tip_card, s_screen_width, s_screen_height);
    // if (s_screen_width == 360 && s_screen_height == 360)
    //     lv_obj_set_size(s_lv_tip_card, 300, 180);
    // else if (s_screen_width == 240 && s_screen_height == 240)
    //     lv_obj_set_size(s_lv_tip_card, 220, 120);
    // else if (s_screen_width == 160 && s_screen_height == 160)
    //     lv_obj_set_size(s_lv_tip_card, 132, 72);
    // else
    //     lv_obj_set_size(s_lv_tip_card, 132, 72);

    s_lv_tip_label = lv_label_create(s_lv_tip_card);
    lv_label_set_long_mode(s_lv_tip_label, LV_LABEL_LONG_SCROLL_CIRCULAR); /*Circular scroll*/
    lv_obj_set_width(s_lv_tip_label, 160);
    lv_obj_set_flex_flow(s_lv_tip_label, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_lv_tip_label, LV_FLEX_ALIGN_CENTER, /* 主轴居中（垂直） */
                          LV_FLEX_ALIGN_CENTER,                 /* 交叉轴居中（水平） */
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_center(s_lv_tip_label); /* 如果父容器不是全屏，可让 label 在父容器里居中 */
    lv_label_set_text(s_lv_tip_label, txt);
    // lv_obj_align(s_lv_tip_label, LV_ALIGN_CENTER, 0, 40);

    /* 4. 切换屏幕并立即刷新 */
    lv_screen_load(s_lv_new_screen);
    lv_refr_now(NULL);

    /* 定时自毁并切回旧屏 */
    lv_timer_create(tip_timer_cb, HTTP_DOWNLOAD_TIP_CARD_STAY_MS, NULL);
    lvgl_port_unlock();
}

/**
 * 绘制提示上传界面
 */

/* 创建进度页面 */
static void ui_progress_create(void) {
    lvgl_port_lock(-1);
    PM_LOGI("Creating progress bar");
    if (s_lv_progress)
        return; /* 避免重复创建 */

    s_lv_new_screen = lv_obj_create(NULL); /* 可能为 NULL */
    if (!s_lv_new_screen) {
        PM_LOGW("LVGL not ready, skip progress overlay");
        return;
    }
    /* 1. 全屏容器 */
    s_lv_progress = lv_obj_create(s_lv_new_screen);
    lv_obj_set_size(s_lv_progress, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(s_lv_progress, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_lv_progress, LV_OPA_30, 0);    /* 30% 黑色蒙版 */
    lv_obj_clear_flag(s_lv_progress, LV_OBJ_FLAG_CLICKABLE); /* 允许点击下层 */
    lv_obj_set_scrollbar_mode(s_lv_progress, LV_SCROLLBAR_MODE_OFF);

    /* 2. 进度条 */
    s_lv_progress_bar = lv_bar_create(s_lv_progress);
    if (s_screen_width == 368 && s_screen_height == 368)
        lv_obj_set_size(s_lv_progress_bar, 280, 36);
    else if (s_screen_width == 240 && s_screen_height == 240)
        lv_obj_set_size(s_lv_progress_bar, 200, 24);
    else if (s_screen_width == 160 && s_screen_height == 160)
        lv_obj_set_size(s_lv_progress_bar, 120, 12);
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
    // lv_obj_align_to(s_lv_progress_label, s_lv_progress_bar, LV_ALIGN_OUT_BOTTOM_MID, 0, 3);

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

    lv_screen_load(s_lv_new_screen); /* 切换到新屏幕 */
    lv_refr_now(NULL);
    lvgl_port_unlock();
}

/**
 * 切换回旧屏幕
 */
static void switch_to_old_screen(void) {
    if (s_lv_old_screen) {
        lvgl_port_lock(-1);
        lv_screen_load(s_lv_old_screen); // 切换回旧页面
                                         /* 删除之前创建的屏幕对象 */
        if (s_lv_new_screen) {
            lv_obj_del(s_lv_new_screen); // 现在可以安全删除
            s_lv_new_screen = NULL;
        }
        lvgl_port_unlock();
    }
}

static void ui_progress_destroy(void) {
    if (s_lv_progress) {
        lvgl_port_lock(-1);
        lv_obj_del(s_lv_progress);
        s_lv_progress = s_lv_progress_bar = s_lv_progress_label = NULL;
        lvgl_port_unlock();
    }
}

/**
 * @brief 设置进度条任务的显示内容
 */
static void ui_progress_set_label(const char *txt) {
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
        if (xTaskNotifyWait(0, 0xFFFFFFFF, &msg, portMAX_DELAY) == pdTRUE) {
            ui_cmd_t cmd = UI_NOTIFY_CMD(msg);
            ui_fail_reason_t reason = UI_NOTIFY_REASON(msg);
            uint8_t val = UI_NOTIFY_VAL(msg);
            lvgl_port_lock(-1);

            switch (cmd) {
            case UI_CMD_PROGRESS: {
                if (val > 100) val = 100;
                lv_bar_set_value(s_lv_progress_bar, val, LV_ANIM_OFF);

                char buf[25];
                if (s_paused) {
                    // 你也可以选择：收到 progress 就自动恢复
                    s_paused = false;
                }
                snprintf(buf, sizeof(buf), "download-%u %% (1/2)", val);
                ui_progress_set_label(buf);
                PM_LOGI("》》》Download progress: %u%%", val);
                break;
            }

            case UI_CMD_FILE_WRITE: {
                if (val > 100) val = 100;
                lv_bar_set_value(s_lv_progress_bar, val, LV_ANIM_OFF);

                char buf[25];
                if (s_paused) {
                    // 你也可以选择：收到 progress 就自动恢复
                    s_paused = false;
                }
                snprintf(buf, sizeof(buf), "writing-%u %% (2/2)", val);
                ui_progress_set_label(buf);
                PM_LOGI("》》》Writing progress: %u%%", val);
                break;
            }

            case UI_CMD_PAUSE: {
                s_paused = true;
                // 不动进度条，只更新文字
                ui_progress_set_label("Paused: network lost");
                break;
            }

            case UI_CMD_RESUME: {
                s_paused = false;
                // 恢复提示（不改进度）
                ui_progress_set_label("Resuming...");
                break;
            }

            case UI_CMD_FAIL: {
                s_paused = true;
                switch (reason) {
                case UI_FAIL_NET_DISCONNECT:
                    ui_progress_set_label("Network Error");
                    vTaskDelay(pdMS_TO_TICKS(HTTP_DOWNLOAD_TIP_CARD_STAY_MS));
                    goto _exit;
                    break;
                case UI_FAIL_NO_SPACE:
                    ui_progress_set_label("No storage space");
                    break;
                case UI_FAIL_WRITE_ERROR:
                    ui_progress_set_label("Write failed");
                    break;
                default:
                    ui_progress_set_label("Download failed");
                    break;
                }
                break;
            }
            case UI_CMD_DONE: {
                lv_bar_set_value(s_lv_progress_bar, 100, LV_ANIM_OFF);
                ui_progress_set_label("100 %");
                vTaskDelay(pdMS_TO_TICKS(1000));
                goto _exit;
            }

            case UI_CMD_CANCEL: {
                goto _exit;
            }

            default:
                break;
            }

            lv_refr_now(NULL);
            lvgl_port_unlock();
        }
    }
_exit:
    lvgl_port_unlock();
    ui_progress_destroy();
    switch_to_old_screen();
    vTaskDelete(NULL);
}

void dot_ui_init(lv_obj_t *psd_obj_, uint16_t screen_w, uint16_t screen_h) {
    // 保存屏幕尺寸
    if (screen_w == 360 && screen_h == 360) { // 360要设置成368
        s_screen_width = 368;
        s_screen_height = 368;
    } else {
        s_screen_width = screen_w;
        s_screen_height = screen_h;
    }

    s_psd_obj_ = psd_obj_;

    // 下载进度条UI归零
    progress_percent = 0;

    /* 保存原来的屏幕对象，进度条结束重新显示 */
    s_lv_old_screen = lv_screen_active();
}

lv_obj_t *dot_ui_get_show_lv_obj(void) {
    if (s_psd_obj_) {
        return s_psd_obj_;
    }
    return NULL;
}

void dot_get_ui_screen_size(uint16_t *width, uint16_t *height) {
    if (width)
        *width = s_screen_width;
    if (height)
        *height = s_screen_height;
}

/**
 * 创建下载进度界面
 */
bool download_progress_create(void) {
    /* 1.创建进度页面 */
    ui_progress_create();
    /* 2.初始化进度消息队列 */
    // progress_queue = xQueueCreate(10, sizeof(uint8_t));
    // if (!progress_queue)
    // {
    //     PM_LOGE("Failed to create progress queue");
    //     return false;
    // }
    /* 3.启动LVGL进度任务 */
    if (pdPASS != xTaskCreate(lvgl_progress_task, "lvgl_progress", 4608, NULL, tskIDLE_PRIORITY + 1, &lvgl_progress_task_handle)) {
        PM_LOGE("Failed to create lvgl_progress_task");
        return false;
    }
    return true;
}

/**
 * @brief 供外部更新进度条
 */
void download_progress_update(uint8_t percent) {
    if (!lvgl_progress_task_handle) return;
    if (percent > 100) percent = 100;
    xTaskNotify(lvgl_progress_task_handle, UI_NOTIFY(UI_CMD_PROGRESS, UI_FAIL_NONE, percent), eSetValueWithOverwrite);
}

void download_progress_update_write(uint8_t percent) {
    if (!lvgl_progress_task_handle) return;
    if (percent > 100) percent = 100;
    xTaskNotify(lvgl_progress_task_handle, UI_NOTIFY(UI_CMD_FILE_WRITE, UI_FAIL_NONE, percent), eSetValueWithOverwrite);
}

void download_progress_pause(void) {
    if (!lvgl_progress_task_handle) return;
    xTaskNotify(lvgl_progress_task_handle, UI_NOTIFY(UI_CMD_PAUSE, UI_FAIL_NONE, 0), eSetValueWithOverwrite);
}

void download_progress_resume(void) {
    if (!lvgl_progress_task_handle) return;
    xTaskNotify(lvgl_progress_task_handle, UI_NOTIFY(UI_CMD_RESUME, UI_FAIL_NONE, 0), eSetValueWithOverwrite);
}

void download_progress_fail(ui_fail_reason_t reason) {
    if (!lvgl_progress_task_handle) return;
    xTaskNotify(lvgl_progress_task_handle, UI_NOTIFY(UI_CMD_FAIL, reason, 0), eSetValueWithOverwrite);
}

void download_progress_done(void) {
    if (!lvgl_progress_task_handle) return;
    xTaskNotify(lvgl_progress_task_handle, UI_NOTIFY(UI_CMD_DONE, UI_FAIL_NONE, 100), eSetValueWithOverwrite);
}

/**
 * 下载失败的回调，显示提示浮动框
 */
void download_fail_show_toast(void) {
    /* 创建同步信号量 */
    if (s_tip_done_sem) {
        vSemaphoreDelete(s_tip_done_sem);
    }
    s_tip_done_sem = xSemaphoreCreateBinary();
    if (!s_tip_done_sem) {
        PM_LOGE("Failed to create tip done semaphore");
        return;
    }
    ui_tip_show(HTTP_DOWNLOAD_TIP);

    /* 等待定时器回调释放信号量 */
    if (s_tip_done_sem && xSemaphoreTake(s_tip_done_sem, portMAX_DELAY) == pdTRUE) {
        PM_LOGI("Tip display finished, safe to return");
        vSemaphoreDelete(s_tip_done_sem); // ✅ 用完即删
        s_tip_done_sem = NULL;
    }
    switch_to_old_screen();
}

/* 公共接口：创建“请上传素材”提示页 */
void dot_ui_show_upload_tip(void) {
    lvgl_port_lock(-1);
    PM_LOGI("Creating upload tip");

    /* 1. 新屏幕：纯白背景 */
    s_lv_new_screen = lv_obj_create(NULL);
    if (!s_lv_new_screen) {
        PM_LOGW("LVGL not ready, skip tip overlay");
        return;
    }
    lv_obj_set_style_bg_color(s_lv_new_screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_lv_new_screen, LV_OPA_COVER, 0); /* 确保不透明 */

    /* 居中 label */
    lv_obj_t *img = lv_img_create(s_lv_new_screen);
    lv_img_set_src(img, &icon_upload_80);
    lv_obj_align(img, LV_ALIGN_CENTER, 0, -20);

    lv_obj_t *label = lv_label_create(s_lv_new_screen);
    lv_obj_set_style_text_font(label, &font_puhui_20_4, 0);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 40);
    lv_label_set_text(label, "请连接小程序上传素材");

    // lv_obj_t *label2 = lv_label_create(s_lv_new_screen);
    // lv_obj_set_style_text_font(label2, &font_puhui_20_4, 0);
    // lv_obj_set_style_text_color(label2, lv_color_white(), 0);
    // lv_obj_align(label2, LV_ALIGN_CENTER, 0, 60);
    // lv_label_set_text(label2, "槽位(1/3)");

    lv_obj_move_foreground(img);
    lv_obj_move_foreground(label);
    // lv_obj_move_foreground(label2);

    /* 4. 切换屏幕并立即刷新 */
    lv_screen_load(s_lv_new_screen);
    lv_refr_now(NULL);

    lvgl_port_unlock();
}

/* 公共接口：销毁“请上传素材”提示页 */
void dot_ui_hide_upload_tip(void) {
    if (!s_lv_new_screen) return;
    switch_to_old_screen();
}

/* 公共接口：创建“U盘模式”提示页 */
void dot_ui_show_udisk_tip(void) {
    lvgl_port_lock(-1);
    PM_LOGI("Creating U盘 tip");

    /* 1. 新屏幕：纯白背景 */
    s_lv_new_screen = lv_obj_create(NULL);
    if (!s_lv_new_screen) {
        PM_LOGW("LVGL not ready, skip tip overlay");
        return;
    }
    lv_obj_set_style_bg_color(s_lv_new_screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_lv_new_screen, LV_OPA_COVER, 0); /* 确保不透明 */

    /* 居中 label */
    lv_obj_t *img = lv_img_create(s_lv_new_screen);
    lv_img_set_src(img, &icon_upload_u80);
    lv_obj_align(img, LV_ALIGN_CENTER, 0, -20);

    lv_obj_t *label = lv_label_create(s_lv_new_screen);
    lv_obj_set_style_text_font(label, &font_puhui_20_4, 0);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 40);
    lv_label_set_text(label, "请从电脑传入素材");

    // lv_obj_t *label2 = lv_label_create(s_lv_new_screen);
    // lv_obj_set_style_text_font(label2, &font_puhui_20_4, 0);
    // lv_obj_set_style_text_color(label2, lv_color_white(), 0);
    // lv_obj_align(label2, LV_ALIGN_CENTER, 0, 60);
    // lv_label_set_text(label2, "槽位(1/3)");

    lv_obj_move_foreground(img);
    lv_obj_move_foreground(label);
    // lv_obj_move_foreground(label2);

    /* 4. 切换屏幕并立即刷新 */
    lv_screen_load(s_lv_new_screen);
    lv_refr_now(NULL);

    lvgl_port_unlock();
}

/* 公共接口：销毁“请上传素材”提示页 */
void dot_ui_hide_udisk_tip(void) {
    if (!s_lv_new_screen) return;
    switch_to_old_screen();
}