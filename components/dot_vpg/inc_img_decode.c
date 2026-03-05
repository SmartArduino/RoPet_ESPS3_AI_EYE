#include "inc_img_decode.h"
#include "dv_common.h"
#include "inc_decode.h"
#include "lvgl.h"
#include "esp_lvgl_port.h"
#include "esp_jpeg_dec.h"
#include "page_manager.h"

static const char *TAG = "dot_img_decode";

static lv_img_dsc_t dot_file_img_dsc_ = {0}; // 用于显示的图片

// 获取LittleFs图片并解码返回可以被lvgl渲染的数据
void inc_img_decode(const char *dir_name) {
    DV_LOGI("dot_img_decode: %s", dir_name);
    uint8_t *out_buf = NULL;
    jpeg_error_t ret = JPEG_ERR_OK;

    // 分配到栈里，避免堆分配
    jpeg_dec_io_t jpeg_io = {0};
    jpeg_dec_header_info_t out_info = {0};

    memset(&jpeg_io, 0, sizeof(jpeg_dec_io_t));
    memset(&out_info, 0, sizeof(jpeg_dec_header_info_t));

    // 读取LittleFs到PSRAM中
    FILE *f = fopen(dir_name, "rb");
    if (!f) {
        DV_LOGE("fopen fail");
        ret = JPEG_ERR_FAIL;
        goto jpeg_dec_failed;
    }

    fseek(f, 0, SEEK_END);  // 将文件指针移动到文件末尾
    int jpg_len = ftell(f); // 获取文件长度
    fseek(f, 0, SEEK_SET);  // 将文件指针重新定位到文件开头
    uint8_t *jpg_buf = (uint8_t *)heap_caps_malloc(jpg_len, MALLOC_CAP_SPIRAM);
    fread(jpg_buf, 1, jpg_len, f);
    fclose(f);

    DV_LOGI("file size = %d", jpg_len);
    if (jpg_len < 2)
        goto jpeg_dec_failed;
    ESP_LOG_BUFFER_HEX(TAG, jpg_buf, 16); // 看前 16 字节

    // 为I/O 控制结构体设置图片数据
    jpeg_io.inbuf = jpg_buf;
    jpeg_io.inbuf_len = jpg_len;

    // 解析 JPEG 图片的头部信息和图片数据。
    ret = jpeg_dec_parse_header(jpeg_dec, &jpeg_io, &out_info);
    if (ret != JPEG_ERR_OK) {
        goto jpeg_dec_failed;
    }

    // 分配输出数据缓冲区，并更新 inbuf 指针和 inbuf 的长度
    // 实现里，解析完 header 后把值带出来
    uint32_t pix_len = out_info.width * out_info.height * 3;

    if (jpeg_dec_config.output_type == JPEG_PIXEL_FORMAT_RGB565_LE || jpeg_dec_config.output_type == JPEG_PIXEL_FORMAT_RGB565_BE || jpeg_dec_config.output_type == JPEG_PIXEL_FORMAT_CbYCrY) {
        pix_len = out_info.width * out_info.height * 2;
    } else if (jpeg_dec_config.output_type == JPEG_PIXEL_FORMAT_RGB888) {
        pix_len = out_info.width * out_info.height * 3;
    } else {
        ret = JPEG_ERR_INVALID_PARAM;
        goto jpeg_dec_failed;
    }
    out_buf = heap_caps_aligned_alloc(16, // 对齐单位
                                      pix_len,
                                      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (out_buf == NULL) {
        ret = JPEG_ERR_NO_MEM;
        goto jpeg_dec_failed;
    }
    jpeg_io.outbuf = out_buf;

    // 解码图片
    ret = jpeg_dec_process(jpeg_dec, &jpeg_io);
    if (ret != JPEG_ERR_OK) {
        goto jpeg_dec_failed;
    }

    DV_LOGI("img decode success %dx%d  %lu bytes", out_info.width, out_info.height, pix_len);
    dot_file_img_dsc_.header.cf = LV_COLOR_FORMAT_RGB565;
    dot_file_img_dsc_.header.w = out_info.width;
    dot_file_img_dsc_.header.h = out_info.height;
    dot_file_img_dsc_.data_size = pix_len;
    dot_file_img_dsc_.data = out_buf;

    lvgl_port_lock(-1);
    lv_image_set_src(page_manager_get_home_img_obj(), &dot_file_img_dsc_);
    lvgl_port_unlock();

    // Decoder deinitialize
jpeg_dec_failed:
    if (out_buf) {
        heap_caps_free(out_buf);
    }
    // return ret;
}
