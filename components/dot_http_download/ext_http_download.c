#include "ext_http_download.h"
#include "ext_vpg_svc.h"
#include "ext_fat_svc.h"

#include <sys/param.h>
#include <ctype.h>
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_tls.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_system.h"
#include <dirent.h>

static const char *TAG = "ext_http_download";

/*==========================宏定义===================================*/
#define HTTP_DOWNLOAD_CHUNK_BUFFER (256 * 1024) //  128K 字节
#define HTTP_DOWNLOAD_STREAM_BUFFER 20480

// #if CONFIG_USE_PSD_MULTIPLE
// #define HTTP_DOWNLOAD_MAX_SIZE ((7 * 1024 * 1024)/CONFIG_USE_PSD_MULTIPLE_NUM) // 最大可下载的文件大小每个槽位相同
// #else
#define HTTP_DOWNLOAD_MAX_SIZE (7 * 1024 * 1024) // 最大下载文件7MB
// #endif

#define PROGRESS_UPDATE_THRESHOLD (128 * 1024) // 进度更新阈值(128KB)

#define FS_WRITE_CHUNK (128 * 1024)
/*===================================================================*/

static uint32_t s_content_len = 0; // 文件总长度

typedef struct
{
    // FILE *file_handle;       // 文件句柄
    uint8_t cur_sta;                // 当前状态,0:初始化,1:正在下载，2：下载完成，正在写入
    bool is_success;                // 是否成功下载
    http_dl_fail_reason_t err_code; // 错误码
    uint8_t *buf_in_ram;            // 攒写缓冲区（PSRAM）
    uint32_t already_in_buf;        // 缓冲区里现在已多少字节
    char *final_path;               // 完整路径，回头要给主函数用
    uint32_t total_written;         // 统计：已经写盘的总字节
    uint32_t file_total;            // 服务器发过来的文件总大小
} http_save_file_t;

/* =============静态函数声明===================*/
static bool is_http_file_content_length_overflow(const char *url);
static doit_file_result_t to_download(const char *url, const char *dir_name);
static char *get_file_name_in_url(const char *url);
static char *get_file_type_in_url(const char *url);
static const char *detect_file_type(const char *data, int len);
static doit_file_result_t http_download_chunk(const char *file_url, const char *dir_name);
static doit_file_result_t http_perform_as_stream_reader(const char *file_url);
/* ==========================================*/

/* 下载回调函数 */
static http_dl_prog_cb_t http_dl_prog_cb = NULL;

/**
 * @brief HTTP事件处理回调函数
 *
 * 该函数用于处理HTTP客户端的各种事件，包括连接建立、数据接收、下载完成等。
 * 主要功能包括：
 * 1. 处理HTTP响应头，获取文件总大小
 * 2. 管理下载进度，包括内存缓冲和文件写入
 * 3. 处理网络错误和断开连接情况
 *
 * @param evt HTTP事件结构体指针，包含事件类型和相关数据
 * @return esp_err_t 返回ESP_OK表示成功，ESP_FAIL表示失败
 *
 * @note 该函数会在以下事件中被调用：
 * - HTTP_EVENT_ERROR: 发生错误
 * - HTTP_EVENT_ON_CONNECTED: 连接建立
 * - HTTP_EVENT_HEADER_SENT: 请求头发送完成
 * - HTTP_EVENT_ON_HEADER: 接收到响应头
 * - HTTP_EVENT_ON_DATA: 接收到数据
 * - HTTP_EVENT_ON_FINISH: 下载完成
 * - HTTP_EVENT_DISCONNECTED: 连接断开
 * - HTTP_EVENT_REDIRECT: 重定向
 */
