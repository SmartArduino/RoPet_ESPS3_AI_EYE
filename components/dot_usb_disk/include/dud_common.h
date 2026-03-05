#ifndef __DUD_COMMON_H__
#define __DUD_COMMON_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes */
/* STD APIs */
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* ESP APIs */
#include "esp_system.h"
#include "esp_check.h"

/* FreeRTOS APIs */
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>



/* Log APIs */
#include "log_conf.h"

#ifdef __cplusplus
}
#endif

#endif // __DUD_COMMON__