#ifndef __VPG_DECODE_H__
#define __VPG_DECODE_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>
#include "esp_jpeg_common.h"
#include "esp_jpeg_dec.h"
#include "lvgl.h"

    void doit_vpg_player_stop(void);
    void doit_vpg_player_start(const char *dir_name);

#ifdef __cplusplus
}
#endif

#endif // __DOIT_FILE_H__