/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
#ifndef GATT_SVR_H
#define GATT_SVR_H

/* Includes */
/* NimBLE GATT APIs */
#include "host/ble_gatt.h"
#include "services/gatt/ble_svc_gatt.h"

/* NimBLE GAP APIs */
#include "host/ble_gap.h"

#include "cJSON.h"




/* Public function declarations */
void gatt_svr_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg);
void gatt_svr_subscribe_cb(struct ble_gap_event *event);
int gatt_svc_init(void);

void ble_json_rx(const char *line);
int ble_json_notify(const char *txt);
int ble_bin_notify(const uint8_t *data, size_t len);
int ble_json_notify_to_conn(uint16_t *connect_handle, const char *txt);
int ble_bin_notify_to_conn(uint16_t *connect_handle, const uint8_t *data, size_t len);
#endif // GATT_SVR_H
