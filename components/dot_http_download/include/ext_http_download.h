#ifndef __EXT_HTTP_DOWNLOAD_H__
#define __EXT_HTTP_DOWNLOAD_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "dhd_common.h"

/**
 * 下载失败原因
 */
typedef enum {
    HTTP_DL_SUC = 0,
    HTTP_DL_FAIL_NET_DISCONNECT = 1, // 网络断开
    HTTP_DL_FAIL_TIMEOUT = 2,        // 超时
    HTTP_DL_FAIL_NO_SPACE = 3,       // 存储空间不足
    HTTP_DL_FAIL__NO_PSRAM = 4,      // PSRAM 不足
    HTTP_DL_FAIL_HTTP_ERROR = 5,     // HTTP 状态码错误
    HTTP_DL_FAIL_TLS_ERROR = 6,      // TLS 握手/证书错误
    HTTP_DL_FAIL_WRITE_ERROR = 7,    // 文件写入失败
    HTTP_DL_FAIL__USER_CANCEL = 8,   // 用户取消
    HTTP_DL_FAIL_UNKNOW = 9,         // 未知错误
    HTTP_DL_FAIL_CODE_NUM
} http_dl_fail_reason_t;

typedef struct
{
    http_dl_fail_reason_t err_code;
    char *path; // 成功时指向 SPIFFS 完整路径（动态分配，调用者 free）
    char *type; // 成功时指向文件类型（动态分配，调用者 free）
} doit_file_result_t;

/* 回调函数 */
typedef void (*http_dl_prog_cb_t)(uint8_t precent,uint8_t stage); // 下载进度的回调

doit_file_result_t dot_http_download(const char *url);
void dot_http_dl_init(http_dl_prog_cb_t prog_cb);

#ifdef __cplusplus
}
#endif

#endif // __EXT_HTTP_DOWNLOAD_H__