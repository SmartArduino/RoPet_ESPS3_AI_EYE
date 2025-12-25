/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
/* Includes */
#include "gatt_svc.h"
#include "ble_common.h"
#include "file_download.h"
#include "vpg_decode.h"
#include "doit_nvs.h"
#include "doit_file.h"
#include "doit_ble.h"
#include "doit_ui.h"

#define JSON_MAX_LEN 256

#define BLE_RESP_FAIL "0100"
#define BLE_RESP_OK "0101"
#define BLE_RATIO_360 "0002"
#define BLE_RATIO_240 "0001"
#define BLE_RATIO_160 "0000"
#define BLE_REC_PLATFORM_EYE "0101"   //用户进入双目
#define BLE_REC_PLATFORM_BADGE "0102" //用户进入吧唧

/* Private variables */
/* Private function declarations */
static int char_rx_access(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg);
static int char_tx_access(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg);

                     
static uint16_t g_conn_handle = BLE_HS_CONN_HANDLE_NONE; // 当前连接句柄
static uint16_t rx_val_handle;                           // 手机→设备
static uint16_t tx_val_handle;                           // 设备→手机（notify）
static uint16_t tx_chr_conn_handle = 0;
static bool tx_chr_conn_handle_inited = false;
static bool tx_noti_status = false;

static QueueHandle_t ble_json_queue = NULL; // BLE JSON数据队列


static uint8_t platform_idx = 1; // 用户进入平台索引

// /* 自定义蓝牙协议 */
// #if CONFIG_LCD_ST77916_360X360
// static const uint8_t ratio_360[] = {0x00, 0x02};
// #elif CONFIG_LCD_GC9A01_240X240 || CONFIG_LCD_ST7796_240X240
// static const uint8_t ratio_240[] = {0x00, 0x01};
// #elif CONFIG_LCD_GC9A01_160X160
// static const uint8_t ratio_160[] = {0x00, 0x00};
// #endif


// static const uint8_t rsp_fail[] = {0x01, 0x00}; // 失败
// static const uint8_t rsp_ok[] = {0x01, 0x01};   // 成功

/* GATT services table */
static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
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

