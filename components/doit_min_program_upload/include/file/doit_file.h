#ifndef __DOIT_FILE_H__
#define __DOIT_FILE_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include "file_common.h"

    void doit_file_init(lv_obj_t *psd_obj_, uint16_t width, uint16_t height);
    void doit_file_decode();
    char *get_show_dir(void);
#if CONFIG_USE_PSD_MULTIPLE
    void doit_file_psd_multi_process(bool go_on);
    void doit_file_psd_set(uint8_t index);
#endif

#ifdef __cplusplus
}
#endif

#endif // __DOIT_FILE_H__