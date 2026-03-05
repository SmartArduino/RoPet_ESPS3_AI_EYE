#ifndef __INC_VPG_DECODE_H__
#define __INC_VPG_DECODE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "dv_common.h"

/* VPG解码 */
void inc_vpg_player_stop(void);
void inc_vpg_player_start(const char *dir_name);

#ifdef __cplusplus
}
#endif

#endif // __INC_VPG_DECODE_H__