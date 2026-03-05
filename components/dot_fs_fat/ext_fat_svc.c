#include "ext_fat_svc.h"
#include "ext_vpg_svc.h"
#include "esp_vfs.h"
#include "esp_vfs_fat.h"
#include <sys/stat.h>
#include <errno.h>
#include "wear_levelling.h"
#include "esp_partition.h"

/* 文件链表 */
/* 链表节点 */
typedef struct file_node {
    char *path;   /* strdup 出来的完整路径 */
    time_t mtime; // 修改时间
    struct file_node *next;
} file_node_t;

/* 循环链表：head = 最新，tail = 最老 */
static file_node_t *head = NULL;
static file_node_t *tail = NULL;
static file_node_t *cur_ptr = NULL; /* 当前指向的节点 */
static uint8_t cnt = 0;             /* 当前节点数 */

static wl_handle_t sm_wl_handle = WL_INVALID_HANDLE;

static void org_filder(const char *path);
/**
 * @brief 获取全局Wayland句柄
 *
 * 返回指向全局Wayland句柄的指针。该句柄用于管理Wayland连接和相关资源。
 *
 * @return wl_handle_t* 返回指向全局Wayland句柄的指针
 */
// wl_handle_t *get_wl_handle() {
//     return &sm_wl_handle;
// }

/**
 * @brief 设置无线句柄为无效值
 *
 * 该函数将全局无线句柄变量 sm_wl_handle 设置为无效值 WL_INVALID_HANDLE。
 * 通常用于初始化或重置无线句柄状态。
 *
 * @param void 无参数
 * @return void 无返回值
 */
static void set_wl_handle_invalid() {
    sm_wl_handle = WL_INVALID_HANDLE;
}

/**
 * @brief 检查wl句柄是否有效
 *
 * 该函数用于检查全局变量sm_wl_handle是否不等于WL_INVALID_HANDLE，
 * 以判断当前无线句柄是否有效。
 *
 * @return bool 返回true表示句柄已被使能，返回false表示未被使能
 */
static bool is_wl_handle_valid() {
    return (sm_wl_handle != WL_INVALID_HANDLE);
}

/* ---------- 链表工具 ---------- */
/* 打印当前循环链表：  newest(0) -> ... -> oldest(cnt-1)  */
static void fat_list_print(void) {
    if (cnt == 0 || !head) {
        DFF_LOGI(">>> file list empty");
        return;
    }

    DFF_LOGI(">>> file list(total %lu) newest->oldest:", (unsigned long)cnt);

    file_node_t *p = head;
    for (uint32_t i = 0; i < cnt; i++) {
        // 打印索引、mtime、路径。mtime
        DFF_LOGI("  [%lu] mtime=%ld  %s",
                 (unsigned long)i,
                 (long)p->mtime,
                 p->path ? p->path : "(null)");
        p = p->next;
    }

    // 额外标注当前指针在哪
    if (cur_ptr) {
        // 找一下当前索引（O(n)）
        uint32_t cur_idx = 0;
        p = head;
        for (uint32_t i = 0; i < cnt; i++) {
            if (p == cur_ptr) {
                cur_idx = i;
                break;
            }
            p = p->next;
        }
        DFF_LOGI(">>> current index=%lu, current=%s",
                 (unsigned long)cur_idx,
                 cur_ptr->path ? cur_ptr->path : "(null)");
    }
}

/* 创建节点 */
static file_node_t *node_new(const char *path, time_t mt) {
    file_node_t *n = (file_node_t *)malloc(sizeof(file_node_t));
    if (!n) return NULL;
    n->path = strdup(path);
    if (!n->path) {
        free(n);
        return NULL;
    }
    n->mtime = mt;
    n->next = NULL;
    return n;
}

static void node_free(file_node_t *n) {
    if (!n) return;
    free(n->path);
    free(n);
}

/* 清空整个链表 */
static void list_clear(void) {
    if (cnt == 0) return;
    file_node_t *p = head;
    for (uint32_t i = 0; i < cnt; i++) {
        file_node_t *n = p;
        p = p->next;
        node_free(n);
    }
    head = tail = cur_ptr = NULL;
    cnt = 0;
}

