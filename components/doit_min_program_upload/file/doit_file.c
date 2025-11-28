#include "doit_file.h"
#include "doit_decode.h"
#include "img_decode.h"
#include "vpg_decode.h"
#include "doit_nvs.h"
#include "doit_ui.h"
#include <esp_err.h>

#include <esp_log.h>
#include <esp_vfs.h> // 追加
#include <esp_heap_caps.h>
#include <esp_littlefs.h>
#include <nvs_flash.h>
#include <esp_netif.h>
#include "esp_lvgl_port.h"

static const char *TAG = "doit_file";

#if CONFIG_USE_PSD_MULTIPLE
static uint8_t s_psd_multi_num = CONFIG_USE_PSD_MULTIPLE_NUM;
static uint8_t s_psd_multi_index = 1; // 多素材索引
#endif

char *get_show_dir(void)
{
#if CONFIG_USE_PSD_ONE
    return CONFIG_USE_PSD_ONE_FILE_NAME;
#elif CONFIG_USE_PSD_MULTIPLE
    switch (s_psd_multi_index)
    {
#ifdef CONFIG_USE_PSD_MULTIPLE_FILE_FIRST
    case 1:
        return CONFIG_USE_PSD_MULTIPLE_FILE_FIRST;
#endif
#ifdef CONFIG_USE_PSD_MULTIPLE_FILE_SECOND
    case 2:
        return CONFIG_USE_PSD_MULTIPLE_FILE_SECOND;
#endif
#ifdef CONFIG_USE_PSD_MULTIPLE_FILE_THIRD
    case 3:
        return CONFIG_USE_PSD_MULTIPLE_FILE_THIRD;
#endif
#ifdef CONFIG_USE_PSD_MULTIPLE_FILE_FOURTH
    case 4:
        return CONFIG_USE_PSD_MULTIPLE_FILE_FOURTH;
#endif
    default:
        return CONFIG_USE_PSD_MULTIPLE_FILE_FIRST;
        break;
    }
#endif
    return "";
}

/**
 * @brief 从文件系统中检测当前文件的类型
 */
static char *detect_file_type_from_fs(void)
{
    char full_path[32];
    snprintf(full_path, 32, "/littlefs/%s", get_show_dir());
    // 读取LittleFs到PSRAM中
    uint8_t head[20] = {0}; // 读取前20字节
    FILE *f = fopen(full_path, "rb");
    if (!f)
    {
        ESP_LOGE(TAG, "fopen fail");
    }
    size_t len = fread(head, 1, sizeof(head), f);
    fclose(f);
    /* 自定义的VPG格式 */
    /* ④ VPG 自定义格式 */
    if (len >= sizeof(FileHeader))
    {
        FileHeader *fh = (FileHeader *)head;
        if (fh->magic == 0xAABBCCDD)
        {
            ESP_LOGI(TAG, "detech file type: vpg");
            return "vpg";
        }
    }

    if (len >= 3 && head[0] == 0xFF && head[1] == 0xD8 && head[2] == 0xFF) /* JPEG */
    {
        ESP_LOGI(TAG, "detech file type: jpg");
        return "jpg";
    }

    if (len >= 4 && memcmp(head, "\x89PNG", 4) == 0) /* PNG */
    {
        ESP_LOGI(TAG, "detech file type: png");
        return "png";
    }

    if (len >= 6 && (memcmp(head, "GIF89a", 6) == 0 || memcmp(head, "GIF87a", 6) == 0)) /* GIF */
    {
        ESP_LOGI(TAG, "detech file type: gif");
        return "gif";
    }

    if (len >= 12 && memcmp(head + 4, "ftyp", 4) == 0) /* MP4 / ISO BMFF：ftyp 在 offset 4 */
    {
        ESP_LOGI(TAG, "detech file type:mp4");
        return "mp4";
    }

    if (len >= 2 && head[0] == 'B' && head[1] == 'M') /* BMP */
    {
        ESP_LOGI(TAG, "detech file type:bmp");
        return "bmp";
    }

    if (len >= 12 && memcmp(head + 8, "WEBP", 4) == 0) /* WEBP */
    {
        ESP_LOGI(TAG, "detech file type:webp");
        return "webp";
    }

    /* 默认 */
    return "bin";
}