static int char_rx_access(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    /* Local variables */
    int rc;

    /* Handle access events */
    switch (ctxt->op)
    {

        /* Write characteristic event */
    case BLE_GATT_ACCESS_OP_WRITE_CHR:
        /* Verify connection handle */
        if (conn_handle != BLE_HS_CONN_HANDLE_NONE)
        {
            MP_LOGI( "characteristic write; conn_handle=%d attr_handle=%d",
                     conn_handle, attr_handle);
        }
        else
        {
            MP_LOGI(
                     "characteristic write by nimble stack; attr_handle=%d",
                     attr_handle);
        }

        /* Verify attribute handle */
        if (attr_handle == rx_val_handle)
        {
            /* Verify access buffer length */
            if (ctxt->om->om_len > 0)
            {
                int rc = 0;
                uint16_t seg_len = OS_MBUF_PKTLEN(ctxt->om); /* 总长度 */
                char line[JSON_MAX_LEN];                     /* 临时缓冲区 */

                /* 超长直接截断，防止越界 */
                if (seg_len >= sizeof(line))
                    seg_len = sizeof(line) - 1;

                /* 拷贝并补 '\0' */
                os_mbuf_copydata(ctxt->om, 0, seg_len, line);
                line[seg_len] = '\0';

                MP_LOGI( "received data: %s", line);

                xQueueSend(ble_json_queue, line, 0);
                return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
            }
            else
            {
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
    MP_LOGE(
             "unexpected access operation to led characteristic, opcode: %d",
             ctxt->op);
    return BLE_ATT_ERR_UNLIKELY;
}

static int char_tx_access(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    return BLE_ATT_ERR_UNLIKELY;
}

static void ble_json_task(void *param)
{
    char line[JSON_MAX_LEN];
    while (true)
    {
        if (xQueueReceive(ble_json_queue, line, portMAX_DELAY) == pdTRUE)
        {
            ble_json_rx(line);
        }
    }
}

/*
 *  Handle GATT attribute register events
 *      - Service register event
 *      - Characteristic register event
 *      - Descriptor register event
 */
void gatt_svr_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg)
{
    /* Local variables */
    char buf[BLE_UUID_STR_LEN];

    /* Handle GATT attributes register events */
    switch (ctxt->op)
    {

    /* Service register event */
    case BLE_GATT_REGISTER_OP_SVC:
        MP_LOGD( "registered service %s with handle=%d",
                 ble_uuid_to_str(ctxt->svc.svc_def->uuid, buf),
                 ctxt->svc.handle);
        break;

    /* Characteristic register event */
    case BLE_GATT_REGISTER_OP_CHR:
        MP_LOGD(
                 "registering characteristic %s with "
                 "def_handle=%d val_handle=%d",
                 ble_uuid_to_str(ctxt->chr.chr_def->uuid, buf),
                 ctxt->chr.def_handle, ctxt->chr.val_handle);
        break;

    /* Descriptor register event */
    case BLE_GATT_REGISTER_OP_DSC:
        MP_LOGD( "registering descriptor %s with handle=%d",
                 ble_uuid_to_str(ctxt->dsc.dsc_def->uuid, buf),
                 ctxt->dsc.handle);
        break;

    /* Unknown event */
    default:
        assert(0);
        break;
    }
}

/*
 *  GATT server subscribe event callback
 *      1. Update heart rate subscription status
 */

void gatt_svr_subscribe_cb(struct ble_gap_event *event)
{
    /* Check connection handle */
    if (event->subscribe.conn_handle != BLE_HS_CONN_HANDLE_NONE)
    {
        MP_LOGI( "subscribe event; conn_handle=%d attr_handle=%d",
                 event->subscribe.conn_handle, event->subscribe.attr_handle);
    }
    else
    {
        MP_LOGI( "subscribe by nimble stack; attr_handle=%d",
                 event->subscribe.attr_handle);
    }

    if (event->subscribe.attr_handle == tx_val_handle)
    {
        tx_chr_conn_handle = event->subscribe.conn_handle;
        tx_chr_conn_handle_inited = true;
        tx_noti_status = event->subscribe.cur_notify;

        if (!tx_noti_status) {
            MP_LOGI("Phone UNsubscribed notify (cur_notify=0), skip sending.");
        return;
        }

        MP_LOGI( "Phone subscribed to notify, send resolution ratio...");
        // 传入连接句柄，确保数据发送到当前订阅的手机
        int ret = 0;
        uint16_t width,height = 0;
        doit_get_ui_screen_size(&width, &height);
        if(width == 160 && height == 160)
            ret = ble_json_notify_to_conn(&event->subscribe.conn_handle, BLE_RATIO_160);
        else if(width == 240 && height == 240)
            ret = ble_json_notify_to_conn(&event->subscribe.conn_handle, BLE_RATIO_240);         
        else if(width == 368 && height == 368)
            ret = ble_json_notify_to_conn(&event->subscribe.conn_handle, BLE_RATIO_360);
        
        if (ret == 0)
            MP_LOGI( ">>>【通知】:分辨率 %dx%d",width,height);   
        else
            MP_LOGI( ">>>【通知】:分辨率发送失败");   
    }
}

/*
 *  GATT server initialization
 *      1. Initialize GATT service
 *      2. Update NimBLE host GATT services counter
 *      3. Add GATT services to server
 */
int gatt_svc_init(void)
{
    /* Local variables */
    int rc;

    /* 1. GATT service initialization */
    ble_svc_gatt_init();

    /* 2. Update GATT services counter */
    rc = ble_gatts_count_cfg(gatt_svr_svcs);
    if (rc != 0)
    {
        return rc;
    }

    /* 3. Add GATT services */
    rc = ble_gatts_add_svcs(gatt_svr_svcs);
    if (rc != 0)
    {
        return rc;
    }

    // 创建ble_json处理队列
    ble_json_queue = xQueueCreate(4, JSON_MAX_LEN);
    xTaskCreate(ble_json_task, "ble_json_task", 8192, NULL, 5, NULL);

    return 0;
}

/**
 * @breif 发送订阅数据给手机(notify)
 */
int ble_json_notify(const char *txt)
{
    if (!tx_noti_status || !tx_chr_conn_handle_inited || !txt)
    {
        return BLE_HS_EAPP;
    }
    size_t txt_len = strlen(txt);
    char *txt_with_newline = malloc(txt_len + 2); // 分配内存，包括换行符和终止符
    if (txt_with_newline != NULL)
    {
        memcpy(txt_with_newline, txt, txt_len);
        txt_with_newline[txt_len] = '\n';     // 添加换行符
        txt_with_newline[txt_len + 1] = '\0'; // 添加终止符
    }

    struct os_mbuf *om = ble_hs_mbuf_from_flat(txt_with_newline, strlen(txt_with_newline));
    if (!om)
    {
        MP_LOGE( "no mbuf");
        return BLE_HS_EAPP;
    }
    int rc = ble_gatts_notify_custom(tx_chr_conn_handle, tx_val_handle, om);
    if (rc != 0)
    {
        MP_LOGE( "notify fail %d", rc);
        os_mbuf_free_chain(om); // 只有失败时才释放
    }
    free(txt_with_newline);
    MP_LOGI( "【ble_json_notify】tx indication sent %s", txt);
    return rc;
}

int ble_bin_notify(const uint8_t *data, size_t len)
{
    if (!tx_noti_status || !tx_chr_conn_handle_inited || !data || len == 0)
        return BLE_HS_EAPP;

    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, len);
    if (!om)
        return BLE_HS_ENOMEM;

    int rc = ble_gatts_notify_custom(tx_chr_conn_handle, tx_val_handle, om);
    if (rc != 0)
        os_mbuf_free_chain(om);
    else
        MP_LOGI( "bin notify %d bytes", (int)len);
    return rc;
}

/**
 * @breif 发送订阅数据给手机(notify)，指定句柄
 */
int ble_json_notify_to_conn(uint16_t *connect_handle, const char *txt)
{
    if (!tx_noti_status || !tx_chr_conn_handle_inited || !txt)
    {
        return BLE_HS_EAPP;
    }

    size_t txt_len = strlen(txt);
    char *txt_with_newline = malloc(txt_len + 2); // 分配内存，包括换行符和终止符

    if (txt_with_newline != NULL)
    {
        memcpy(txt_with_newline, txt, txt_len);
        txt_with_newline[txt_len] = '\n';     // 添加换行符
        txt_with_newline[txt_len + 1] = '\0'; // 添加终止符
    }

    struct os_mbuf *om = ble_hs_mbuf_from_flat(txt_with_newline, strlen(txt_with_newline));
    if (!om)
    {
        MP_LOGE( "no mbuf");
        return BLE_HS_EAPP;
    }
    int rc = ble_gatts_notify_custom(*connect_handle, tx_val_handle, om);
    if (rc != 0)
    {
        MP_LOGE( "notify fail %d", rc);
        os_mbuf_free_chain(om); // 只有失败时才释放
    }
    free(txt_with_newline);
    MP_LOGI( "【ble_json_notify_to_conn】tx indication sent %s", om->om_data);
    return rc;
}

int ble_bin_notify_to_conn(uint16_t *connect_handle, const uint8_t *data, size_t len)
{
    if (!tx_noti_status || !tx_chr_conn_handle_inited || !data || len == 0)
        return BLE_HS_EAPP;

    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, len);
    if (!om)
        return BLE_HS_ENOMEM;

    int rc = ble_gatts_notify_custom(*connect_handle, tx_val_handle, om);
    if (rc != 0)
        os_mbuf_free_chain(om);
    else
        MP_LOGI( "bin notify to conn %d bytes", (int)len);

    return rc;
}

