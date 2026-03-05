#ifndef __DIFF_COMMON_H__
#define __DIFF_COMMON_H__

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

/* fat APIs */


/* Log APIs */
#include "log_conf.h"

#ifdef __cplusplus
}
#endif

#endif // __DIFF_COMMON__