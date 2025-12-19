/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
/* Includes */
#include "doit_ble.h"
#include "ble_common.h"
#include "gap.h"
#include "gatt_svc.h"

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

static doit_ble_ready_cb_t ble_ready_cb = NULL;

/* Private functions */
/*
 *  Stack event callback functions
 *      - on_stack_reset is called when host resets BLE stack due to errors
 *      - on_stack_sync is called when host has synced with controller
 */
static void on_stack_reset(int reason)
{
    /* On reset, print reset reason to console */
    MP_LOGI( "nimble stack reset, reset reason: %d", reason);
}

static void on_stack_sync(void)
{
    /* On stack sync, do advertising initialization */
    adv_init();
}

static void nimble_host_config_init(void)
{
    /* Set host callbacks */
    ble_hs_cfg.reset_cb = on_stack_reset;
    ble_hs_cfg.sync_cb = on_stack_sync;
    ble_hs_cfg.gatts_register_cb = gatt_svr_register_cb;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    /* Store host configuration */
    ble_store_config_init();
}

static void nimble_host_task(void *param)
{
    /* Task entry log */
    MP_LOGI( "nimble host task has been started!");

    /* This function won't return until nimble_port_stop() is executed */
    nimble_port_run();

    /* Clean up at exit */
    vTaskDelete(NULL);
}

/**
 * @brief 注册 BLE 就绪回调（仅会调用一次）
 * @param cb 为 NULL 则注销
 */
void doit_ble_ready_cb_register(doit_ble_ready_cb_t cb)
{
    ble_ready_cb = cb;
}

void min_program_ble_start(void)
{
    // if (ble_ready_cb)
    // {
    //     ble_ready_cb();
    //     ble_ready_cb = NULL; /* 只调用一次，可省 */
    //     vTaskDelay(2000);    // 等待回调执行完
    // }
    /* Local variables */
    int rc;
    esp_err_t ret;

    /*
     * NVS flash initialization
     * Dependency of BLE stack to store configurations
     */
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK)
    {
        MP_LOGE( "failed to initialize nvs flash, error code: %d ", ret);
        return;
    }

    /* NimBLE stack initialization */
    ret = nimble_port_init();
    if (ret != ESP_OK)
    {
        MP_LOGE( "failed to initialize nimble stack, error code: %d ",
                 ret);
        return;
    }

    /* GAP service initialization */
    rc = gap_init();
    if (rc != 0)
    {
        MP_LOGE( "failed to initialize GAP service, error code: %d", rc);
        return;
    }

    /* GATT server initialization */
    rc = gatt_svc_init();
    if (rc != 0)
    {
        MP_LOGE( "failed to initialize GATT server, error code: %d", rc);
        return;
    }

    /* NimBLE host configuration initialization */
    nimble_host_config_init();

    /* Start NimBLE host task thread and return */
    xTaskCreate(nimble_host_task, "NimBLE Host", 4 * 1024, NULL, 5, NULL);

    ble_initialized = true;
}

bool min_program_in_ota_mode(void)
{
    return ble_initialized;
}


void min_program_ble_stop(void)
{

    // /* 1. 停止广播 */
    // ble_gap_adv_stop();

    // ble_gap_terminate(*conn_handle1, BLE_ERR_REM_USER_CONN_TERM);

    nimble_port_stop();             // ① 停止 NimBLE Host（即 NimBLE 主线程）
    vTaskDelay(pdMS_TO_TICKS(150)); // 等 Host 任务退出（或用事件同步）
    nimble_port_deinit();           // ② 释放 NimBLE Port 的内部任务和资源
    esp_nimble_hci_deinit();
    esp_bt_controller_disable(); // ④ 禁用底层蓝牙控制器
    esp_bt_controller_deinit();  // ⑤ 释放控制器内存

    ble_initialized = false;
    // esp_bt_mem_release(ESP_BT_MODE_BTDM); // ★ 归还 BTDM 内存到系统堆（关键）

    // /* 2. 主动断开所有已连接链路 */
    // struct ble_gap_conn_desc desc;
    // while (ble_gap_conn_find(0xffff, &desc) == 0)
    // {
    //     ble_gap_terminate(desc.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    //     vTaskDelay(10 / portTICK_PERIOD_MS);
    // }

    // /* 3. 释放GATT服务资源 */
    // ble_svc_gatt_deinit(); // 需要实现对应的反初始化函数

    // /* 4. 通知 NimBLE 主机任务退出 */
    // nimble_port_stop();

    // /* 5. 等待主机任务结束 */
    // vTaskDelay(100 / portTICK_PERIOD_MS);

    // /* 6. 反初始化协议栈 */
    // nimble_port_deinit();

    // /* 7. 反初始化HCI */
    // esp_nimble_hci_deinit();

    MP_LOGI( "NimBLE completely stopped");
}
