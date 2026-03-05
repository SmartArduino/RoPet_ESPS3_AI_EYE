#ifndef __PROGRESS_H__
#define __PROGRESS_H__

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 进度条cmd
 */
typedef enum {
    UI_PROG_CMD_PROGRESS = 0,   // 文件下载。参数：percent(0~100)
    UI_PROG_CMD_FILE_WRITE = 1, // 文件写入。参数：percent(0~100)
    UI_PROG_CMD_PAUSE = 2,      // 暂停（网络断开等）
    UI_PROG_CMD_RESUME = 3,     // 恢复
    UI_PROG_CMD_FAIL = 4,       // 失败（显示失败，不退出）
    UI_PROG_CMD_DONE = 5,       // 完成（100%并退出）
    UI_PROG_CMD_CANCEL = 6,     // 取消（退出）
} ui_prog_cmd_t;

/**
 * 对应下载失败原因的UI
 */
typedef enum {
    UI_PROG_FAIL_NONE = 0,

    UI_PROG_FAIL_NET_DISCONNECT = 1, // 网络断开
    UI_PROG_FAIL_TIMEOUT = 2,        // 超时
    UI_PROG_FAIL_NO_SPACE = 3,       // 存储空间不足
    UI_PROG_FAIL_NO_PSRAM = 4,       // PSRAM 不足
    UI_PROG_FAIL_HTTP_ERROR = 5,     // HTTP 状态码错误
    UI_PROG_FAIL_TLS_ERROR = 6,      // TLS 握手/证书错误
    UI_PROG_FAIL_WRITE_ERROR = 7,    // 文件写入失败
    UI_PROG_FAIL_USER_CANCEL = 8,    // 用户取消
    UI_PROG_FAIL_UNKNOWN = 9,        // 未知错误
    UI_PROG_FAIL_NUM

} ui_prog_fail_reason_t;

void ui_prog_update(uint8_t percent);
void ui_prog_update_write(uint8_t percent);
void ui_prog_pause(void);
void ui_prog_resume(void);
void ui_prog_fail(ui_prog_fail_reason_t reason);
bool ui_prog_fail_wait(ui_prog_fail_reason_t reason, uint32_t timeout_ms);
void ui_prog_done(void);

#ifdef __cplusplus
}
#endif

#endif