#ifndef __DOIT_DECODE_H__
#define __DOIT_DECODE_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include "file_common.h"

    extern jpeg_dec_handle_t jpeg_dec;
    extern jpeg_dec_config_t config;

    jpeg_error_t doit_decode_init(void);
    void doit_decode_deinit(void);

#ifdef __cplusplus
}
#endif

#endif // __DOIT_DECODE_H__