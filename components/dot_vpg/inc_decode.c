#include "inc_decode.h"

// 全局唯一解码器
jpeg_dec_handle_t jpeg_dec = NULL;
jpeg_dec_config_t jpeg_dec_config = DEFAULT_JPEG_DEC_CONFIG();

jpeg_error_t inc_decode_init(void) {
    jpeg_error_t ret = JPEG_ERR_OK;

    // Create jpeg_dec handle
    jpeg_dec_config.output_type = JPEG_PIXEL_FORMAT_RGB565_LE;
    jpeg_dec_config.rotate = JPEG_ROTATE_0D;
    // config->scale.width       = 0;
    // config->scale.height      = 0;
    // config->clipper.width     = 0;
    // config->clipper.height    = 0;

    ret = jpeg_dec_open(&jpeg_dec_config, &jpeg_dec);
    if (ret != JPEG_ERR_OK) {
        DV_LOGE("jpeg decoder open fail,ret = %d", ret);
        return ret;
    }

    return ret;
}

void inc_decode_deinit() {
    // Decoder deinitialize
    if (jpeg_dec != NULL) {
        jpeg_dec_close(jpeg_dec);
        jpeg_dec = NULL;
    }
}
