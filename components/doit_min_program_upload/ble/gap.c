/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
/* Includes */
#include "gap.h"
#include "ble_common.h"
#include "gatt_svc.h"
#include "doit_ble.h"
#include "doit_ui.h"

/* Private function declarations */
inline static void format_addr(char *addr_str, uint8_t addr[]);
static void print_conn_desc(struct ble_gap_conn_desc *desc);
static void start_advertising(void);
static int gap_event_handler(struct ble_gap_event *event, void *arg);

/* Private variables */
static uint8_t own_addr_type;
static uint8_t addr_val[6] = {0};

/* Private functions */
inline static void format_addr(char *addr_str, uint8_t addr[])
{
    sprintf(addr_str, "%02X:%02X:%02X:%02X:%02X:%02X", addr[0], addr[1],
            addr[2], addr[3], addr[4], addr[5]);
}

static void print_conn_desc(struct ble_gap_conn_desc *desc)
{
    /* Local variables */
    char addr_str[18] = {0};

    /* Connection handle */
    MP_LOGI( "connection handle: %d", desc->conn_handle);

    /* Local ID address */
    format_addr(addr_str, desc->our_id_addr.val);
    MP_LOGI( "device id address: type=%d, value=%s",
             desc->our_id_addr.type, addr_str);

    /* Peer ID address */
    format_addr(addr_str, desc->peer_id_addr.val);
    MP_LOGI( "peer id address: type=%d, value=%s", desc->peer_id_addr.type,
             addr_str);

    /* Connection info */
    MP_LOGI(
             "conn_itvl=%d, conn_latency=%d, supervision_timeout=%d, "
             "encrypted=%d, authenticated=%d, bonded=%d\n",
             desc->conn_itvl, desc->conn_latency, desc->supervision_timeout,
             desc->sec_state.encrypted, desc->sec_state.authenticated,
             desc->sec_state.bonded);
}

/* 构造 ManufacturerData：
   [CompanyID(2 bytes little-endian)] + ['A''Y'(2 bytes魔数)] + [unique_id(2 bytes)] + [ver(1 byte)]
   注意：CompanyID 建议用你真实的 Bluetooth SIG Company ID；没有的话临时用 0xFFFF 也能工作（只是“不规范”）
*/
static void fill_mfg_data(uint8_t *out, size_t out_len, uint16_t unique_id)
{
    // out_len 至少 2+2+2+1 = 7
    if (out_len < 7) return;

    out[0] = 0xE5; out[1] = 0x02;          // Company ID = 0xFFFF (临时)
    out[2] = (uint8_t)(unique_id & 0xFF);  // unique_id L
    out[3] = (uint8_t)(unique_id >> 8);    // unique_id H
    //获取屏幕UI尺寸,分辨率数据：160分辨率：out[4]=0x01; 240分辨率：out[4]=0x02; 360分辨率：out[4]=0x03;
    uint16_t width,height;
    doit_get_ui_screen_size(&width, &height);
    if(width == 160 && height == 160){
        out[4] = 0x00;           
    }else if(width == 240 && height == 240){
        out[4] = 0x01;           
    }else if(width == 368 && height == 368){
        out[4] = 0x02;
    }
    out[5] = 0x00;      // 保留位
    out[6] = 0x01;                         // version
}

static uint16_t get_unique_id_from_mac(void)
{
    uint8_t addr[6] = {0};
    // 取 public address（或你 own addr），用后两字节做 unique_id
    // 注意：在 ble_hs 已同步后调用更稳
    ble_hs_id_copy_addr(BLE_ADDR_PUBLIC, addr, NULL);
    return (uint16_t)((addr[4] << 8) | addr[5]);
}