/* 按 mtime 从大到小插入（新->旧），维护循环链表 head/tail */
static void list_insert_sorted(const char *path, time_t mt) {
    file_node_t *n = node_new(path, mt);
    if (!n) return;

    if (cnt == 0) {
        head = tail = n;
        n->next = n;
        cur_ptr = head; // 默认指向最新
        cnt = 1;
        return;
    }

    // 插到最前（比 head 更新）
    if (mt >= head->mtime) {
        n->next = head;
        head = n;
        tail->next = head;
        cnt++;
        return;
    }

    // 插到最后（比 tail 更旧）
    if (mt <= tail->mtime) {
        tail->next = n;
        n->next = head;
        tail = n;
        cnt++;
        return;
    }

    // 插入中间：找到第一个比它“旧”的节点，插前面
    file_node_t *prev = head;
    file_node_t *cur = head->next;
    while (cur != head && cur->mtime >= mt) {
        prev = cur;
        cur = cur->next;
    }
    prev->next = n;
    n->next = cur;
    cnt++;
}

/* 删除当前节点 cur_ptr，成功返回 true */
static bool delete_current_node(void) {
    if (cnt == 0 || !head || !cur_ptr) {
        return false;
    }

    /* 只有一个节点 */
    if (cnt == 1) {
        node_free(cur_ptr);
        head = tail = cur_ptr = NULL;
        cnt = 0;
        return true;
    }

    /* 找 cur_ptr 的前驱（循环链表必能找到） */
    file_node_t *prev = head;
    while (prev->next != cur_ptr && prev->next != head) {
        prev = prev->next;
    }
    if (prev->next != cur_ptr) {
        // cur_ptr 不在链表里（异常）
        return false;
    }

    file_node_t *to_del = cur_ptr;
    file_node_t *next = cur_ptr->next;

    /* 断链 */
    prev->next = next;

    /* 更新 head/tail */
    if (to_del == head) head = next;
    if (to_del == tail) tail = prev;
    tail->next = head; // 维持循环

    /* cur_ptr 指向下一个 */
    cur_ptr = next;

    node_free(to_del);
    cnt--;
    return true;
}

/* ---------- 原对外接口(没用但保留) ---------- */
const char *dot_fs_get_file_name_by_index(uint8_t index) {
    if (cnt == 0 || index >= cnt) return "";
    file_node_t *p = head;
    for (uint32_t i = 0; i < index; i++) p = p->next;
    return p->path;
}

const char *dot_fs_next_file(void) {
    if (!cur_ptr) return "";
    cur_ptr = cur_ptr->next; // 循环链表，自动回到 head
    return cur_ptr->path;
}

uint32_t dot_fs_get_file_count(void) {
    return cnt;
}

const char *dot_fs_get_current_file_name(void) {
    return cur_ptr ? cur_ptr->path : "";
}

bool dot_fs_delete_current_node_and_file(void) {
    if (cnt == 0 || !head || !cur_ptr) return false;

    char path_copy[128];
    const char *p = cur_ptr->path ? cur_ptr->path : "";
    snprintf(path_copy, sizeof(path_copy), "%s", p);

    bool ok = delete_current_node(); // 先删节点
    if (!ok) return false;

    if (path_copy[0] != '\0') {
        int r = unlink(path_copy); // 再删文件
        if (r != 0) {
            // 文件删失败不影响链表已删除，按你需求决定是否当失败
            // 这里返回 true，但你也可以改成 false
        }
    }
    return true;
}

static esp_err_t fatfs_print_size(const char *drv) // drv 一般是 "0:" 或 ""（默认盘）
{
    FATFS *fs = NULL;
    DWORD free_clusters = 0;

    FRESULT fr = f_getfree(drv, &free_clusters, &fs);
    if (fr != FR_OK || fs == NULL) {
        DFF_LOGE("f_getfree failed: %d", fr);
        return ESP_FAIL;
    }

    // 总簇数 = n_fatent - 2 (FatFs 约定：保留 0/1)
    uint32_t total_clusters = (fs->n_fatent - 2);
    uint32_t free_clu = free_clusters;
    DFF_LOGI("total_clusters=%lu, free_clusters=%lu",
             total_clusters,
             free_clu);

    // 簇大小 = csize * 512 (FF_MAX_SS 也可能是 4096，但 FatFs 用 ss=512 的换算是常见写法)
    // 为了更稳，使用 fs->ssize（新版本 FatFs 有），没有的话用 512。
#if FF_FS_EXFAT || FF_MAX_SS != FF_MIN_SS
    uint32_t sector_size = (fs->ssize ? fs->ssize : 512);
#else
    uint32_t sector_size = 512;
#endif

    uint32_t bytes_per_cluster = fs->csize * sector_size;

    uint32_t total_bytes = total_clusters * bytes_per_cluster;
    uint32_t free_bytes = free_clu * bytes_per_cluster;
    uint32_t used_bytes = total_bytes - free_bytes;

    DFF_LOGI("Cluster size: csize=%lu, sector_size=%lu, bytes_per_cluster=%lu",
             fs->csize,
             sector_size,
             bytes_per_cluster);

    DFF_LOGI("FATFS total=%lu bytes, used=%lu bytes, free=%lu bytes",
             total_bytes,
             used_bytes,
             free_bytes);

    return ESP_OK;
}

