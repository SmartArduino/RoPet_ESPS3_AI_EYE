/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
/* Includes */
#include "ext_nimble_svc.h"
#include "dn_common.h"
#include "inc_gap.h"
#include "inc_gatt_svc.h"

#include "esp_bt.h"
#include "esp_nimble_hci.h"

static bool ble_initialized = false;

/* Library function declarations */
void ble_store_config_init(void);

/* Private function declarations */
static void on_stack_reset(int reason);
static void on_stack_sync(void);
static void nimble_host_config_init(void);
static void nimble_host_task(void *param);

// static doit_ble_ready_cb_t ble_ready_cb = NULL;

/* Private functions */
/*
 *  Stack event callback functions
 *      - on_stack_reset is called when host resets BLE stack due to errors
 *      - on_stack_sync is called when host has synced with controller
 */
static void on_stack_reset(int reason) {
    /* On reset, print reset reason to console */
    DN_LOGI("nimble stack reset, reset reason: %d", reason);
}

static void on_stack_sync(void) {
    /* On stack sync, do advertising initialization */
    adv_init();
}

static void nimble_host_config_init(void) {
    /* Set host callbacks */
    ble_hs_cfg.reset_cb = on_stack_reset;
    ble_hs_cfg.sync_cb = on_stack_sync;
    ble_hs_cfg.gatts_register_cb = gatt_svr_register_cb;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    /* Store host configuration */
    ble_store_config_init();
}

static void nimble_host_task(void *param) {
    /* Task entry log */
    DN_LOGI("nimble host task has been started!");

    /* This function won't return until nimble_port_stop() is executed */
    nimble_port_run();

    /* Clean up at exit */
    vTaskDelete(NULL);
}

// /**
//  * @brief 注册 BLE 就绪回调（仅会调用一次）
//  * @param cb 为 NULL 则注销
//  */
// void doit_ble_ready_cb_register(doit_ble_ready_cb_t cb)
// {
//     ble_ready_cb = cb;
// }

bool dot_send_str_to_phone(const char *str) {
    return inc_nimble_notify(str);
}

void dot_nimble_init(cust_nimble_get_info_cb cb) {
    if (ble_initialized) return;

    /* Local variables */
    int rc;
    esp_err_t ret;
    /*
     * NVS flash initialization
     * Dependency of BLE stack to store configurations
     */
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        DN_LOGE("failed to initialize nvs flash, error code: %d ", ret);
        return;
    }

    /* NimBLE stack initialization */
    ret = nimble_port_init();
    if (ret != ESP_OK) {
        DN_LOGE("failed to initialize nimble stack, error code: %d ",
                ret);
        return;
    }

    /* GAP service initialization */
    rc = gap_init();
    if (rc != 0) {
        DN_LOGE("failed to initialize GAP service, error code: %d", rc);
        return;
    }

    /* GATT server initialization */
    rc = gatt_svc_init();
    if (rc != 0) {
        DN_LOGE("failed to initialize GATT server, error code: %d", rc);
        return;
    }

    /* NimBLE host configuration initialization */
    nimble_host_config_init();

    /* Start NimBLE host task thread and return */
    xTaskCreate(nimble_host_task, "NimBLE Host", 4 * 1024, NULL, 5, NULL);

    /* 接收到蓝牙信息的回调初始化 */
    inc_set_nimble_rec_info_cb(cb);

    ble_initialized = true;
}

void dot_nimble_deinit(void) {
    nimble_port_stop();             // ① 停止 NimBLE Host（即 NimBLE 主线程）
    vTaskDelay(pdMS_TO_TICKS(150)); // 等 Host 任务退出（或用事件同步）
    nimble_port_deinit();           // ② 释放 NimBLE Port 的内部任务和资源
    esp_nimble_hci_deinit();
    esp_bt_controller_disable(); // ④ 禁用底层蓝牙控制器
    esp_bt_controller_deinit();  // ⑤ 释放控制器内存

    DN_LOGI("NimBLE completely stopped");
}