void doit_file_init(lv_obj_t *psd_obj_, uint16_t width, uint16_t height)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_OK)
    {
        ESP_LOGD(TAG, "NVS initialized by component");
    }

    err = esp_netif_init();
    if (err == ESP_OK)
    {
        ESP_LOGD(TAG, "TCP/IP adapter initialized by component");
    }
    if (err == ESP_ERR_INVALID_STATE)
    {
        ESP_LOGD(TAG, "TCP/IP adapter already initialized by user");
        return;
    }
    ESP_ERROR_CHECK(err);

    // 挂载文件系统spifss
    ESP_LOGI(TAG, "Initializing LITTLEFS...");
    esp_vfs_littlefs_conf_t conf = {
        .base_path = "/littlefs",
        .partition_label = "storage",
        .format_if_mount_failed = true,
        .dont_mount = false,
    };

    // Use settings defined above to initialize and mount LittleFS filesystem.
    // Note: esp_vfs_littlefs_register is an all-in-one convenience function.
    esp_err_t ret = esp_vfs_littlefs_register(&conf);

    if (ret != ESP_OK)
    {
        if (ret == ESP_FAIL)
        {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        }
        else if (ret == ESP_ERR_NOT_FOUND)
        {
            ESP_LOGE(TAG, "Failed to find LittleFS partition");
        }
        else
        {
            ESP_LOGE(TAG, "Failed to initialize LittleFS (%s)", esp_err_to_name(ret));
        }
        return;
    }

    size_t total = 0, used = 0;
    ret = esp_littlefs_info(conf.partition_label, &total, &used);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to get LittleFS partition information (%s)", esp_err_to_name(ret));
        esp_littlefs_format(conf.partition_label);
    }
    else
    {
        ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
    }

    DIR *dir = opendir("/littlefs");
    if (dir)
    {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL)
        {
            ESP_LOGI(TAG, "【目录项】%s", entry->d_name);
        }
        closedir(dir);
    }
    else
    {
        ESP_LOGE(TAG, "【错误】无法打开 /littlefs 目录");
    }

    // 初始化UI
    doit_ui_init(psd_obj_, width, height);
    ESP_LOGI(TAG, "UI initialized with width: %d, height: %d", width, height);

    // 初始化解码器
    doit_decode_init();
}

void doit_file_decode(void)
{
    // 显示前先读取数据的类型
    doit_vpg_player_stop(); // 先停止当前播放的VPG视频

    char *file_type = detect_file_type_from_fs();
    if (strcmp(file_type, "jpg") == 0)
    {
        // 计算图片缩放比例
#if CONFIG_LCD_ST77916_360X360
        float scale = 360.0f / 368.0f;
#elif CONFIG_LCD_GC9A01_240X240 || CONFIG_LCD_ST7796_240X240
        float scale = 240.0f / 368.0f;
#elif CONFIG_LCD_GC9A01_160X160
        float scale = 160.0f / 368.0f;
#endif
        uint16_t scale_val = (uint16_t)(scale * 256.0f + 0.5f); // +0.5 四舍五入
        lvgl_port_lock(-1);
        lv_image_set_scale(doit_ui_get_show_lv_obj(), scale_val);
        lvgl_port_unlock();
        doit_img_decode(get_show_dir());
    }
    else if (strcmp(file_type, "vpg") == 0)
    {
        lvgl_port_lock(-1);
        lv_image_set_scale(doit_ui_get_show_lv_obj(), 256);
        lvgl_port_unlock();
        doit_vpg_player_start(get_show_dir());
    }
}

#if CONFIG_USE_PSD_MULTIPLE
void doit_file_psd_multi_process(bool go_on)
{
    if (go_on)
    {
        // 切换到下一个文件目录
        s_psd_multi_index = (s_psd_multi_index % s_psd_multi_num) + 1;
        ESP_LOGI(TAG, "切换到下一个素材,索引=%d", s_psd_multi_index);
        doit_file_decode();
    }
    else
    {
        ESP_LOGI(TAG, "退出");
        esp_restart();
    }
}

void doit_file_psd_set(uint8_t index)
{
    // 切换到下一个文件目录
    s_psd_multi_index = index;
    ESP_LOGI(TAG, "切换到下一个素材,索引=%d", s_psd_multi_index);
    doit_file_decode();
}
#endif