/**
 * @brief 获取指定目录中，所有文件的总大小（以字节为单位）
 */
uint32_t dir_size_bytes(const char *path) {
    DIR *dir = opendir(path);
    if (!dir) {
        // ESP_LOGE(TAG, "opendir failed: %s (%s)", path, strerror(errno));
        return 0;
    }

    uint32_t total_size = 0;
    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL) {
        // 跳过 . 和 ..
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;

        char full_path[64];
        int n = snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);
        if (n < 0 || n >= (int)sizeof(full_path)) {
            DFF_LOGI("path too long, skip: %s", full_path);
            continue;
        }

        struct stat st;
        if (stat(full_path, &st) != 0) {
            // ESP_LOGW(TAG, "stat failed: %s (%s)", full_path, strerror(errno));
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            // 递归子目录
            uint32_t sub = dir_size_bytes(full_path);
            DFF_LOGI("发现子目录,递归查找");
            // total += sub;
        } else if (S_ISREG(st.st_mode)) {
            if (strstr(full_path, "System Volume Information") || strstr(full_path, "WPSettings.dat") || strstr(full_path, "IndexerVolumeGuid")) {
                DFF_LOGI("skip system file: %s", full_path);
                continue; // 跳过 Windows 系统文件
            }
            DFF_LOGI("发现文件%s,文件大小=%lu字节", full_path, st.st_size);
            total_size += st.st_size;
            DFF_LOGI("累计大小%lu字节", total_size);
        } else {
            // 其它类型（一般不会有）
            DFF_LOGI("other: %s", full_path);
        }
    }

    closedir(dir);
    return total_size;
}
/* 重置文件链表，把文件插入链表（新->旧） */
static void org_filder(const char *path) {
    DFF_LOGE("PATH=%s", path);

    // 只在扫描根目录时清空
    if (strcmp(path, CONFIG_FILE_BASE_PATH) == 0) {
        list_clear();
    }

    DIR *dir = opendir(path);
    if (!dir) {
        DFF_LOGE("opendir failed: %s", path);
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;

        // 跳过系统目录
        if (!strcmp(entry->d_name, "System Volume Information") || !strcmp(entry->d_name, "$RECYCLE.BIN")) {
            continue;
        }

        char full_path[96];
        int n = snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);
        if (n < 0 || n >= (int)sizeof(full_path)) continue;

        struct stat st;
        if (stat(full_path, &st) != 0) continue;

        if (S_ISREG(st.st_mode)) {
            // 跳过 windows 系统文件
            if (strstr(full_path, "WPSettings.dat") || strstr(full_path, "IndexerVolumeGuid")) {
                continue;
            }
            list_insert_sorted(full_path, st.st_mtime);
        }

        // 如果要递归子目录，打开这段，并加 yield 防 WDT
        // else if (S_ISDIR(st.st_mode)) {
        //     org_filder(full_path);
        // }

        // 防止主线程扫描太久触发 WDT
        // static int k = 0;
        // if ((++k % 20) == 0) vTaskDelay(1);
    }

    closedir(dir);

    // 扫完根目录后，默认当前指向最新
    if (strcmp(path, CONFIG_FILE_BASE_PATH) == 0) {
        cur_ptr = head;
        DFF_LOGI("scan done, total=%lu, newest=%s",
                 (unsigned long)cnt,
                 head ? head->path : "(null)");
    }
}
void dot_fat_org_filder() {
    org_filder(CONFIG_FILE_BASE_PATH);
    fat_list_print();
}

/**
 * @brief 检查文件大小是否会导致存储空间溢出
 *
 * @param file_size 待检查的文件大小（单位：字节）
 * @return bool
 *         - true: 文件大小超过剩余空间
 *         - false: 文件大小在允许范围内
 *
 * @note 该函数会检查当前已用空间和总空间，判断文件大小是否会导致溢出
 */