/* 手机->处理ble收到的json数据,返回值是要返回给手机的json数据 */
void ble_json_rx(const char *line)
{
    if (line)
    {
        MP_LOGI( "ble_json_rx: %s", line);
        if(strcmp(line,"0303")==0){
            // MP_LOGI("user choose platform command received,platform=%s",line);
            return;
            // ble_json_notify(line);
        }
    }


    if(strcmp(line,BLE_REC_PLATFORM_EYE)==0){
        if(ble_json_notify(BLE_REC_PLATFORM_EYE)==0){
            MP_LOGI( ">>>【通知】回复小程序进入平台：%s:成功",BLE_REC_PLATFORM_EYE);
            platform_idx = 1;
        }
        MP_LOGI(">>>user choose platform double eye,platform=%d",platform_idx);
    }
    else if(strcmp(line,BLE_REC_PLATFORM_BADGE)==0){
        if(ble_json_notify(BLE_REC_PLATFORM_BADGE)==0){
            MP_LOGI( ">>>【通知】回复小程序进入平台：%s:成功",BLE_REC_PLATFORM_BADGE);
            platform_idx = 2;
        }
         MP_LOGI(">>>user choose platform badge,platform=%d",platform_idx);
    }
    else{
        // 根据搜到文件名，拼接http请求url
        char url[256]; // 确保有足够的空间存储完整的URL
        if(platform_idx == 1)
            sprintf(url, "http://tui.doit.am/sucai/uploads/%s", line);
        else if(platform_idx == 2)
            sprintf(url, "http://tui.doit.am/second_dimension/uploads/20%s", line);
            
        doit_file_result_t ret = doit_file_download(url, get_show_dir());
        int rc;
        if (ret.err_code != CL_OPRET_SUCCESS)
        {
            MP_LOGI("ssss");
            // 提示手机下载失败
            if(ble_json_notify(BLE_RESP_FAIL)==0){
                MP_LOGI( ">>>【通知】回复小程序:失败");
            }
        }else{
             MP_LOGI("ffff");
            // 下载完成，回复小程序，然后重启
            if(ble_json_notify(BLE_RESP_OK)==0){
                MP_LOGI( ">>>【通知】回复小程序:成功");
            }
        }
        doit_file_decode(); // 重新启动VPG视频播放
    }
}
