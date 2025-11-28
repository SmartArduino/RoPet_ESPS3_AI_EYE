#ifndef __FILE_COMMON_H__
#define __FILE_COMMON_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>
#include "esp_jpeg_common.h"
#include "esp_jpeg_dec.h"
#include "lvgl.h"

#pragma pack(push, 1) // 设置结构体为1字节对齐
    typedef struct
    {
        uint32_t offset; // 4 bytes offset
        uint32_t size;   // 2 bytes size
    } ItemHeader;

    typedef struct
    {
        uint32_t magic;   // 4 bytes offset
        uint32_t size;    // 2 bytes size
        uint32_t itemNum; // 2 bytes size
        uint32_t fps;     // 2 bytes size
    } FileHeader;
#pragma pack(pop) // 恢复对齐方式

    typedef enum
    {
        CL_OPRET_SUCCESS = 0,       // 下载成功
        CL_OPERT_FAIL = 1,          // 下载失败,可能是内存不足或网络中断
        CL_OPRET_BUSY = 2,          // 设备忙，可能正在下载其他资源
        CL_OPRET_NET_ERR = 3,       // 网络错误
        CL_OPRET_BT_ERR = 4,        // 蓝牙错误
        CL_OPRET_FILE_OVERFLOW = 5, // 文件过大，超过限制
    } cl_operation_code_t;

    typedef struct
    {
        cl_operation_code_t err_code; // ESP_OK 表示成功，其余为失败原因
        char *path;                   // 成功时指向 SPIFFS 完整路径（动态分配，调用者 free）
        char *type;                   // 成功时指向文件类型（动态分配，调用者 free）
    } doit_file_result_t;

#ifdef __cplusplus
}
#endif

#endif // __FILE_COMMON__