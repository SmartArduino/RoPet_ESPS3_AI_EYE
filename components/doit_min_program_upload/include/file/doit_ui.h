#ifndef __DOIT_UI_H__
#define __DOIT_UI_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include "file_common.h"


/**
 * 进度条cmd
 */
typedef enum {
    UI_CMD_PROGRESS = 0, // 文件下载。参数：percent(0~100)
    UI_CMD_FILE_WRITE = 1, // 文件写入。参数：percent(0~100)
    UI_CMD_PAUSE    = 2, // 暂停（网络断开等）
    UI_CMD_RESUME   = 3, // 恢复
    UI_CMD_FAIL     = 4, // 失败（显示失败，不退出）
    UI_CMD_DONE     = 5, // 完成（100%并退出）
    UI_CMD_CANCEL   = 6, // 取消（退出）
} ui_cmd_t;

/**
 * 下载失败原因
 */
typedef enum {
    UI_FAIL_NONE = 0,

    UI_FAIL_NET_DISCONNECT = 1,   // 网络断开
    UI_FAIL_TIMEOUT        = 2,   // 超时
    UI_FAIL_NO_SPACE       = 3,   // 存储空间不足
    UI_FAIL_NO_PSRAM       = 4,   // PSRAM 不足
    UI_FAIL_HTTP_ERROR     = 5,   // HTTP 状态码错误
    UI_FAIL_TLS_ERROR      = 6,   // TLS 握手/证书错误
    UI_FAIL_WRITE_ERROR    = 7,   // 文件写入失败
    UI_FAIL_USER_CANCEL    = 8,   // 用户取消

} ui_fail_reason_t;

    void doit_ui_init(lv_obj_t *psd_obj_, uint16_t screen_w, uint16_t screen_h);
    lv_obj_t *doit_ui_get_show_lv_obj(void);
    bool download_progress_create(void);
    void download_fail_show_toast(void);
    void doit_get_ui_screen_size(uint16_t *width, uint16_t *height);
    
    void download_progress_update(uint8_t percent);
    void download_progress_update_write(uint8_t percent);
    void download_progress_pause(void);
    void download_progress_resume(void);
    void download_progress_fail(ui_fail_reason_t reason);
    void download_progress_done(void);

#ifdef __cplusplus
}
#endif

#endif // __DOIT_UI__