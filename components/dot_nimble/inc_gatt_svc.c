/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
/* Includes */
#include "inc_gatt_svc.h"
#include "dn_common.h"
#include "inc_custom_svc.h"
#include "cJSON.h"

/*
 *  Handle GATT attribute register events
 *      - Service register event
 *      - Characteristic register event
 *      - Descriptor register event
 */
void gatt_svr_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg) {
    /* Local variables */
    char buf[BLE_UUID_STR_LEN];

    /* Handle GATT attributes register events */
    switch (ctxt->op) {
    /* Service register event */
    case BLE_GATT_REGISTER_OP_SVC:
        DN_LOGD("registered service %s with handle=%d",
                ble_uuid_to_str(ctxt->svc.svc_def->uuid, buf),
                ctxt->svc.handle);
        break;

    /* Characteristic register event */
    case BLE_GATT_REGISTER_OP_CHR:
        DN_LOGD(
            "registering characteristic %s with "
            "def_handle=%d val_handle=%d",
            ble_uuid_to_str(ctxt->chr.chr_def->uuid, buf),
            ctxt->chr.def_handle, ctxt->chr.val_handle);
        break;

    /* Descriptor register event */
    case BLE_GATT_REGISTER_OP_DSC:
        DN_LOGD("registering descriptor %s with handle=%d",
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

void gatt_svr_subscribe_cb(struct ble_gap_event *event) {
    /* Check connection handle */
    if (event->subscribe.conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        DN_LOGI("subscribe event; conn_handle=%d attr_handle=%d",
                event->subscribe.conn_handle, event->subscribe.attr_handle);
    } else {
        DN_LOGI("subscribe by nimble stack; attr_handle=%d",
                event->subscribe.attr_handle);
    }

    /** 自定义服务的GATT订阅事件 */
    inc_custom_svc_gatt_svr_subscribe_handle(event);
}

/*
 *  GATT server initialization
 *      1. Initialize GATT service
 *      2. Update NimBLE host GATT services counter
 *      3. Add GATT services to server
 */
int gatt_svc_init(void) {
    /* Local variables */
    int rc;

    /* 1. GATT service initialization */
    ble_svc_gatt_init();

    /* 2. Update GATT services counter */
    rc = ble_gatts_count_cfg(get_cust_gatt_svc_def());
    if (rc != 0) {
        return rc;
    }

    /* 3. Add GATT services */
    rc = ble_gatts_add_svcs(get_cust_gatt_svc_def());
    if (rc != 0) {
        return rc;
    }

    /* 4.Init Custom Svc */
    inc_custom_svc_init();

    return 0;
}
