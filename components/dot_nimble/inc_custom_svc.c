/**
 * @file inc_custom_svc.c
 * @author your name (you@domain.com)
 * @brief 该文件用于自定义服务的实现
 * @version 0.1
 * @date 2026-01-05
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "inc_custom_svc.h"
#include "dn_common.h"
#include "ext_http_download.h"

#define JSON_MAX_LEN 256

/* Private variables */
/* Service */
static const ble_uuid128_t svc_uuid =
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
                     0x93, 0xf3, 0xa3, 0xb5, 0x01, 0x00, 0x40, 0x6e);
static const ble_uuid128_t char_rx_uuid = // 手机→设备
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
                     0x93, 0xf3, 0xa3, 0xb5, 0x02, 0x00, 0x40, 0x6e);
static const ble_uuid128_t char_tx_uuid = // 设备→手机（notify）
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
                     0x93, 0xf3, 0xa3, 0xb5, 0x03, 0x00, 0x40, 0x6e);

/* Private function declarations */
static uint16_t get_unique_id_from_mac(void);
static int char_rx_access(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg);
static int char_tx_access(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg);
static void ble_json_task(void *param);
static int ble_json_notify(const char *txt);
static int ble_bin_notify(const uint8_t *data, size_t len);
static int ble_json_notify_to_conn(uint16_t *connect_handle, const char *txt);
static int ble_bin_notify_to_conn(uint16_t *connect_handle, const uint8_t *data, size_t len);
static void ble_json_rx(const char *line);

static uint16_t g_conn_handle = BLE_HS_CONN_HANDLE_NONE; // 当前连接句柄
static uint16_t rx_val_handle;                           // 手机→设备
static uint16_t tx_val_handle;                           // 设备→手机（notify）
static uint16_t tx_chr_conn_handle = 0;
static bool tx_chr_conn_handle_inited = false;
static bool tx_noti_status = false;

static QueueHandle_t ble_json_queue = NULL; // BLE JSON数据队列

static cust_nimble_get_info_cb s_cust_nimble_cb = NULL; // 接收到蓝牙数据d回调函数

/* GATT services table */
static const struct ble_gatt_svc_def inc_cust_gatt_svr_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &svc_uuid.u,
        .characteristics =
            (struct ble_gatt_chr_def[]){
                {.uuid = &char_rx_uuid.u,
                 .access_cb = char_rx_access,
                 .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
                 .val_handle = &rx_val_handle},
                {.uuid = &char_tx_uuid.u,
                 .access_cb = char_tx_access,
                 .flags = BLE_GATT_CHR_F_NOTIFY, // 支持通知和读取
                 .val_handle = &tx_val_handle},

                {0}},
    },
    {
        0, /* No more services. */
    },
};

/**
 * @brief Get the unique id from mac object
 *
 * @return uint16_t
 */
static uint16_t get_unique_id_from_mac(void) {
    uint8_t addr[6] = {0};
    // 取 public address（或你 own addr），用后两字节做 unique_id
    // 注意：在 ble_hs 已同步后调用更稳
    ble_hs_id_copy_addr(BLE_ADDR_PUBLIC, addr, NULL);
    return (uint16_t)((addr[4] << 8) | addr[5]);
}

/**
 * @brief Rx服务
 */
static int char_rx_access(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg) {
    /* Local variables */
    int rc;

    /* Handle access events */
    switch (ctxt->op) {
        /* Write characteristic event */
    case BLE_GATT_ACCESS_OP_WRITE_CHR:
        /* Verify connection handle */
        if (conn_handle != BLE_HS_CONN_HANDLE_NONE) {
            DN_LOGI("characteristic write; conn_handle=%d attr_handle=%d",
                    conn_handle, attr_handle);
        } else {
            DN_LOGI(
                "characteristic write by nimble stack; attr_handle=%d",
                attr_handle);
        }

        /* Verify attribute handle */
        if (attr_handle == rx_val_handle) {
            /* Verify access buffer length */
            if (ctxt->om->om_len > 0) {
                int rc = 0;
                uint16_t seg_len = OS_MBUF_PKTLEN(ctxt->om); /* 总长度 */
                char line[JSON_MAX_LEN];                     /* 临时缓冲区 */

                /* 超长直接截断，防止越界 */
                if (seg_len >= sizeof(line))
                    seg_len = sizeof(line) - 1;

                /* 拷贝并补 '\0' */
                os_mbuf_copydata(ctxt->om, 0, seg_len, line);
                line[seg_len] = '\0';

                DN_LOGI("received data: %s", line);

                xQueueSend(ble_json_queue, line, 0);
                return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
            } else {
                goto error;
            }
            return rc;
        }
        goto error;

    /* Unknown event */
    default:
        goto error;
    }
error:
    DN_LOGE(
        "unexpected access operation to led characteristic, opcode: %d",
        ctxt->op);
    return BLE_ATT_ERR_UNLIKELY;
}

/**
 * @brief Tx服务
 */
static int char_tx_access(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg) {
    return BLE_ATT_ERR_UNLIKELY;
}

