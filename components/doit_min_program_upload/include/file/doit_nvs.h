#ifndef __DOIT_NVS_H__
#define __DOIT_NVS_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>

#define MIN_PROGRAM_NVS "min_program"
#define MIN_PROGRAM_IMG "min_program_img"
#define MIN_PROGRAM_VPG "min_program_vpg"

    void doit_nvs_set(const char *type);
    char *doit_nvs_get(void);

#ifdef __cplusplus
}
#endif

#endif // __DOIT_NVS__