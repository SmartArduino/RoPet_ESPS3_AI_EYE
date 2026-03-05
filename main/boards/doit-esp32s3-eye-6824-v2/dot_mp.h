
#ifndef __DOT_MP_H__
#define __DOT_MP_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "stdio.h"
#include "stdint.h"

typedef enum {
    MPU_MODE_RUN = 0,           // 运行模式
    MPU_MODE_ONLINE_UPDATE = 0, // 在线更新模式
    MPU_MODE_USB = 1,           // USB模式
    MPU_MODE_NUM
} mpu_mode_t;

void switch_next_psd(void);
uint8_t dot_mp_get_psd_index(void);
uint8_t dot_mp_get_platform_idx(void);
void dot_mp_set_psd_index(uint8_t index);
void dot_mp_set_platform_idx(uint8_t index);
void dot_mp_start_play(void);
void dot_mp_stop_play(void);
void dot_mp_switchRunOrUSB(void);
#ifdef __cplusplus
}
#endif

#endif // __DOT_MP_H__
