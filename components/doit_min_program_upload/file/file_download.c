#include "doit_file.h"
#include "doit_ui.h"
#include <stdio.h>
#include <string.h>

#include <stdlib.h>
#include <sys/param.h>
#include <ctype.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_tls.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_system.h"

#include "esp_http_client.h"

#include "esp_err.h"
#include "esp_vfs.h" // 追加
#include "esp_heap_caps.h"

#include "esp_littlefs.h"

#include "doit_nvs.h"
#include "vpg_decode.h"

static const char *TAG = "file_download";

/*==========================宏定义===================================*/
#define HTTP_DOWNLOAD_CHUNK_BUFFER (128 * 1024) //  128K 字节
#define HTTP_DOWNLOAD_STREAM_BUFFER 20480
#define HTTP_DOWNLOAD_MAX_SIZE (8 * 1024 * 1024) // 最大下载文件8MB
#define PROGRESS_UPDATE_THRESHOLD (128 * 1024)   // 进度更新阈值(128KB)

/*===================================================================*/

static uint32_t s_content_len = 0; // 文件总长度

typedef struct
{
    FILE *file_handle;       // 文件句柄
    uint8_t *buf_in_ram;     // 攒写缓冲区（PSRAM）
    uint32_t already_in_buf; // 缓冲区里现在已多少字节
    char *final_path;        // 完整路径，回头要给主函数用
    uint32_t total_written;  // 统计：已经写盘的总字节
    uint32_t file_total;     // 服务器发过来的文件总大小
} http_save_file_t;

/* =============静态函数声明===================*/
static bool is_http_file_content_length_overflow(const char *url);
static doit_file_result_t to_download(const char *url, const char *dir_name);
static void switch_to_old_screen(void);
static char *get_file_name_in_url(const char *url);
static char *get_file_type_in_url(const char *url);
static const char *detect_file_type(const char *data, int len);
static doit_file_result_t http_download_chunk(const char *file_url, const char *dir_name);
static doit_file_result_t http_perform_as_stream_reader(const char *file_url);
/* ==========================================*/

/*===========================================文件下载相关函数=============================================================== */
/* 销毁浮动进度页面 */

esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    static uint32_t last_update = 0; // 上次更新时的已下载字节数
    http_save_file_t *store = (http_save_file_t *)evt->user_data;
    if (store == NULL)
        return ESP_FAIL;
    switch (evt->event_id)
    {
    case HTTP_EVENT_ERROR:
        MP_LOGI("HTTP_EVENT_ERROR");
        break;
    case HTTP_EVENT_ON_CONNECTED:
        MP_LOGI("HTTP_EVENT_ON_CONNECTED");
        break;
    case HTTP_EVENT_HEADER_SENT:
        MP_LOGI("HTTP_EVENT_HEADER_SENT");
        break;
    case HTTP_EVENT_ON_HEADER:
        MP_LOGI("HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
        if (strcasecmp(evt->header_key, "Content-Length") == 0)
        {
            uint32_t content_length = atol(evt->header_value);
            store->file_total = content_length;
            last_update = 0;
        }
        break;
    case HTTP_EVENT_ON_DATA:
        // MP_LOGI("HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
        uint8_t *data = (uint8_t *)evt->data; // 获取一个数据块
        uint32_t data_len = evt->data_len;    // 数据的长度
        uint32_t data_to_copy = data_len;     // 剩余需要保存的数据

        // 开始存储数据
        while (data_to_copy > 0)
        {
            // 当前缓冲区可以存放的字节数
            uint32_t buf_can_save = HTTP_DOWNLOAD_CHUNK_BUFFER - store->already_in_buf;
            uint32_t time_byte = data_to_copy < buf_can_save ? data_to_copy : buf_can_save; // 本次要拷贝的字节数

            // 拷贝数据到缓冲区
            memcpy(store->buf_in_ram + store->already_in_buf, data, time_byte);

            store->already_in_buf += time_byte;
            data += time_byte;
            data_to_copy -= time_byte;

            // 如果缓冲区满了，写入文件
            if (store->already_in_buf == HTTP_DOWNLOAD_CHUNK_BUFFER)
            {
                int wlen = fwrite(store->buf_in_ram, 1, HTTP_DOWNLOAD_CHUNK_BUFFER, store->file_handle);

                MP_LOGI("fwrite=%d", wlen);

                // g_done += evt->data_len;
                // if (g_total)
                // {
                //     uint8_t percent = (g_done * 100) / g_total;
                //     ui_overlay_set_progress(percent);
                // }
                // int wlen = fwrite(store->buf_in_ram, 1, HTTP_DOWNLOAD_CHUNK_BUFFER, store->file_handle);
                // if (wlen != HTTP_DOWNLOAD_CHUNK_BUFFER)
                // {
                //     vTaskDelay(pdMS_TO_TICKS(10));
                //     MP_LOGE("fwrite=%d", wlen);
                // }

                store->total_written += HTTP_DOWNLOAD_CHUNK_BUFFER;
                store->already_in_buf = 0;

                // 进度更新：如果文件总大小已知，则计算百分比并发送到队列
                if (store->file_total > 0)
                {
                    if (store->total_written - last_update >= PROGRESS_UPDATE_THRESHOLD || store->total_written == store->file_total)
                    {
                        last_update = store->total_written;
                        uint8_t percent = (store->total_written * 100) / store->file_total;
                        download_progress_update(percent); // 更新下载进度
                    }
                }

                MP_LOGI("数据写入，总计 %lu KB", store->total_written / 1024);
            }
        }
        break;
    case HTTP_EVENT_ON_FINISH:
        MP_LOGI("HTTP_EVENT_ON_FINISH");
        if (store->already_in_buf > 0)
        {
            size_t wlen = fwrite(store->buf_in_ram, 1, store->already_in_buf, store->file_handle);
            if (wlen != store->already_in_buf)
            {
                MP_LOGE("末尾 fwrite 失败 %d vs %lu", wlen, store->already_in_buf);
            }
            store->total_written += store->already_in_buf;
            store->already_in_buf = 0;
        }
        fflush(store->file_handle); // 强制落盘

        // 确保最终进度为100%
        if (store->file_total > 0)
        {
            uint8_t percent = (store->total_written * 100) / store->file_total;
            download_progress_update(percent);
        }

        MP_LOGI("[HTTP_EVENT_ON_FINISH] 文件写入完成，总计 %lu 字节 (%lu KB)",
                 store->total_written, store->total_written / 1024);
        break;
    case HTTP_EVENT_DISCONNECTED:
        MP_LOGI("HTTP_EVENT_DISCONNECTED");
        break;
    case HTTP_EVENT_REDIRECT:
        MP_LOGI("HTTP_EVENT_REDIRECT");
        break;
    }

    return ESP_OK;
}

/*
    http事件回调下载方式
*/
static doit_file_result_t http_download_chunk(const char *file_url, const char *dir_name)
{
    doit_file_result_t ret = {.err_code = CL_OPRET_SUCCESS, .path = NULL, .type = NULL};
    /* 1 取文件名 */
    char *file_name = get_file_name_in_url(file_url);
    /* 2.取文件类型*/
    char *file_type = get_file_type_in_url(file_url);
    // if (!file_name)

    /* 5.2 拼 FS 路径 */
    char full_path[32] = {0};
    snprintf(full_path, 32, "/littlefs/%s", dir_name);
    // 读取结构体初始化
    http_save_file_t save = {0};               // 初始化
    save.file_handle = fopen(full_path, "wb"); // 打开文件，如果不存在则创建
    save.buf_in_ram = (uint8_t *)heap_caps_malloc(HTTP_DOWNLOAD_CHUNK_BUFFER, MALLOC_CAP_SPIRAM);
    save.final_path = full_path;
    save.already_in_buf = 0;
    save.total_written = 0;

    if (!save.file_handle || !save.buf_in_ram)
    {
        if (save.file_handle)
        {
            fclose(save.file_handle);
        }

        if (save.buf_in_ram)
        {
            heap_caps_free(save.buf_in_ram);
        }

        ret.err_code = CL_OPERT_FAIL;
        return ret;
    }
    // 配置 HTTP 客户端 */
    esp_http_client_config_t config = {
        .url = file_url,
        .event_handler = http_event_handler,
        .user_data = &save,
        .buffer_size = HTTP_DOWNLOAD_CHUNK_BUFFER,
        // .buffer_size_tx = 1024, // ← 必须 >0，否则不分段
        // .is_async = false,      // ← 显式用“同步事件”模式
        .keep_alive_enable = true,
    };
    // 阻塞下载直到服务器发完
    MP_LOGI("HTTP chunk encoding request =>");
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK)
    {
        MP_LOGI("HTTP chunk encoding Status = %d, content_length = %" PRId64,
                 esp_http_client_get_status_code(client),
                 esp_http_client_get_content_length(client));
        ret.path = strdup(save.final_path); // 成功：把路径带回去
        ret.type = strdup(file_type);       // 成功：把文件类型带回去
    }
    else
    {
        MP_LOGE("Error perform http request %s", esp_err_to_name(err));
    }
    esp_http_client_cleanup(client);
    /* ← 在这里加日志 */
    MP_LOGI("[http_download_chunk] 文件写入完成，总计 %lu 字节 (%lu KB)",
             save.total_written, save.total_written / 1024);
    fclose(save.file_handle);
    heap_caps_free(save.buf_in_ram);
    DIR *dir = opendir("/littlefs");
    if (dir)
    {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL)
        {
            MP_LOGI("【目录项】%s", entry->d_name);
        }
        closedir(dir);
    }
    else
    {
        MP_LOGE("【错误】无法打开 /littlefs 目录");
    }

    return ret;
}

