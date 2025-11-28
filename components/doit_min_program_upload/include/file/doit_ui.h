#ifndef __DOIT_UI_H__
#define __DOIT_UI_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include "file_common.h"
    void doit_ui_init(lv_obj_t *psd_obj_, uint16_t screen_w, uint16_t screen_h);
    lv_obj_t *doit_ui_get_show_lv_obj(void);
    bool download_progress_create(void);
    void download_progress_update(uint8_t percent);
    void download_fail_show_toast(void);
    void doit_get_ui_screen_size(uint16_t *width, uint16_t *height);
#ifdef __cplusplus
}
#endif

#endif // __DOIT_UI__