#ifndef __EXT_VPG_SVC_H__
#define __EXT_VPG_SVC_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "dv_common.h"

void dot_vpg_decode_init(void);
void dot_vpg_decode_deinit(void);
void dot_img_show(const char *dir_name);
void dot_vpg_start(const char *dir_name);
void dot_vpg_stop(void);

#ifdef __cplusplus
}
#endif

#endif // __EXT_VPG_SVC_H__