static void start_advertising(void)
{
    /* Local variables */
    int rc = 0;
    const char *name;
    struct ble_hs_adv_fields adv_fields = {0};
    struct ble_hs_adv_fields rsp_fields = {0};
    struct ble_gap_adv_params adv_params = {0};

    /* Set advertising flags */
    adv_fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

    /* Set device name */
    name = ble_svc_gap_device_name();
    adv_fields.name = (uint8_t *)name;
    adv_fields.name_len = strlen(name);
    adv_fields.name_is_complete = 1;



    
    /* ManufacturerData 放 ADV（iOS 小程序识别最稳） */
     static uint8_t mfg[7];
    uint16_t uid = get_unique_id_from_mac();
    fill_mfg_data(mfg, sizeof(mfg), uid);
    MP_LOGI("mfg_data: %02X %02X %02X %02X %02X %02X %02X",
             mfg[0], mfg[1], mfg[2], mfg[3], mfg[4], mfg[5], mfg[6]);
    adv_fields.mfg_data = mfg;
    adv_fields.mfg_data_len = sizeof(mfg);

    // /* Set device tx power */
    // adv_fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
    // adv_fields.tx_pwr_lvl_is_present = 1;

    // /* Set device appearance */
    // adv_fields.appearance = BLE_GAP_APPEARANCE_GENERIC_TAG;
    // adv_fields.appearance_is_present = 1;

    // /* Set device LE role */
    // adv_fields.le_role = BLE_GAP_LE_ROLE_PERIPHERAL;
    // adv_fields.le_role_is_present = 1;

    /* Set advertiement fields */
    rc = ble_gap_adv_set_fields(&adv_fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "failed to set advertising data, error code: %d", rc);
        return;
    }

        rsp_fields.uuids128 = (ble_uuid128_t *)&svc_uuid;
    rsp_fields.num_uuids128 = 1;
    rsp_fields.uuids128_is_complete = 1;
    // /* Set device address */
    // rsp_fields.device_addr = addr_val;
    // rsp_fields.device_addr_type = own_addr_type;
    // rsp_fields.device_addr_is_present = 1;

    // // /* Set URI */
    // // rsp_fields.uri = esp_uri;
    // // rsp_fields.uri_len = sizeof(esp_uri);

    // /* Set advertising interval */
    // rsp_fields.adv_itvl = BLE_GAP_ADV_ITVL_MS(500);
    // rsp_fields.adv_itvl_is_present = 1;

    /* Set scan response fields */
    rc = ble_gap_adv_rsp_set_fields(&rsp_fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "failed to set scan response data, error code: %d", rc);
        return;
    }

    /* Set non-connetable and general discoverable mode to be a beacon */
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    /* Set advertising interval */
    adv_params.itvl_min = BLE_GAP_ADV_ITVL_MS(100);
    adv_params.itvl_max = BLE_GAP_ADV_ITVL_MS(110);

    /* Start advertising */
    rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER, &adv_params,
                           gap_event_handler, NULL);
    if (rc != 0)
    {
        MP_LOGE( "failed to start advertising, error code: %d", rc);
        return;
    }
    MP_LOGI( "advertising started!");
}

/*
 * NimBLE applies an event-driven model to keep GAP service going
 * gap_event_handler is a callback function registered when calling
 * ble_gap_adv_start API and called when a GAP event arrives
 */
