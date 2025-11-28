#ifndef __IMG_DECODE_H__
#define __IMG_DECODE_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include "file_common.h"

    jpeg_error_t doit_img_decode(const char *dir_name);
    // jpeg_error_t doit_img_decode2(lv_obj_t *img_obj, uint8_t *jpg_buf, int jpg_len);

#ifdef __cplusplus
}
#endif

#endif // __IMG_DECODE_H__