/**
 * @brief BLE JSON数据处理任务
 * @details 该任务持续从队列中接收JSON格式的数据，并调用处理函数进行解析
 *
 * @param param 任务参数（未使用）
 * @return 无返回值
 *
 * @note 该任务会无限循环运行
 * @note 使用portMAX_DELAY表示无限期等待队列数据
 */
static void ble_json_task(void *param) {
    char line[JSON_MAX_LEN];
    while (true) {
        if (xQueueReceive(ble_json_queue, line, portMAX_DELAY) == pdTRUE) {
            ble_json_rx(line);
        }
    }
}

/**
 * @brief 通过蓝牙发送JSON格式通知
 *
 * @param txt 要发送的JSON字符串
 * @return int 返回状态码：
 *             - BLE_HS_EAPP: 当通知状态未启用、连接句柄未初始化或输入为空时
 *             - 0: 成功发送通知
 *             - 其他错误码: 蓝牙协议栈返回的错误
 *
 * @note 函数会自动在输入字符串后添加换行符
 * @note 调用者需确保输入的JSON字符串格式正确
 */
static int ble_json_notify(const char *txt) {
    if (!tx_noti_status || !tx_chr_conn_handle_inited || !txt) {
        return BLE_HS_EAPP;
    }
    size_t txt_len = strlen(txt);
    char *txt_with_newline = malloc(txt_len + 2); // 分配内存，包括换行符和终止符
    if (txt_with_newline != NULL) {
        memcpy(txt_with_newline, txt, txt_len);
        txt_with_newline[txt_len] = '\n';     // 添加换行符
        txt_with_newline[txt_len + 1] = '\0'; // 添加终止符
    }

    struct os_mbuf *om = ble_hs_mbuf_from_flat(txt_with_newline, strlen(txt_with_newline));
    if (!om) {
        DN_LOGE("no mbuf");
        return BLE_HS_EAPP;
    }
    int rc = ble_gatts_notify_custom(tx_chr_conn_handle, tx_val_handle, om);
    if (rc != 0) {
        DN_LOGE("notify fail %d", rc);
        os_mbuf_free_chain(om); // 只有失败时才释放
    }
    free(txt_with_newline);
    DN_LOGI("【ble_json_notify】tx indication sent %s", txt);
    return rc;
}

static int ble_bin_notify(const uint8_t *data, size_t len) {
    if (!tx_noti_status || !tx_chr_conn_handle_inited || !data || len == 0)
        return BLE_HS_EAPP;

    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, len);
    if (!om)
        return BLE_HS_ENOMEM;

    int rc = ble_gatts_notify_custom(tx_chr_conn_handle, tx_val_handle, om);
    if (rc != 0)
        os_mbuf_free_chain(om);
    else
        DN_LOGI("bin notify %d bytes", (int)len);
    return rc;
}

/**
 * @breif 发送订阅数据给手机(notify)，指定句柄
 */
static int ble_json_notify_to_conn(uint16_t *connect_handle, const char *txt) {
    if (!tx_noti_status || !tx_chr_conn_handle_inited || !txt) {
        return BLE_HS_EAPP;
    }

    size_t txt_len = strlen(txt);
    char *txt_with_newline = malloc(txt_len + 2); // 分配内存，包括换行符和终止符

    if (txt_with_newline != NULL) {
        memcpy(txt_with_newline, txt, txt_len);
        txt_with_newline[txt_len] = '\n';     // 添加换行符
        txt_with_newline[txt_len + 1] = '\0'; // 添加终止符
    }

    struct os_mbuf *om = ble_hs_mbuf_from_flat(txt_with_newline, strlen(txt_with_newline));
    if (!om) {
        DN_LOGE("no mbuf");
        return BLE_HS_EAPP;
    }
    int rc = ble_gatts_notify_custom(*connect_handle, tx_val_handle, om);
    if (rc != 0) {
        DN_LOGE("notify fail %d", rc);
        os_mbuf_free_chain(om); // 只有失败时才释放
    }
    free(txt_with_newline);
    DN_LOGI("【ble_json_notify_to_conn】tx indication sent %s", om->om_data);
    return rc;
}

static int ble_bin_notify_to_conn(uint16_t *connect_handle, const uint8_t *data, size_t len) {
    if (!tx_noti_status || !tx_chr_conn_handle_inited || !data || len == 0)
        return BLE_HS_EAPP;

    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, len);
    if (!om)
        return BLE_HS_ENOMEM;

    int rc = ble_gatts_notify_custom(*connect_handle, tx_val_handle, om);
    if (rc != 0)
        os_mbuf_free_chain(om);
    else
        DN_LOGI("bin notify to conn %d bytes", (int)len);

    return rc;
}

/* 手机->处理ble收到的json数据,返回值是要返回给手机的json数据 */
static void ble_json_rx(const char *line) {
    if (line) {
        DN_LOGI("ble_json_rx: %s", line);
    }

    // json处理回调,处理接收到的蓝牙数据
    if (s_cust_nimble_cb)
        s_cust_nimble_cb(line);
}

