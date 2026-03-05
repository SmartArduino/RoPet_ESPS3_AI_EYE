#ifndef __DOT_MP_UI_COMMON_H__
#define __DOT_MP_UI_COMMON_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes */
/* STD APIs */
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* ESP APIs */
#include "esp_event.h"
#include "esp_system.h"

/* FreeRTOS APIs */
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

/* mp_ui APIs */
#include "lvgl.h"
#include "esp_lvgl_port.h"

/* Log APIs */
#include "log_conf.h"

#ifdef __cplusplus
}
#endif

#endif // __DOT_MP_UI_COMMON__