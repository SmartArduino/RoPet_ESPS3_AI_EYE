#include "vpg_decode.h"
#include "file_common.h"
#include "doit_decode.h"
#include "doit_ui.h"
#include <stdio.h>
#include <string.h>

#include <stdlib.h>
#include <sys/param.h>
#include <ctype.h>
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_tls.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"

#include "esp_http_client.h"

#include "esp_spiffs.h"
#include "esp_err.h"
#include "esp_vfs.h" // 追加
#include "esp_heap_caps.h"

#include "esp_lvgl_port.h" // 用 lvgl_port_lock/unlock
#include "lvgl.h"

const static char *TAG = "VPG_FILE";

#define VPG_BUFF_NUM 2

static SemaphoreHandle_t buf_free[VPG_BUFF_NUM] = {NULL}; // 缓冲区空闲信号量
static bool vpg_is_init = false;                          // 只建一次标志
// vpg结构体
typedef struct lv_vpg_t
{
    FILE *fp;
    char spiff_path[32]; // 固定 32 字节
    bool is_vpg_player;
    lv_obj_t *img_obj;
    FileHeader fileHeader;
    ItemHeader *ItemHeader;
    uint16_t cur_frame_idx;              // 当前解码帧索引
    uint8_t lv_buf_cur_idx;              // 当前缓冲区索引
    lv_image_dsc_t lv_buf[VPG_BUFF_NUM]; // 双缓冲用于存储jpg解码数

    uint8_t *psram_data; // 文件在 PSRAM 中的基地址 用于将文件预存入psram
    uint32_t psram_size; // 文件总字节数
} lv_vpg_t;

// 播放控制
typedef struct
{
    uint8_t buf_idx;
} vpg_frame_t;

static lv_vpg_t *vpg = NULL;
static TaskHandle_t s_vpg_decode_task = NULL;
static TaskHandle_t s_vpg_show_task = NULL;
static QueueHandle_t jpg_decode_queue = NULL; // jpg解码队列

static volatile bool decode_idle = false; // 解码任务已让出资源
static volatile bool show_idle = false;   // 显示任务已让出资源

/*------------------static,内部使用-------------------------------------------------------------------- */

/**
 * @brief  把内存中的一段 JPG 数据解码为 LVGL 可直接显示的 output_type 图像
 * @param  jpg_data : JPG 原始数据指针
 * @param  jpg_len  : JPG 原始数据长度
 * @param output_buf : 解码后的 output_type 数据缓冲区指针
 * @param out_len   : 解码后的 output_type 数据长度
 * @param out_info : JPG 图像宽高参数
 * @param output_type : 输出格式
 * @retval 成功返回 malloc 的 lv_image_dsc_t*，失败返回 NULL（内部已释放在堆）
 */
static jpeg_error_t decode_jpg_from_mem(const uint8_t *jpg_data, const int jpg_len, lv_image_dsc_t *output_dsc)
{

    // MP_LOGI( "decode open： %ld", (uint32_t)esp_timer_get_time());
    jpeg_error_t ret = JPEG_ERR_OK;

    // 分配到栈里，避免堆分配
    jpeg_dec_io_t jpeg_io = {0};
    jpeg_dec_header_info_t out_info = {0};

    memset(&jpeg_io, 0, sizeof(jpeg_dec_io_t));
    memset(&out_info, 0, sizeof(jpeg_dec_header_info_t));

    jpeg_io.inbuf = jpg_data;
    jpeg_io.inbuf_len = jpg_len;

    ret = jpeg_dec_parse_header(jpeg_dec, &jpeg_io, &out_info);
    if (ret != JPEG_ERR_OK)
        return ret;

    output_dsc->header.w = out_info.width;
    output_dsc->header.h = out_info.height;
    if (config.output_type == JPEG_PIXEL_FORMAT_RGB565_LE ||
        config.output_type == JPEG_PIXEL_FORMAT_RGB565_BE ||
        config.output_type == JPEG_PIXEL_FORMAT_CbYCrY)
    {
        output_dsc->data_size = out_info.width * out_info.height * 2;
    }
    else if (config.output_type == JPEG_PIXEL_FORMAT_RGB888)
    {
        output_dsc->data_size = out_info.width * out_info.height * 3;
    }
    else
    {
        ret = JPEG_ERR_INVALID_PARAM;
        return ret;
    }

    output_dsc->header.cf = LV_COLOR_FORMAT_RGB565; // 必须设

    jpeg_io.outbuf = output_dsc->data;
    ret = jpeg_dec_process(jpeg_dec, &jpeg_io);

    // MP_LOGI( "img decode success %dx%d  %lu bytes", out_info.width, out_info.height, output_dsc->data_size);

    // MP_LOGI( "decode over： %ld", (uint32_t)esp_timer_get_time());
    return ret;
}