esp_err_t http_event_handler(esp_http_client_event_t *evt) {
    static uint32_t last_update = 0; // 上次更新时的已下载字节数
    http_save_file_t *store = (http_save_file_t *)evt->user_data;
    if (store == NULL)
        return ESP_FAIL;
    switch (evt->event_id) {
    case HTTP_EVENT_ERROR:
        DHD_LOGI("HTTP_EVENT_ERROR");
        break;
    case HTTP_EVENT_ON_CONNECTED:
        DHD_LOGI("HTTP_EVENT_ON_CONNECTED");
        break;
    case HTTP_EVENT_HEADER_SENT:
        DHD_LOGI("HTTP_EVENT_HEADER_SENT");
        break;
    case HTTP_EVENT_ON_HEADER:
        DHD_LOGI("HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
        if (strcasecmp(evt->header_key, "Content-Length") == 0) {
            uint32_t content_length = atol(evt->header_value);
            store->file_total = content_length;
            last_update = 0;

            // 判断当前psram能否存下文件
            DHD_LOGI("psram可用大小=%d,psram可用的最大块=%d", heap_caps_get_free_size(MALLOC_CAP_SPIRAM), heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
            if (content_length > 0 && heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) > content_length) {
                store->buf_in_ram = (uint8_t *)heap_caps_malloc(content_length, MALLOC_CAP_SPIRAM);
                DHD_LOGI("缓冲区足够,分配缓冲区大小=%d", content_length);
            }
        }
        break;
    case HTTP_EVENT_ON_DATA:
        // DHD_LOGI("HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
        if (!store->buf_in_ram || store->file_total == 0) {
            DHD_LOGE("no PSRAM buffer or unknown file_total, abort");
            return ESP_FAIL;
        }

        store->cur_sta = 1;
        //  /* 直接写入全量缓冲区，直到收到完 file_total 字节 */
        memcpy(store->buf_in_ram + store->already_in_buf, evt->data, evt->data_len);
        store->already_in_buf += evt->data_len;
        // DHD_LOGI("Download  %lu Bytes...",store->already_in_buf);
        uint8_t percent = (store->already_in_buf * 100) / store->file_total;
        if (last_update != percent) {
            // DHD_LOGI("Download  %lu Bytes...", store->already_in_buf);
            if (http_dl_prog_cb) http_dl_prog_cb(percent, store->cur_sta); // 下载进度回调
            last_update = percent;
        }
        break;
    case HTTP_EVENT_ON_FINISH:
        DHD_LOGI("HTTP_EVENT_ON_FINISH");

        // 如果下载完成，写入文件
        if (store->already_in_buf == store->file_total) {
            store->cur_sta = 2;
            DHD_LOGI("缓冲区已经写满，文件开始写入%s", store->final_path);
            FILE *f = fopen(store->final_path, "wb"); // 打开文件，如果不存在则创建
            store->total_written = 0;
            uint8_t percent;
            while (store->total_written < store->file_total) {
                size_t pre_write_num = MIN(FS_WRITE_CHUNK, store->file_total - store->total_written);
                size_t real_write = fwrite(store->buf_in_ram + store->total_written, 1, pre_write_num, f);
                if (pre_write_num != real_write) {
                    DHD_LOGE("fwrite fail off=%u", store->total_written);
                    fclose(f);
                    break;
                }
                store->total_written += pre_write_num;
                percent = (store->total_written * 100) / store->file_total;
                if (http_dl_prog_cb) http_dl_prog_cb(percent, store->cur_sta); // 进度回调
                vTaskDelay(pdMS_TO_TICKS(1));                                  // 给个延时
            }
            fflush(f);
            fclose(f);
            /* ← 在这里加日志 */
            DHD_LOGI("文件写入完成，总计 %lu 字节 (%lu KB)",
                     store->total_written, store->total_written / 1024);
            store->is_success = true;
            store->err_code = HTTP_DL_SUC;
        } else { // 可能传输过程中网络波动导致数据下载不完全，判断失败
            DHD_LOGE("下载失败，http连接未知中断");
            store->err_code = HTTP_DL_FAIL_UNKNOW;
            store->is_success = false;
            return ESP_FAIL;
        }

        break;
    case HTTP_EVENT_DISCONNECTED:
        DHD_LOGI("HTTP_EVENT_DISCONNECTED");
        int sock_err = esp_http_client_get_errno(evt->client); // errno，若无效返回 -1
        int tls_err = 0, tls_flags = 0;
        esp_http_client_get_and_clear_last_tls_error(evt->client, &tls_err, &tls_flags);
        DHD_LOGI("HTTP_EVENT_DISCONNECTED, errno=%d, tls_err=0x%x, tls_flags=0x%x",
                 sock_err, tls_err, tls_flags);
        if (sock_err == 113 && tls_err == 0 && tls_flags == 0) {
            DHD_LOGE("下载失败，网络连接超时");
            store->err_code = HTTP_DL_FAIL_NET_DISCONNECT;
            store->is_success = false;
            DHD_LOGI("cur_sta=%d", store->cur_sta);
        }
        break;
    case HTTP_EVENT_REDIRECT:
        DHD_LOGI("HTTP_EVENT_REDIRECT");
        break;
    }

    return ESP_OK;
}

/**
 * @brief 通过HTTP分块下载文件到本地文件系统
 *
 * @param file_url 要下载的文件的URL地址
 * @param dir_name 本地保存目录名
 * @return doit_file_result_t 返回下载结果，包含错误码、文件路径和文件类型
 *
 * @note 函数执行流程：
 * 1. 从URL中提取文件类型
 * 2. 构建本地保存路径
 * 3. 初始化HTTP下载配置
 * 4. 执行文件下载
 * 5. 清理资源并返回结果
 */
static doit_file_result_t http_download_chunk(const char *file_url, const char *dir_name) {
    doit_file_result_t ret = {.err_code = HTTP_DL_SUC, .path = NULL, .type = NULL};
    /* 1 取文件名 */
    // char *file_name = get_file_name_in_url(file_url);
    /* 2.取文件类型*/
    char *file_type = get_file_type_in_url(file_url);
    // 读取结构体初始化
    http_save_file_t save = {0}; // 初始化

    char full_path[32];
    snprintf(full_path, sizeof(full_path), "%s/%s", CONFIG_FILE_BASE_PATH, dir_name);
    // save.buf_in_ram = (uint8_t *)heap_caps_malloc(HTTP_DOWNLOAD_CHUNK_BUFFER, MALLOC_CAP_SPIRAM);
    DHD_LOGI("准备下载文件到目录：%s", full_path);
    save.final_path = full_path;
    save.cur_sta = 0;
    save.is_success = false;
    save.already_in_buf = 0;
    save.total_written = 0;

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
    DHD_LOGI("HTTP chunk encoding request =>");
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);
    
    int status_code = esp_http_client_get_status_code(client);
    int64_t content_length = esp_http_client_get_content_length(client);
    
    esp_http_client_cleanup(client);
    if (err == ESP_OK) {
        DHD_LOGI("HTTP chunk encoding Status = %d, content_length = %" PRId64,
                 status_code, content_length);
        
        // 检查HTTP状态码，只有2xx才表示成功
        if (status_code < 200 || status_code >= 300) {
            DHD_LOGE("HTTP download failed, status=%d (expected 2xx)", status_code);
            // 删除已写入的错误文件
            if (save.final_path[0] != '\0') {
                unlink(save.final_path);
                DHD_LOGI("Deleted invalid file: %s", save.final_path);
            }
            ret.err_code = HTTP_DL_FAIL_UNKNOW;
        } else if (save.is_success) {
            ret.path = strdup(save.final_path); // 成功：把路径带回去
            ret.type = strdup(file_type);       // 成功：把文件类型带回去
            ret.err_code = HTTP_DL_SUC;
        } else {
            ret.err_code = save.err_code;
        }

    } else {
        ret.err_code = HTTP_DL_FAIL_UNKNOW;
        DHD_LOGE("Error perform http request %s", esp_err_to_name(err));
    }

    heap_caps_free(save.buf_in_ram);
    save.buf_in_ram = NULL;
    save.already_in_buf = 0;

    return ret;
}

