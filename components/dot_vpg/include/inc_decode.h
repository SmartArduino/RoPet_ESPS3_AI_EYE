#ifndef __INC_DECODE_H__
#define __INC_DECODE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "dv_common.h"
#include "esp_jpeg_dec.h"

extern jpeg_dec_handle_t jpeg_dec;
extern jpeg_dec_config_t jpeg_dec_config;

jpeg_error_t inc_decode_init(void);
void inc_decode_deinit(void);

#ifdef __cplusplus
}
#endif

#endif // __INC_DECODE_H__