// 显示任务
static void vpg_show_task(void *pvParameters)
{
    vpg_frame_t vpg_frame;
    int last_idx = -1; // 上一帧使用的缓冲索引

    while (true)
    {
        show_idle = true; // 空闲状态
        if (!vpg || !vpg->is_vpg_player)
        { /* 停止期让出 CPU */
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        show_idle = false; // 工作状态

        if (xQueueReceive(jpg_decode_queue, &vpg_frame, pdMS_TO_TICKS(10)) == pdTRUE)
        {
            uint8_t idx = vpg_frame.buf_idx;

            lvgl_port_lock(-1);
            lv_image_set_src(vpg->img_obj, &vpg->lv_buf[idx]);
            lv_obj_invalidate(vpg->img_obj);
            lvgl_port_unlock();

            // ✅ 释放“上一帧”的缓冲给解码线程复用（而不是当前帧）
            if (last_idx >= 0)
            {
                xSemaphoreGive(buf_free[last_idx]);
            }
            last_idx = idx;
        }
    }
}

// 解码任务
/**
 * @brief VPG解码任务函数
 * @param pvParameters 任务参数（未使用）
 * @note 该函数是一个无限循环的任务，负责从文件中读取JPG图像数据，解码并发送到队列
 */
static void vpg_decode_task(void *pvParameters)
{
    while (true)
    {
        decode_idle = true; // 空闲状态
        if (!vpg || !vpg->is_vpg_player)
        { /* 停止期让出 CPU */
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        decode_idle = false; // 工作状态

        uint8_t idx = vpg->lv_buf_cur_idx;
        /* === 关键：等这块缓冲区被显示任务释放 === */
        xSemaphoreTake(buf_free[idx], portMAX_DELAY);

        // MP_LOGI( "===decode open===： %ld", (uint32_t)esp_timer_get_time());
        uint32_t cur_fr_offset = vpg->ItemHeader[vpg->cur_frame_idx].offset; // 当前帧偏移
        uint32_t cur_fr_size = vpg->ItemHeader[vpg->cur_frame_idx].size;     // 当前帧大小

        /* 1.一帧数据 */
        // 从文件系统读取这一帧数据
        fseek(vpg->fp, cur_fr_offset, SEEK_SET);
        // const uint8_t *jpg_data = vpg->psram_data + cur_fr_offset;
        memset(vpg->psram_data, 0, vpg->psram_size);
        fread(vpg->psram_data, cur_fr_size, 1, vpg->fp);
        // MP_LOGI( "current frame %d", vpg->cur_frame_idx);

        /* 2.解码JPG数据 */
        if (JPEG_ERR_OK == decode_jpg_from_mem(vpg->psram_data, cur_fr_size, &vpg->lv_buf[idx]))
        {
            vpg_frame_t msg = {.buf_idx = idx};
            xQueueSend(jpg_decode_queue, &msg, 0);
        }
        else
        {
            /* 解码失败，直接归还缓冲区 */
            MP_LOGE( "解码失败，跳过第 %d 帧", vpg->cur_frame_idx);
            xSemaphoreGive(buf_free[idx]);
            continue;
        }

        // doit_img_decode2(vpg->img_obj, (uint8_t *)jpg_data, cur_fr_size); // 解码并显示

        /* 解码下一帧*/
        vpg->lv_buf_cur_idx = (idx + 1) % VPG_BUFF_NUM;
        if (++vpg->cur_frame_idx >= vpg->fileHeader.itemNum)
            vpg->cur_frame_idx = 0; // 循环播放
        // MP_LOGI( "===decode over===： %ld", (uint32_t)esp_timer_get_time());
        // MP_LOGI("vpg->fileHeader.fps=%lu",vpg->fileHeader.fps);
         vTaskDelay(pdMS_TO_TICKS(1000 / (vpg->fileHeader.fps+5))); 
    }
}

/* ---------------- 对外：停止播放 ---------------- */
void doit_vpg_player_stop(void)
{
    if (!vpg || !vpg->is_vpg_player)
        return;

    // 1. 先让任务退出循环
    vpg->is_vpg_player = false;

    // 这里要等待解码和显示任务执行完成
    uint32_t wait_time = pdMS_TO_TICKS(200); // 最多等待200ms
    TickType_t start = xTaskGetTickCount();
    while ((!decode_idle || !show_idle) && (xTaskGetTickCount() - start) < wait_time)
    {
        vTaskDelay(1);
    }
    if ((!decode_idle || !show_idle))
    {
        MP_LOGW( "stop timeout, force suspend");
    }

    if (s_vpg_decode_task)
        vTaskSuspend(s_vpg_decode_task);
    if (s_vpg_show_task)
        vTaskSuspend(s_vpg_show_task);

    /* 3. 把信号量恢复成“全空闲” */
    for (int i = 0; i < VPG_BUFF_NUM; ++i)
    {
        xSemaphoreGive(buf_free[i]);
    }

    int ret = fclose(vpg->fp);
    if (ret != 0)
    {
        MP_LOGE( "fclose error");
    }
    vpg->fp = NULL; // 防止后续误用

    MP_LOGI( "VPG player stopped,Free PSRAM: %d,min free sram=%d", heap_caps_get_free_size(MALLOC_CAP_SPIRAM), heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM));
}

/**
 * @brief 启动VPG视频播放器
 *
 * @param spiff_path VPG文件路径
 * @param img_obj LVGL图像对象指针，用于显示视频帧
 * @param width 视频宽度
 * @param height 视频高度
 *
 * 该函数执行以下操作：
 * 1. 分配VPG播放器结构体内存
 * 2. 初始化播放器参数和缓冲区
 * 3. 读取并验证VPG文件头
 * 4. 读取帧索引表
 * 5. 初始化双缓冲区
 * 6. 创建解码和显示任务
 *
 * @note 如果播放器已在运行，会先停止之前的播放
 * @note 使用SPIRAM内存分配以适应ESP32的内存布局
 * @note 文件格式验证失败时会进行资源清理
 */
void doit_vpg_player_start(const char *dir_name)
{
    MP_LOGI( "加载的动画文件是：%s", dir_name);
    // if (vpg && vpg->is_vpg_player)
    // {
    //     doit_vpg_player_stop();
    // }
     MP_LOGI("doit_vpg_player_start:psram可用大小=%d,psram可用的最大块=%d", heap_caps_get_free_size(MALLOC_CAP_SPIRAM),heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
    if (!vpg_is_init)
    {
        // 初始化vpg
        if (!vpg)
        {
            // 1. 分配vpg结构体内存
            vpg = heap_caps_calloc(1, sizeof(lv_vpg_t), MALLOC_CAP_SPIRAM);
            if (!vpg)
            {
                MP_LOGE( "vpg malloc fail");
                return;
            }

            // 2. 初始化播放器参数和缓冲区
            jpg_decode_queue = xQueueCreate(1, sizeof(vpg_frame_t));
            vpg->img_obj = doit_ui_get_show_lv_obj();

            // 获取屏幕尺寸，分配缓冲区
            uint16_t width = 0, height = 0;
            doit_get_ui_screen_size(&width, &height);
            for (uint16_t i = 0; i < VPG_BUFF_NUM; ++i)
            {
                vpg->lv_buf[i].data = (uint8_t *)heap_caps_aligned_alloc(16, // 对齐单位
                                                                         width * height * 2,
                                                                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                if (!vpg->lv_buf[i].data)
                {
                    MP_LOGE( "PSRAM malloc failed for lv_buf[%d]", i);
                    return;
                }
            }

            // 3. 创建双缓冲区信号量
            for (int i = 0; i < VPG_BUFF_NUM; ++i)
            {
                buf_free[i] = xSemaphoreCreateBinary();
                xSemaphoreGive(buf_free[i]); // 初始：两块都空闲
            }

            /*4. 创建启动解码任务和显示任务 */
            BaseType_t ret = xTaskCreatePinnedToCore(vpg_show_task, "vpg_show_task", 1536, NULL, 6, &s_vpg_show_task, 1);
            if (ret != pdPASS)
                MP_LOGE( "show task create fail, ret=%d", ret);
            else
            {
                MP_LOGI( "show task create ok, handle=%p", s_vpg_show_task);
                vTaskSuspend(s_vpg_show_task); // 先挂起
            }

            ret = xTaskCreatePinnedToCore(vpg_decode_task, "vpg_decode_task", 1728, NULL, 6, &s_vpg_decode_task, 1);
            if (ret != pdPASS)
                MP_LOGE( "decode task create fail, ret=%d", ret);
            else
            {
                MP_LOGI( "decode task create ok, handle=%p", s_vpg_decode_task);
                vTaskSuspend(s_vpg_decode_task); // 先挂起
            }

            MP_LOGI( "VPG player started,Free PSRAM: %d", heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM));

            vpg_is_init = true;
            MP_LOGI( "VPG资源初始化完成");
        }
    }

    /* 1.初始化参数 */
    vpg->is_vpg_player = true;
    vpg->cur_frame_idx = 0;
    snprintf(vpg->spiff_path, 32, "/littlefs/%s", dir_name);
    vpg->lv_buf_cur_idx = 0; // 当前缓冲区索引
    vpg->psram_size = 0;
    if (vpg->psram_data) // 释放之前分配的内存
        heap_caps_free(vpg->psram_data);
    if (vpg->ItemHeader)
        heap_caps_free(vpg->ItemHeader);

    /*  2.读取文件头,判断文件格式*/
    vpg->fp = fopen(vpg->spiff_path, "rb");
    if (vpg->fp == NULL)
    {
        /* code */
        MP_LOGE( "文件读取失败");
        MP_LOGE( "fopen 失败: %s (errno=%d)", strerror(errno), errno);
        return;
    }

    fseek(vpg->fp, 0, SEEK_END);
    vpg->psram_size = ftell(vpg->fp);
    fseek(vpg->fp, 0, SEEK_SET);
    MP_LOGI( "【VPG】文件总大小=%lu 字节", vpg->psram_size);

    // 读文件头
    if (fread(&vpg->fileHeader, sizeof(FileHeader), 1, vpg->fp) != 1)
    {
        MP_LOGE( "fileHeader read fail");
        return;
    }
    if (vpg->fileHeader.magic != 0xAABBCCDD)
    {
        MP_LOGE( "【VPG】魔数错误，不是合法 VPG 文件");
        return;
    }

    // 分配并读索引表到 PSRAM（只占 itemNum*sizeof(ItemHeader)）
    size_t idx_bytes = vpg->fileHeader.itemNum * sizeof(ItemHeader);
    vpg->ItemHeader = (ItemHeader *)heap_caps_malloc(idx_bytes, MALLOC_CAP_SPIRAM);
    if (!vpg->ItemHeader)
    {
        MP_LOGE( "vpg->ItemHeader heap_caps_malloc fail");
        return;
    }
    if (fread(vpg->ItemHeader, idx_bytes, 1, vpg->fp) != 1)
    {
        MP_LOGE( "vpg->ItemHeader read index fail");
        return;
    }

    uint32_t max_frame_size = 0;
    for (uint16_t i = 0; i < vpg->fileHeader.itemNum; i++)
    {
        if (vpg->ItemHeader[i].size > max_frame_size)
        {
            max_frame_size = vpg->ItemHeader[i].size;
        }
    }
    MP_LOGI( "本次一帧最大的大小是=%lu", max_frame_size);

    // 分配缓冲区
    vpg->psram_data = (uint8_t *)heap_caps_malloc(max_frame_size, MALLOC_CAP_SPIRAM);
    vpg->psram_size = max_frame_size;

    /* 5. 恢复任务 */
    vTaskResume(s_vpg_show_task);
    vTaskResume(s_vpg_decode_task);

    // 调试日志
    // for (int i = 0; i < vpg->fileHeader.itemNum; ++i)
    // {
    //     MP_LOGI( "【VPG】第 %d 帧，大小=%lu 字节, 偏移=%lu", i, vpg->ItemHeader[i].size, vpg->ItemHeader[i].offset);
    //     MP_LOG_BUFFER_HEX("【VPG】", vpg->psram_data + vpg->ItemHeader[i].offset, 10);
    // }

    return;
}
