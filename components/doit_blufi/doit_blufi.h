/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */


#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_blufi_api.h"
 

 int esp_blufi_gap_register_callback(void);
 esp_err_t esp_blufi_host_init(void);
 esp_err_t esp_blufi_host_and_cb_init(esp_blufi_callbacks_t *callbacks);
 esp_err_t esp_blufi_host_deinit(void);
 esp_err_t esp_blufi_controller_init(void);
 esp_err_t esp_blufi_controller_deinit(void);
 void doit_blufi_init(void);
 void doit_blufi_init(void);
 void blufi_storage_write_has_config(bool has_config);
 bool blufi_storage_read_has_config();
 void doit_blufi_send_code(uint8_t *code);
 bool blufi_wifi_sta_get_connect_status();
 void blufi_wifi_start_connect();
 void blufi_storage_write_wifi_password(const char *password);
void blufi_storage_read_wifi_password(char *password);
size_t blufi_storage_read_wifi_password_length();
void blufi_storage_write_wifi_ssid(const char *ssid);
void blufi_storage_read_wifi_ssid(char *ssid);
void blufi_storage_write_ota_url(const char *url);
void blufi_storage_read_ota_url(char *url);
size_t blufi_storage_read_ota_url_length();
size_t blufi_storage_read_wifi_ssid_length();
void blufi_wifi_start_connect() ;
#ifdef __cplusplus
}
#endif