static int gap_event_handler(struct ble_gap_event *event, void *arg)
{
    /* Local variables */
    int rc = 0;
    struct ble_gap_conn_desc desc;

    /* Handle different GAP event */
    switch (event->type)
    {

    /* Connect event */
    case BLE_GAP_EVENT_CONNECT:
        /* A new connection was established or a connection attempt failed. */
        MP_LOGI( "connection %s; status=%d",
                 event->connect.status == 0 ? "established" : "failed",
                 event->connect.status);

        /* Connection succeeded */
        if (event->connect.status == 0)
        {
            /* Check connection handle */
            rc = ble_gap_conn_find(event->connect.conn_handle, &desc);
            if (rc != 0)
            {
                MP_LOGE(
                         "failed to find connection by handle, error code: %d",
                         rc);
                return rc;
            }

            /* Print connection descriptor */
            print_conn_desc(&desc);

            /* Try to update connection parameters */
            struct ble_gap_upd_params params = {.itvl_min = desc.conn_itvl,
                                                .itvl_max = desc.conn_itvl,
                                                .latency = 3,
                                                .supervision_timeout =
                                                    desc.supervision_timeout};
            rc = ble_gap_update_params(event->connect.conn_handle, &params);
            if (rc != 0)
            {
                MP_LOGE(
                    
                    "failed to update connection parameters, error code: %d",
                    rc);
                return rc;
            }
        }
        /* Connection failed, restart advertising */
        else
        {
            start_advertising();
        }
        return rc;

    /* Disconnect event */
    case BLE_GAP_EVENT_DISCONNECT:
        /* A connection was terminated, print connection descriptor */
        MP_LOGI( "disconnected from peer; reason=%d",
                 event->disconnect.reason);

        /* Restart advertising */
        start_advertising();
        return rc;

    /* Connection parameters update event */
    case BLE_GAP_EVENT_CONN_UPDATE:
        /* The central has updated the connection parameters. */
        MP_LOGI( "connection updated; status=%d",
                 event->conn_update.status);

        /* Print connection descriptor */
        rc = ble_gap_conn_find(event->conn_update.conn_handle, &desc);
        if (rc != 0)
        {
            MP_LOGE( "failed to find connection by handle, error code: %d",
                     rc);
            return rc;
        }
        print_conn_desc(&desc);
        return rc;

    /* Advertising complete event */
    case BLE_GAP_EVENT_ADV_COMPLETE:
        /* Advertising completed, restart advertising */
        MP_LOGI( "advertise complete; reason=%d",
                 event->adv_complete.reason);
        start_advertising();
        return rc;

    /* Notification sent event */
    case BLE_GAP_EVENT_NOTIFY_TX:
        if ((event->notify_tx.status != 0) &&
            (event->notify_tx.status != BLE_HS_EDONE))
        {
            /* Print notification info on error */
            MP_LOGI(
                     "notify event; conn_handle=%d attr_handle=%d "
                     "status=%d is_indication=%d",
                     event->notify_tx.conn_handle, event->notify_tx.attr_handle,
                     event->notify_tx.status, event->notify_tx.indication);
        }
        return rc;

    /* Subscribe event */
    case BLE_GAP_EVENT_SUBSCRIBE:
        /* Print subscription info to log */
        MP_LOGI(
                 "subscribe event; conn_handle=%d attr_handle=%d "
                 "reason=%d prevn=%d curn=%d previ=%d curi=%d",
                 event->subscribe.conn_handle, event->subscribe.attr_handle,
                 event->subscribe.reason, event->subscribe.prev_notify,
                 event->subscribe.cur_notify, event->subscribe.prev_indicate,
                 event->subscribe.cur_indicate);

        /* GATT subscribe event callback */
        gatt_svr_subscribe_cb(event);
        return rc;

    /* MTU update event */
    case BLE_GAP_EVENT_MTU:
        /* Print MTU update info to log */
        MP_LOGI( "mtu update event; conn_handle=%d cid=%d mtu=%d",
                 event->mtu.conn_handle, event->mtu.channel_id,
                 event->mtu.value);
        return rc;
    }

    return rc;
}

/* Public functions */
void adv_init(void)
{
    /* Local variables */
    int rc = 0;
    char addr_str[18] = {0};

    /* Make sure we have proper BT identity address set (random preferred) */
    rc = ble_hs_util_ensure_addr(0);
    if (rc != 0)
    {
        MP_LOGE( "device does not have any available bt address!");
        return;
    }

    /* Figure out BT address to use while advertising (no privacy for now) */
    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0)
    {
        MP_LOGE( "failed to infer address type, error code: %d", rc);
        return;
    }

    /* Printing ADDR */
    rc = ble_hs_id_copy_addr(own_addr_type, addr_val, NULL);
    if (rc != 0)
    {
        MP_LOGE( "failed to copy device address, error code: %d", rc);
        return;
    }
    format_addr(addr_str, addr_val);
    MP_LOGI( "device address: %s", addr_str);

    /* Start advertising. */
    start_advertising();
}

int gap_init(void)
{
    /* Local variables */
    int rc = 0;

    /* Call NimBLE GAP initialization API */
    ble_svc_gap_init();

    /* Set GAP device name */
    rc = ble_svc_gap_device_name_set(DEVICE_NAME);
    if (rc != 0)
    {
        MP_LOGE( "failed to set device name to %s, error code: %d",
                 DEVICE_NAME, rc);
        return rc;
    }
    return rc;
}
