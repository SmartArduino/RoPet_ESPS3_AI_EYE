#ifndef __DOIT_BLE_H__
#define __DOIT_BLE_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>
#include <stdbool.h>

    /* 用户回调类型：BLE 协议栈完全就绪 */
    typedef void (*doit_ble_ready_cb_t)(void);

    void min_program_ble_start(void);
    void min_program_ble_stop(void);
    bool min_program_in_ota_mode(void);
    void doit_ble_ready_cb_register(doit_ble_ready_cb_t cb);
#ifdef __cplusplus
}
#endif

#endif // __DOIT_BLE_H__