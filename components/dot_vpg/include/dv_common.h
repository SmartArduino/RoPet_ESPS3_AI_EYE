#ifndef __DV_COMMON_H__
#define __DV_COMMON_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes */
/* STD APIs */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ESP APIs */

/* FreeRTOS APIs */
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/event_groups.h>

/* vpg decode APIs */

/* Log APIs */
#include "log_conf.h"

#pragma pack(push, 1) // 设置结构体为1字节对齐
typedef struct
{
    uint32_t offset; // 4 bytes offset
    uint32_t size;   // 2 bytes size
} ItemHeader;

typedef struct
{
    uint32_t magic;       // 4 bytes offset
    uint32_t size;        // 2 bytes size
    uint32_t itemNum;     // 2 bytes size
    uint32_t fps : 8;     // 2 bytes size     //补丁
    uint32_t height : 12; // 2 bytes size    //补丁，没用上，只用来对其字节
    uint32_t width : 12;  // 2 bytes size     //补丁，没用上，只用来对其字节
} FileHeader;
#pragma pack(pop) // 恢复对齐方式

#ifdef __cplusplus
}
#endif

#endif // __DV_COMMON__