/*
    http流式轮询下载方式
*/
static doit_file_result_t http_perform_as_stream_reader(const char *file_url)
{
    doit_file_result_t ret = {.err_code = CL_OPRET_SUCCESS, .path = NULL};

    // 1.http配置
    esp_http_client_config_t config = {
        .url = file_url,
        .keep_alive_enable = true,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err;

    if ((err = esp_http_client_open(client, 0)) != ESP_OK)
    {
        MP_LOGE("Failed to open HTTP connection: %s", esp_err_to_name(err));
        ret.err_code = CL_OPERT_FAIL;
        return ret;
    }

    int content_length = esp_http_client_fetch_headers(client);
    MP_LOGI("Content length = %d", content_length);

    // 1.先读取512个字节用于判断是什么文件类型
    char *probe = malloc(512);
    int probe_len = esp_http_client_read(client, probe, 512);
    if (probe_len <= 0)
    {
        MP_LOGE("No data received");
        ret.err_code = CL_OPERT_FAIL;
        return ret;
    }

    // 输出文件内容
    MP_LOGI("probe_len = %d", probe_len);
    ESP_LOG_BUFFER_HEX(TAG,probe, probe_len);

    // 2.判断文件类型
    const char *ext = detect_file_type(probe, 512);
    MP_LOGI("File type detected: %s", ext);

    // 3.获取文件名称
    char *file_name = get_file_name_in_url(file_url);
    // if (file_name)   //文件名相同，保证覆盖文件，如果改成大容量sd卡，开启if
    // {
    //     MP_LOGI("file_name: %s\n", file_name);
    // }
    // else
    // {
    file_name = "vpg";
    // }

    // littlefs完整路径
    char *littlefs_path = malloc(64);
    if (!littlefs_path)
    {
        MP_LOGE("malloc littlefs_path fail");
        ret.err_code = CL_OPERT_FAIL;
        return ret;
    }
    snprintf(littlefs_path, 64, // 不区分文件名和扩展名
             "/littlefs/%s", file_name);
    // if (strcmp(ext, "jpg") == 0 || strcmp(ext, "jpeg") == 0)
    // {
    //     snprintf(littlefs_path, 64,
    //              "/littlefs/%s.%s", file_name, ext);
    // }
    // else if (strcmp(ext, "vpg") == 0)
    // {
    //     snprintf(littlefs_path, 64,
    //              "/littlefs/%s.%s", file_name, ext);
    // }

    MP_LOGI("littlefs_path: %s\n", littlefs_path);

    // 4.把头文件写入
    FILE *fp = fopen(littlefs_path, "wb");
    if (!fp)
    {
        MP_LOGE("Failed to create file %s", littlefs_path);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        ret.err_code = CL_OPERT_FAIL;
        return ret;
    }
    fwrite(probe, 1, probe_len, fp);

    /* 循环读剩余数据 */
    char *buffer = (char *)heap_caps_malloc(HTTP_DOWNLOAD_STREAM_BUFFER + 1, MALLOC_CAP_SPIRAM);
    if (buffer == NULL)
    {
        MP_LOGE("Cannot malloc http receive buffer");
        ret.err_code = CL_OPERT_FAIL;
        return ret;
    }

    int read_len, total = probe_len;
    MP_LOGI("total = %d,content_length = %d", total, content_length);
    do // 不要依赖 Content-Length 控制循环。改成“读到没数据为止”的流式写入
    {
        read_len = esp_http_client_read(client, buffer, HTTP_DOWNLOAD_STREAM_BUFFER);
        if (read_len > 0)
        {
            int wlen = 0;
            do
            {
                wlen = fwrite(buffer, 1, read_len, fp);
                total += read_len;
                if (wlen != read_len)
                {
                    vTaskDelay(pdMS_TO_TICKS(10));
                    MP_LOGE("fwrite=%d", wlen);
                }
                MP_LOGI("数据写入，总计 %d KB", total / 1024);

            } while (wlen != read_len);
        }
    } while (read_len > 0);
    // while (total < content_length)
    // {
    //     read_len = esp_http_client_read(client, buffer, HTTP_DOWNLOAD_STREAM_BUFFER);
    //     if (read_len <= 0)
    //         break;
    //     fwrite(buffer, 1, read_len, fp);
    //     total += read_len;

    // }
    fclose(fp);
    free(buffer);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    MP_LOGI("Download complete, written %d bytes -> %s", total, littlefs_path);
    ret.path = littlefs_path;
    return ret;
}

/*======================================================================================================================= */

static bool is_http_file_content_length_overflow(const char *url)
{
    MP_LOGI("Checking file size for URL: %s", url);
    bool ret = true;

    /* 1.获取文件长度 */
    s_content_len = 0;
    esp_http_client_config_t head_cfg = {
        .url = url,
        .method = HTTP_METHOD_HEAD, // 只获取响应头
    };

    esp_http_client_handle_t head = esp_http_client_init(&head_cfg);
    if (esp_http_client_perform(head) == ESP_OK)
    {
        s_content_len = esp_http_client_get_content_length(head);
        MP_LOGI("File size: %lu bytes", s_content_len);
    }

    /* 2.判断文件是否过大 */
    if (s_content_len > HTTP_DOWNLOAD_MAX_SIZE)
    {
        MP_LOGW("File size exceeds limit of %d bytes,Skip download", HTTP_DOWNLOAD_MAX_SIZE);
        ret = true;
    }
    else
    {
        ret = false;
    }
    esp_http_client_cleanup(head);
    return ret;
}

static doit_file_result_t to_download(const char *url, const char *dir_name)
{

    // 创建下载进度条
    if (!download_progress_create())
        return (doit_file_result_t){.err_code = CL_OPERT_FAIL, .path = NULL};
    /* 下载文件 */
    return http_download_chunk(url, dir_name);
}

/* 从 url 中提取纯文件名（不含扩展名），成功返回 malloc 的字符串，失败返回 NULL */
static char *get_file_name_in_url(const char *url)
{
    if (!url)
        return NULL;

    /* 1. 定位到文件名起始 */
    const char *slash = strrchr(url, '/');       // trrchr 在字符串中从后往前找第一个 '/'，返回它的地址；如果没找到返回 NULL
    const char *start = slash ? slash + 1 : url; // 如果找到了 '/'（slash != NULL），start 就指向 '/' 的下一个字符，也就是文件名的开头。如果 URL 里没有 '/'，就把整个字符串当成文件名，start = url。

    /* 2. 定位扩展名 */
    const char *dot = strrchr(start, '.');                         // 在 start（即文件名）里从后往前找第一个 '.'，返回它的地址；如果没找到返回 NULL。
    size_t name_len = dot ? (size_t)(dot - start) : strlen(start); // 如果找到了 '.'（dot != NULL），copy_len = dot - start，也就是文件名部分的长度（不含扩展名）。如果没找到 '.'，copy_len = strlen(start)，也就是整个字符串的长度（没有扩展名）。

    /* 3.  malloc 并拷贝 */
    char *out = malloc(name_len + 1);
    if (!out)
        return NULL;

    memcpy(out, start, name_len);
    out[name_len] = '\0';
    return out;
}

/* 返回静态字符串，URL 探测与 文件通用 */
static const char *detect_file_type(const char *data, int len)
{
    MP_LOGI("Detecting file type...,len = %d", len);
    if (len < 4)
        return "bin"; // 太短无法判断

    /* 自定义的VPG格式 */
    FileHeader *fh = (FileHeader *)data;
    if (fh->magic == 0xAABBCCDD) // vng格式头
    {
        MP_LOGI("VPG detected: size=%" PRIu32 " itemNum=%" PRIu32 " fps=%" PRIu32,
                 fh->size, fh->itemNum, fh->fps);
        return "vpg";
    }
    else
    {
        MP_LOGI("11VPG detected: size=%" PRIu32 " itemNum=%" PRIu32 " fps=%" PRIu32,
                 fh->size, fh->itemNum, fh->fps);
    }

    /* JPEG */
    if (data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF)
        return "jpg";

    /* PNG */
    if (data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G')
        return "png";

    /* GIF */
    if (len >= 6 &&
        (memcmp(data, "GIF89a", 6) == 0 || memcmp(data, "GIF87a", 6) == 0))
        return "gif";

    /* MP4 / ISO BMFF：ftyp 在 offset 4 */
    if (len >= 12 && memcmp(data + 4, "ftyp", 4) == 0)
        return "mp4";

    /* BMP */
    if (data[0] == 'B' && data[1] == 'M')
        return "bmp";

    /* WEBP */
    if (len >= 12 && memcmp(data + 8, "WEBP", 4) == 0)
        return "webp";

    /* 默认 */
    return "bin";
}

static char *get_file_type_in_url(const char *url)
{
    char *dot = strrchr(url, '.');
    if (dot)
    {
        MP_LOGI("get file type is %s in url %s", (dot + 1), url);
        return strdup(dot + 1);
    }
    return "";
}

// 下载图片，返回littlefs存储路径
doit_file_result_t doit_file_download(const char *url, const char *dir_name)
{
    doit_vpg_player_stop(); // 先停止当前播放的VPG视频
    MP_LOGI("Downloading file from : %s", url);
    doit_file_result_t ret = {.err_code = CL_OPRET_SUCCESS, .path = NULL};

    /* 1. 校验文件大小 */
    if (is_http_file_content_length_overflow(url))
    {
        /* 1.1 文件大小超过限制，直接返回失败 */

        /* 展示 tip 并阻塞等待其结束 */
        download_fail_show_toast();
        ret.err_code = CL_OPRET_FILE_OVERFLOW;
        return (doit_file_result_t){.err_code = CL_OPRET_FILE_OVERFLOW, .path = NULL};
    }
    else
    {
        /* 2.2 正常下载流程 */
        doit_file_result_t download_ret = to_download(url, dir_name);
        if (download_ret.err_code == CL_OPRET_SUCCESS)
            ret.err_code = download_ret.err_code;
    }
    return ret;
}

#include "mbedtls/md5.h"
void md5_test(void *buf, uint32_t len)
{
    MP_LOGI("MD5 加密示例\n");

    // 初始化 MD5 上下文
    mbedtls_md5_context md5_ctx;
    unsigned char output[16]; // 输出 MD5 散列值

    mbedtls_md5_init(&md5_ctx);
    mbedtls_md5_starts(&md5_ctx);           // 开始 MD5 计算
    mbedtls_md5_update(&md5_ctx, buf, len); // 更新输入数据
    mbedtls_md5_finish(&md5_ctx, output);   // 完成计算并获取结果

    MP_LOGI("MD5 加密后 (32位): ");
    for (int i = 0; i < 16; i++)
    {
        printf("%02x", output[i]); // 输出为十六进制格式
    }
    printf("\n");

    mbedtls_md5_free(&md5_ctx); // 释放上下文资源
}
