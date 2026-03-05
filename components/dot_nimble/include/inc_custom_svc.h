#ifndef __INC_CUSTOM_SVC_H__
#define __INC_CUSTOM_SVC_H__

/* Includes */
/* NimBLE GAP APIs */
#include "host/ble_gap.h"
#include "services/gap/ble_svc_gap.h"
#include "inc_custom_svc.h"

/* Defines */
#define BLE_RESP_FAIL "0100"
#define BLE_RESP_OK "0101"
#define BLE_RATIO_360 "0002"
#define BLE_RATIO_240 "0001"
#define BLE_RATIO_160 "0000"
#define BLE_REC_PLATFORM_EYE "0201"   // 用户进入双目
#define BLE_REC_PLATFORM_BADGE "0202" // 用户进入吧唧

typedef void (*cust_nimble_get_info_cb)(char *str); // 接收到蓝牙信息的回调

/* Public function declarations */
void cust_manufacturer_data(uint8_t *mfg);
void inc_custom_svc_init(void);
void inc_custom_svc_gatt_svr_subscribe_handle(struct ble_gap_event *event);
struct ble_gatt_svc_def *get_cust_gatt_svc_def(void);
ble_uuid128_t *get_svc_uuid128(void);
void inc_set_nimble_rec_info_cb(cust_nimble_get_info_cb cb);
bool inc_nimble_notify(const char *str);

#endif // __INC_CUSTOM_SVC_H__