/* 构造 厂商数据
  [CompanyID(2 bytes little-endian)] + ['A''Y'(2 bytes魔数)] + [unique_id(2 bytes)] + [ver(1 byte)]

   out[0]:固定 0xE5;
   out[1]:固定 0x02;
   out[2]: unique_id L，唯一ID的低字节
   out[3]: unique_id H，唯一ID的高字节
   out[4]: 放入分辨率数据 0x00：160分辨率；0x1：240分辨率；0x02：360分辨率
   out[5]: 保留位
   out[6]: 固定 0x01
*/
void cust_manufacturer_data(uint8_t *mfg) {
    uint16_t uid = get_unique_id_from_mac();

    mfg[0] = 0xE5;
    mfg[1] = 0x02;                  // Company ID = 0xFFFF (临时)
    mfg[2] = (uint8_t)(uid & 0xFF); // unique_id L
    mfg[3] = (uint8_t)(uid >> 8);   // unique_id H
    // 获取屏幕UI尺寸,分辨率数据：160分辨率：mfg[4]=0x01; 240分辨率：mfg[4]=0x02; 360分辨率：mfg[4]=0x03;

#if CONFIG_LCD_ST77916_360X360
    mfg[4] = 0x02;
#elif CONFIG_LCD_ST7796_240X240 || CONFIG_LCD_GC9A01_240X240
    mfg[4] = 0x01;
#elif CONFIG_LCD_GC9A01_160X160
    mfg[4] = 0x00;
#else
    mfg[4] = 0x01; //默认240分辨率
#endif
    mfg[5] = 0x00; // 保留位
    mfg[6] = 0x01; // version

    DN_LOGI("mfg_data: %02X %02X %02X %02X %02X %02X %02X",
            mfg[0], mfg[1], mfg[2], mfg[3], mfg[4], mfg[5], mfg[6]);
}

/**
 * @brief 处理自定义服务的GATT订阅事件
 *
 * @param event BLE GAP事件结构体指针，包含订阅相关信息
 *
 * 该函数处理客户端对自定义服务特征的订阅/取消订阅操作：
 * - 检查是否为TX特征值的订阅事件
 * - 更新连接句柄和通知状态
 * - 当收到订阅通知时，获取屏幕分辨率并发送给订阅方
 * - 根据不同分辨率发送对应的比例值
 *
 * @note 函数会检查订阅状态，如果取消订阅则直接返回
 * @note 支持的分辨率包括160x160、240x240和368x368
 */
void inc_custom_svc_gatt_svr_subscribe_handle(struct ble_gap_event *event) {
    if (event->subscribe.attr_handle == tx_val_handle) {
        tx_chr_conn_handle = event->subscribe.conn_handle;
        tx_chr_conn_handle_inited = true;
        tx_noti_status = event->subscribe.cur_notify;

        if (!tx_noti_status) {
            DN_LOGI("Phone UNsubscribed notify (cur_notify=0), skip sending.");
            return;
        }

        DN_LOGI("Phone subscribed to notify, send resolution ratio...");
        // // 传入连接句柄，确保数据发送到当前订阅的手机
        // int ret = 0;
        // uint16_t width, height = 0;
        // doit_get_ui_screen_size(&width, &height);
        // if (width == 160 && height == 160)
        //     ret = ble_json_notify_to_conn(&event->subscribe.conn_handle, BLE_RATIO_160);
        // else if (width == 240 && height == 240)
        //     ret = ble_json_notify_to_conn(&event->subscribe.conn_handle, BLE_RATIO_240);
        // else if (width == 368 && height == 368)
        //     ret = ble_json_notify_to_conn(&event->subscribe.conn_handle, BLE_RATIO_360);

        // if (ret == 0)
        //     DN_LOGI(">>>【通知】:分辨率 %dx%d", width, height);
        // else
        //     DN_LOGI(">>>【通知】:分辨率发送失败");
    }
}

struct ble_gatt_svc_def *get_cust_gatt_svc_def() {
    return inc_cust_gatt_svr_svcs;
}

ble_uuid128_t *get_svc_uuid128() {
    return &svc_uuid;
}

void inc_custom_svc_init(void) {
    // 创建ble_json处理队列
    ble_json_queue = xQueueCreate(4, JSON_MAX_LEN);
    xTaskCreate(ble_json_task, "ble_json_task", 8192, NULL, 5, NULL);
}

void inc_set_nimble_rec_info_cb(cust_nimble_get_info_cb cb) {
    s_cust_nimble_cb = cb;
}

/**
 * @brief 通过蓝牙将字符串发送到手机
 *
 * @details 该函数通过蓝牙低功耗(BLE)的JSON通知服务将字符串发送到手机设备。
 *          函数内部调用ble_json_notify()函数实现具体的发送功能。
 *
 * @param str 要发送的字符串指针，字符串必须以'\0'结尾
 *
 * @return bool 返回发送结果：
 *         - true: 发送成功
 *         - false: 发送失败
 *
 * @note 该函数是同步操作，会等待发送完成才返回
 */
bool inc_nimble_notify(const char *str) {
    int send_ret = ble_json_notify(str);
    if (send_ret == 0)
        return true;
    else
        return false;
}