/**
 * @brief 通过HTTP流式下载文件并保存到本地文件系统
 *
 * @param file_url 要下载的文件URL
 * @return doit_file_result_t 返回操作结果，包含错误码和文件路径
 *
 * 该函数执行以下操作：
 * 1. 初始化HTTP客户端并建立连接
 * 2. 读取前512字节用于检测文件类型
 * 3. 根据URL生成目标文件路径
 * 4. 流式下载并写入文件
 * 5. 清理资源并返回结果
 */
static doit_file_result_t http_perform_as_stream_reader(const char *file_url) {
    doit_file_result_t ret = {.err_code = HTTP_DL_SUC, .path = NULL};

    // 1.http配置
    esp_http_client_config_t config = {
        .url = file_url,
        .keep_alive_enable = true,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err;

    if ((err = esp_http_client_open(client, 0)) != ESP_OK) {
        DHD_LOGE("Failed to open HTTP connection: %s", esp_err_to_name(err));
        ret.err_code = HTTP_DL_FAIL_UNKNOW;
        return ret;
    }

    int content_length = esp_http_client_fetch_headers(client);
    DHD_LOGI("Content length = %d", content_length);

    // 1.先读取512个字节用于判断是什么文件类型
    char *probe = malloc(512);
    int probe_len = esp_http_client_read(client, probe, 512);
    if (probe_len <= 0) {
        DHD_LOGE("No data received");
        ret.err_code = HTTP_DL_FAIL_UNKNOW;
        return ret;
    }

    // 输出文件内容
    DHD_LOGI("probe_len = %d", probe_len);
    ESP_LOG_BUFFER_HEX(TAG, probe, probe_len);

    // 2.判断文件类型
    const char *ext = detect_file_type(probe, 512);
    DHD_LOGI("File type detected: %s", ext);

    // 3.获取文件名称
    char *file_name = get_file_name_in_url(file_url);
    // if (file_name)   //文件名相同，保证覆盖文件，如果改成大容量sd卡，开启if
    // {
    //     DHD_LOGI("file_name: %s\n", file_name);
    // }
    // else
    // {
    file_name = "vpg";
    // }

    // littlefs完整路径
    char *littlefs_path = malloc(64);
    if (!littlefs_path) {
        DHD_LOGE("malloc littlefs_path fail");
        ret.err_code = HTTP_DL_FAIL_UNKNOW;
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

    DHD_LOGI("littlefs_path: %s\n", littlefs_path);

    // 4.把头文件写入
    FILE *fp = fopen(littlefs_path, "wb");
    if (!fp) {
        DHD_LOGE("Failed to create file %s", littlefs_path);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        ret.err_code = HTTP_DL_FAIL_UNKNOW;
        return ret;
    }
    fwrite(probe, 1, probe_len, fp);

    /* 循环读剩余数据 */
    char *buffer = (char *)heap_caps_malloc(HTTP_DOWNLOAD_STREAM_BUFFER + 1, MALLOC_CAP_SPIRAM);
    if (buffer == NULL) {
        DHD_LOGE("Cannot malloc http receive buffer");
        ret.err_code = HTTP_DL_FAIL_UNKNOW;
        return ret;
    }

    int read_len, total = probe_len;
    DHD_LOGI("total = %d,content_length = %d", total, content_length);
    do // 不要依赖 Content-Length 控制循环。改成“读到没数据为止”的流式写入
    {
        read_len = esp_http_client_read(client, buffer, HTTP_DOWNLOAD_STREAM_BUFFER);
        if (read_len > 0) {
            int wlen = 0;
            do {
                wlen = fwrite(buffer, 1, read_len, fp);
                total += read_len;
                if (wlen != read_len) {
                    vTaskDelay(pdMS_TO_TICKS(10));
                    DHD_LOGE("fwrite=%d", wlen);
                }
                DHD_LOGI("数据写入，总计 %d KB", total / 1024);

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
    DHD_LOGI("Download complete, written %d bytes -> %s", total, littlefs_path);
    ret.path = littlefs_path;
    return ret;
}

/**
 * @brief 检查HTTP文件内容长度是否超出存储限制
 *
 * 通过HEAD请求获取文件大小，并与LittleFS可用空间进行比较，判断是否可以下载该文件
 *
 * @param url 要检查的文件URL地址
 * @return true 文件大小超出可用空间限制，不应下载
 * @return false 文件大小在允许范围内，可以下载
 *
 * @note 该函数会执行以下操作：
 *       1. 发送HEAD请求获取文件大小
 *       2. 获取LittleFS总空间和已用空间信息
 *       3. 比较文件大小与可用空间
 *       4. 返回是否允许下载的判断结果
 */
static bool is_http_file_content_length_overflow(const char *url) {
    DHD_LOGI("Checking file size for URL: %s", url);
    bool ret = false;

    /* 1.获取文件长度 */
    s_content_len = 0;
    esp_http_client_config_t head_cfg = {
        .url = url,
        .method = HTTP_METHOD_HEAD, // 只获取响应头
    };

    esp_http_client_handle_t head = esp_http_client_init(&head_cfg);
    if (esp_http_client_perform(head) == ESP_OK) {
        s_content_len = esp_http_client_get_content_length(head);
        DHD_LOGI("File size: %lu bytes", s_content_len);
    }

    /* 2.判断文件是否过大 */
    ret = dot_fat_is_file_size_overflow(s_content_len);
    esp_http_client_cleanup(head);
    return ret;
}

/**
 * @brief 下载文件到指定目录
 *
 * @param url 要下载的文件URL
 * @param dir_name 下载文件保存的目标目录名
 * @return doit_file_result_t 返回下载结果，包含错误代码和文件路径
 *         - err_code: 操作错误代码
 *         - path: 下载文件的保存路径
 */
static doit_file_result_t to_download(const char *url, const char *dir_name) {
    /* 下载文件 */
    return http_download_chunk(url, dir_name);
}

/**
 * @brief 从URL中提取文件名（不包含扩展名）
 *
 * @param url 输入的URL字符串
 * @return char* 返回新分配的内存，包含提取的文件名（不包含扩展名）。
 *               如果输入为NULL或内存分配失败，返回NULL。
 *               调用者负责释放返回的内存。
 */
static char *get_file_name_in_url(const char *url) {
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

/**
 * @brief 检测文件类型
 *
 * 根据文件头的魔数（magic number）来判断文件类型。
 * 支持检测的文件类型包括：VPG、JPEG、PNG、GIF、MP4、BMP、WEBP。
 * 如果无法识别或文件太短，则返回"bin"。
 *
 * @param data 文件数据的指针
 * @param len 文件数据的长度
 * @return const char* 返回文件类型字符串：
 *         - "vpg": 自定义VPG格式
 *         - "jpg": JPEG格式
 *         - "png": PNG格式
 *         - "gif": GIF格式
 *         - "mp4": MP4格式
 *         - "bmp": BMP格式
 *         - "webp": WEBP格式
 *         - "bin": 未知格式或文件太短
 */
static const char *detect_file_type(const char *data, int len) {
    DHD_LOGI("Detecting file type...,len = %d", len);
    if (len < 4)
        return "bin"; // 太短无法判断

    /* 自定义的VPG格式 */
    FileHeader *fh = (FileHeader *)data;
    if (fh->magic == 0xAABBCCDD) // vng格式头
    {
        DHD_LOGI("VPG detected: size=%" PRIu32 " itemNum=%" PRIu32 " fps=%" PRIu32,
                 fh->size, fh->itemNum, fh->fps);
        DHD_LOGI("VPG detected: size=%lu,itemNum=%lu,fps=%lu",
                 fh->size, fh->itemNum, fh->fps);
        return "vpg";
    } else {
        DHD_LOGI("VPG detected: size=%lu,itemNum=%lu,fps=%lu",
                 fh->size, fh->itemNum, fh->fps);
    }

    /* JPEG */
    if (data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF)
        return "jpg";

    /* PNG */
    if (data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G')
        return "png";

    /* GIF */
    if (len >= 6 && (memcmp(data, "GIF89a", 6) == 0 || memcmp(data, "GIF87a", 6) == 0))
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

/**
 * @brief 从URL中提取文件类型
 * @details 该函数通过查找URL中最后一个'.'字符来获取文件类型，并返回该类型的字符串副本。
 *          如果URL中不包含'.'字符，则返回空字符串。
 *
 * @param url 输入的URL字符串
 * @return char* 返回文件类型的字符串副本，如果找不到则返回空字符串。
 *               注意：返回的字符串需要调用者负责释放内存。
 *
 * @note 该函数不会修改原始URL字符串。
 *       如果URL为NULL，行为未定义。
 *       返回的空字符串是静态分配的，不应被修改。
 */
static char *get_file_type_in_url(const char *url) {
    char *dot = strrchr(url, '.');
    if (dot) {
        DHD_LOGI("get file type is %s in url %s", (dot + 1), url);
        return strdup(dot + 1);
    }
    return "";
}

/**
 * @brief 从指定URL下载文件到指定目录
 *
 * @param url 要下载文件的URL地址
 * @param dir_name 文件名称
 * @return doit_file_result_t 返回下载结果，包含错误码和文件路径
 *         - HTTP_DL_SUC: 下载成功
 *         - HTTP_DL_FAIL_NO_SPACE: 文件大小超过限制
 *         - 其他错误码: 下载过程中出现的其他错误
 *
 * @note 此函数会先停止当前播放的VPG视频，然后检查文件大小是否超过限制。
 *       如果文件大小在限制范围内，则执行下载操作。
 */
doit_file_result_t dot_http_download(const char *url) {
    DHD_LOGI("Downloading file from : %s", url);
    doit_file_result_t ret = {.err_code = HTTP_DL_SUC, .path = NULL};

    /* 1. 校验文件大小 */
    if (is_http_file_content_length_overflow(url)) {
        /* 文件大小超过限制，直接返回失败 */
        ret.err_code = HTTP_DL_FAIL_NO_SPACE;
    } else {
        // 获取文件名称，只取最后一个/后的名称作为文件名称
        char *filename = (strchr(url, '/')) ? url + strlen(url) - strlen(strrchr(url, '/') + 1) : url;

        DHD_LOGI(">>>准备下载文件，url=%s,filename=%s", url, filename);
        /* 2.2 下载流程 */
        doit_file_result_t download_ret = to_download(url, filename);
        if (download_ret.err_code != HTTP_DL_SUC)
            ret.err_code = download_ret.err_code;
    }
    return ret;
}

/**
 * @brief 初始化HTTP下载模块的回调函数
 *
 * @details 此函数用于设置HTTP下载过程中的成功、进度和失败回调函数。
 *          当下载成功、下载进度更新或下载失败时，将分别调用对应的回调函数。
 *
 * @param prog_cb  下载进度更新时的回调函数
 *
 * @return void
 *
 * @note 此函数需要在开始HTTP下载之前调用，以确保回调函数被正确设置。
 */
void dot_http_dl_init(http_dl_prog_cb_t prog_cb) {
    http_dl_prog_cb = prog_cb;
    DHD_LOGI("XXX");
}