bool dot_fat_is_file_size_overflow(uint32_t file_size) {
    bool ret = false;
    size_t total = 0;
    size_t used = dir_size_bytes(CONFIG_FILE_BASE_PATH);
    // 获取分区表中指定位置的总大小
    const esp_partition_t *storage = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA,
        ESP_PARTITION_SUBTYPE_DATA_FAT,
        "storage"); /* 分区表里的 Name 字段 */
    if (storage) {
        total = storage->size; /* 单位：字节 */
    }
    DFF_LOGI("检测文件是否过大，已用空间=%d bytes,总空间=%d bytes,文件大小=%d bytes", used, total, file_size);
    // ret = esp_littlefs_info("storage", &total, &used);
    if (file_size > (total - used)) {
        DFF_LOGW("File size exceeds limit of %d bytes,Skip download", (total - used));
        ret = true;
    } else {
        DFF_LOGI(">>>File size verification is successful. Download is permitted.");
        ret = false;
    }
    return ret;
}

const char *dot_fat_get_cur_file_type() {
    /* 1.获取文件目录名称 */
    const char *file_name = dot_fs_get_current_file_name();
    if (!file_name || file_name[0] == '\0') return "unknown";

    DFF_LOGI("Checking file: %s", file_name);  // 添加日志
    FILE *f = fopen(file_name, "rb");
    if (!f) {
        DFF_LOGE("fopen fail: %s, errno=%d", file_name, errno);  // 增强日志
        return "unknown";
    }

    /* 2.获取文件类型-读取前20个字节 */
    uint8_t head[20] = {0};
    size_t len = fread(head, 1, sizeof(head), f);
    fclose(f);

    /* 自定义的VPG格式 */
    if (len >= sizeof(FileHeader)) {
        FileHeader *fh = (FileHeader *)head;
        if (fh->magic == 0xAABBCCDD) {
            DFF_LOGI("detech file type: vpg");
            return "vpg";
        }
    }

    if (len >= 3 && head[0] == 0xFF && head[1] == 0xD8 && head[2] == 0xFF) /* JPEG */
    {
        DFF_LOGI("detech file type: jpg");
        return "jpg";
    }

    /* 默认 - 打印前几个字节用于调试 */
    DFF_LOGI("detech file type: unknown, len=%d, head[0]=%02X %02X %02X %02X %02X %02X",
             len, head[0], head[1], head[2], head[3], head[4], head[5]);
    return "unknown";

    if (len >= 4 && memcmp(head, "\x89PNG", 4) == 0) /* PNG */
    {
        DFF_LOGI("detech file type: png");
        return "png";
    }

    if (len >= 6 && (memcmp(head, "GIF89a", 6) == 0 || memcmp(head, "GIF87a", 6) == 0)) /* GIF */
    {
        DFF_LOGI("detech file type: gif");
        return "gif";
    }

    if (len >= 12 && memcmp(head + 4, "ftyp", 4) == 0) /* MP4 / ISO BMFF：ftyp 在 offset 4 */
    {
        DFF_LOGI("detech file type:mp4");
        return "mp4";
    }

    if (len >= 2 && head[0] == 'B' && head[1] == 'M') /* BMP */
    {
        DFF_LOGI("detech file type:bmp");
        return "bmp";
    }

    if (len >= 12 && memcmp(head + 8, "WEBP", 4) == 0) /* WEBP */
    {
        DFF_LOGI("detech file type:webp");
        return "webp";
    }

    /* 默认 */
    return "unknown";
}

void dot_fat_init(void) {
    DFF_LOGI("Mounting FAT filesystem");
    // To mount device we need name of device partition, define base_path
    // and allow format partition in case if it is new one and was not formatted before
    const esp_vfs_fat_mount_config_t mount_config = {
        .max_files = 4,                 // Number of files that can be open at a time
        .format_if_mount_failed = true, // If true, try to format the partition if mount fails
        .allocation_unit_size = 4096,   // Size of allocation unit, cluster size.
        .use_one_fat = false,           // Use only one FAT table (reduce memory usage), but decrease reliability of file system in case of power failure.
    };

    // Mount FATFS filesystem located on "storage" partition in read-write mode
    esp_err_t err = esp_vfs_fat_spiflash_mount_rw_wl(CONFIG_FILE_BASE_PATH, "storage", &mount_config, &sm_wl_handle);
    if (err != ESP_OK) {
        DFF_LOGI("Failed to mount FATFS (%s)", esp_err_to_name(err));
        return;
    }
    DFF_LOGI("Filesystem mounted");

    /* 整理文件资源 */
    org_filder(CONFIG_FILE_BASE_PATH);
}

void dot_fat_deinit() {
    // 卸载 FATFS 文件系统
    ESP_ERROR_CHECK(esp_vfs_fat_spiflash_unmount_rw_wl(CONFIG_FILE_BASE_PATH, sm_wl_handle));
    wl_unmount(sm_wl_handle);
    set_wl_handle_invalid();
    DFF_LOGI("FATFS filesystem unmounted");
}