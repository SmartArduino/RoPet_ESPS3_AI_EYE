
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#if CONFIG_BT_CONTROLLER_ENABLED || !CONFIG_BT_NIMBLE_ENABLED
#include "esp_bt.h"
#endif

#include "esp_blufi_api.h"
#include "doit_blufi.h"

#include "esp_blufi.h"
#define TAG "DOIT_BLUFI"
#define EXAMPLE_WIFI_CONNECTION_MAXIMUM_RETRY 10
#define EXAMPLE_INVALID_REASON                255
#define EXAMPLE_INVALID_RSSI                  -128

static uint8_t g_bind_status = 0;
static void example_event_callback(esp_blufi_cb_event_t event, esp_blufi_cb_param_t *param);

#define WIFI_LIST_NUM   10

static wifi_config_t sta_config;
static wifi_config_t ap_config;

/* FreeRTOS event group to signal when we are connected & ready to make a request */
static EventGroupHandle_t wifi_event_group;

/* The event group allows multiple bits for each event,
   but we only care about one event - are we connected
   to the AP with an IP? */
const int CONNECTED_BIT = BIT0;

static uint8_t example_wifi_retry = 0;

/* store the station info for send back to phone */
static bool gl_sta_connected = false;
static bool gl_sta_got_ip = false;
static bool ble_is_connected = false;
static uint8_t gl_sta_bssid[6];
static uint8_t gl_sta_ssid[32];
static int gl_sta_ssid_len;
static wifi_sta_list_t gl_sta_list;
static bool gl_sta_is_connecting = false;
static esp_blufi_extra_info_t gl_sta_conn_info;

static void example_record_wifi_conn_info(int rssi, uint8_t reason)
{
    memset(&gl_sta_conn_info, 0, sizeof(esp_blufi_extra_info_t));
    if (gl_sta_is_connecting) {
        gl_sta_conn_info.sta_max_conn_retry_set = true;
        gl_sta_conn_info.sta_max_conn_retry = EXAMPLE_WIFI_CONNECTION_MAXIMUM_RETRY;
    } else {
        gl_sta_conn_info.sta_conn_rssi_set = true;
        gl_sta_conn_info.sta_conn_rssi = rssi;
        gl_sta_conn_info.sta_conn_end_reason_set = true;
        gl_sta_conn_info.sta_conn_end_reason = reason;
    }
}

static void example_wifi_connect(void)
{
    example_wifi_retry = 0;
    gl_sta_is_connecting = (esp_wifi_connect() == ESP_OK);
    example_record_wifi_conn_info(EXAMPLE_INVALID_RSSI, EXAMPLE_INVALID_REASON);
}

static bool example_wifi_reconnect(void)
{
    bool ret;
    if (gl_sta_is_connecting && example_wifi_retry++ < EXAMPLE_WIFI_CONNECTION_MAXIMUM_RETRY) {
        ESP_LOGI(TAG, "BLUFI WiFi starts reconnection\n");
        gl_sta_is_connecting = (esp_wifi_connect() == ESP_OK);
        example_record_wifi_conn_info(EXAMPLE_INVALID_RSSI, EXAMPLE_INVALID_REASON);
        ret = true;
    } else {
        ret = false;
    }
    return ret;
}

static int softap_get_current_connection_number(void)
{
    esp_err_t ret;
    ret = esp_wifi_ap_get_sta_list(&gl_sta_list);
    if (ret == ESP_OK)
    {
        return gl_sta_list.num;
    }

    return 0;
}

static void ip_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    wifi_mode_t mode;

    switch (event_id) {
    case IP_EVENT_STA_GOT_IP: {
        esp_blufi_extra_info_t info;

        xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
        esp_wifi_get_mode(&mode);

        memset(&info, 0, sizeof(esp_blufi_extra_info_t));
        memcpy(info.sta_bssid, gl_sta_bssid, 6);
        info.sta_bssid_set = true;
        info.sta_ssid = gl_sta_ssid;
        info.sta_ssid_len = gl_sta_ssid_len;
        gl_sta_got_ip = true;
        if (ble_is_connected == true) {
            esp_blufi_send_wifi_conn_report(mode, ESP_BLUFI_STA_CONN_SUCCESS, softap_get_current_connection_number(), &info);
        } else {
            ESP_LOGI(TAG, "BLUFI BLE is not connected yet\n");
        }
        break;
    }
    default:
        break;
    }
    return;
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    wifi_event_sta_connected_t *event;
    wifi_event_sta_disconnected_t *disconnected_event;
    wifi_mode_t mode;

    switch (event_id) {
    case WIFI_EVENT_STA_START:
        // example_wifi_connect();
        esp_wifi_connect();
        break;
    case WIFI_EVENT_STA_CONNECTED:
        gl_sta_connected = true;
        gl_sta_is_connecting = false;
        event = (wifi_event_sta_connected_t*) event_data;
        memcpy(gl_sta_bssid, event->bssid, 6);
        memcpy(gl_sta_ssid, event->ssid, event->ssid_len);
        gl_sta_ssid_len = event->ssid_len;
        ESP_LOGI(TAG, "BLUFI WiFi connected to SSID: %s, BSSID: "MACSTR", channel: %d, authmode: %d",
                   gl_sta_ssid, MAC2STR(gl_sta_bssid), event->channel, event->authmode);
        blufi_storage_write_has_config(true);
        break;
    case WIFI_EVENT_STA_DISCONNECTED:
        /* Only handle reconnection during connecting */
        if (gl_sta_connected == false && example_wifi_reconnect() == false) {
            gl_sta_is_connecting = false;
            disconnected_event = (wifi_event_sta_disconnected_t*) event_data;
            example_record_wifi_conn_info(disconnected_event->rssi, disconnected_event->reason);
        }
        /* This is a workaround as ESP32 WiFi libs don't currently
           auto-reassociate. */
        gl_sta_connected = false;
        gl_sta_got_ip = false;
        memset(gl_sta_ssid, 0, 32);
        memset(gl_sta_bssid, 0, 6);
        gl_sta_ssid_len = 0;
        xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
        break;
    case WIFI_EVENT_AP_START:
        esp_wifi_get_mode(&mode);

        /* TODO: get config or information of softap, then set to report extra_info */
        if (ble_is_connected == true) {
            if (gl_sta_connected) {
                esp_blufi_extra_info_t info;
                memset(&info, 0, sizeof(esp_blufi_extra_info_t));
                memcpy(info.sta_bssid, gl_sta_bssid, 6);
                info.sta_bssid_set = true;
                info.sta_ssid = gl_sta_ssid;
                info.sta_ssid_len = gl_sta_ssid_len;
                esp_blufi_send_wifi_conn_report(mode, gl_sta_got_ip ? ESP_BLUFI_STA_CONN_SUCCESS : ESP_BLUFI_STA_NO_IP, softap_get_current_connection_number(), &info);
            } else if (gl_sta_is_connecting) {
                esp_blufi_send_wifi_conn_report(mode, ESP_BLUFI_STA_CONNECTING, softap_get_current_connection_number(), &gl_sta_conn_info);
            } else {
                esp_blufi_send_wifi_conn_report(mode, ESP_BLUFI_STA_CONN_FAIL, softap_get_current_connection_number(), &gl_sta_conn_info);
            }
        } else {
            ESP_LOGI(TAG, "BLUFI BLE is not connected yet\n");
        }
        break;
    case WIFI_EVENT_SCAN_DONE: {
        uint16_t apCount = 0;
        esp_wifi_scan_get_ap_num(&apCount);
        if (apCount == 0) {
            ESP_LOGI(TAG, "Nothing AP found");
            break;
        }
        wifi_ap_record_t *ap_list = (wifi_ap_record_t *)malloc(sizeof(wifi_ap_record_t) * apCount);
        if (!ap_list) {
            ESP_LOGE(TAG, "malloc error, ap_list is NULL");
            esp_wifi_clear_ap_list();
            break;
        }
        ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&apCount, ap_list));
        ESP_LOGI("BLUFI", "Found %d APs", apCount);
        for (int i = 0; i < apCount; ++i)
        {
            ESP_LOGI(TAG, "AP[%d]: SSID: %s, RSSI: %d", i, ap_list[i].ssid, ap_list[i].rssi);
        }
        esp_blufi_ap_record_t * blufi_ap_list = (esp_blufi_ap_record_t *)malloc(apCount * sizeof(esp_blufi_ap_record_t));
        if (!blufi_ap_list) {
            if (ap_list) {
                free(ap_list);
            }
            ESP_LOGE(TAG, "malloc error, blufi_ap_list is NULL");
            break;
        }
        for (int i = 0; i < apCount; ++i)
        {
            blufi_ap_list[i].rssi = ap_list[i].rssi;
            memcpy(blufi_ap_list[i].ssid, ap_list[i].ssid, sizeof(ap_list[i].ssid));
        }

        if (ble_is_connected == true) {
            ESP_LOGI("BLUFI", "Send WiFi list to phone");
            esp_blufi_send_wifi_list(apCount, blufi_ap_list);
        } else {
            ESP_LOGI(TAG, "BLUFI BLE is not connected yet\n");
        }

        esp_wifi_scan_stop();
        free(ap_list);
        free(blufi_ap_list);
        break;
    }
    case WIFI_EVENT_AP_STACONNECTED: {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" join, AID=%d", MAC2STR(event->mac), event->aid);
        break;
    }
    case WIFI_EVENT_AP_STADISCONNECTED: {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" leave, AID=%d, reason=%d", MAC2STR(event->mac), event->aid, event->reason);
        break;
    }

    default:
        break;
    }
    return;
}

static void initialise_wifi(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    wifi_event_group = xEventGroupCreate();
    // ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);
    esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();
    assert(ap_netif);
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &ip_event_handler, NULL));

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
    example_record_wifi_conn_info(EXAMPLE_INVALID_RSSI, EXAMPLE_INVALID_REASON);
    ESP_ERROR_CHECK( esp_wifi_start() );
}

static esp_blufi_callbacks_t example_callbacks = {
    .event_cb = example_event_callback,
    // .negotiate_data_handler = blufi_dh_negotiate_data_handler,
    // .encrypt_func = blufi_aes_encrypt,
    // .decrypt_func = blufi_aes_decrypt,
    // .checksum_func = blufi_crc_checksum,
};

static void example_event_callback(esp_blufi_cb_event_t event, esp_blufi_cb_param_t *param)
{
    /* actually, should post to blufi_task handle the procedure,
     * now, as a example, we do it more simply */
    switch (event) {
    case ESP_BLUFI_EVENT_INIT_FINISH:
        ESP_LOGI(TAG, "BLUFI init finish\n");

        esp_blufi_adv_start();
        break;
    case ESP_BLUFI_EVENT_DEINIT_FINISH:
        ESP_LOGI(TAG, "BLUFI deinit finish\n");
        break;
    case ESP_BLUFI_EVENT_BLE_CONNECT:
        ESP_LOGI(TAG, "BLUFI ble connect\n");
        ble_is_connected = true;
        esp_blufi_adv_stop();
        // blufi_security_init();
        break;
    case ESP_BLUFI_EVENT_BLE_DISCONNECT:
        ESP_LOGI(TAG, "BLUFI ble disconnect\n");
        ble_is_connected = false;
        // blufi_security_deinit();
        esp_blufi_adv_start();
        break;
    case ESP_BLUFI_EVENT_SET_WIFI_OPMODE:
        ESP_LOGI(TAG, "BLUFI Set WIFI opmode %d\n", param->wifi_mode.op_mode);
        ESP_ERROR_CHECK( esp_wifi_set_mode(param->wifi_mode.op_mode) );
        break;
    case ESP_BLUFI_EVENT_REQ_CONNECT_TO_AP:
        ESP_LOGI(TAG, "BLUFI request wifi connect to AP\n");
        /* there is no wifi callback when the device has already connected to this wifi
        so disconnect wifi before connection.
        */
        esp_wifi_disconnect();
        example_wifi_connect();
        break;
    case ESP_BLUFI_EVENT_REQ_DISCONNECT_FROM_AP:
        ESP_LOGI(TAG, "BLUFI request wifi disconnect from AP\n");
        esp_wifi_disconnect();
        break;
    case ESP_BLUFI_EVENT_REPORT_ERROR:
        ESP_LOGE(TAG, "BLUFI report error, error code %d\n", param->report_error.state);
        esp_blufi_send_error_info(param->report_error.state);
        break;
    case ESP_BLUFI_EVENT_GET_WIFI_STATUS: {
        wifi_mode_t mode;
        esp_blufi_extra_info_t info;

        esp_wifi_get_mode(&mode);

        if (gl_sta_connected) {
            memset(&info, 0, sizeof(esp_blufi_extra_info_t));
            memcpy(info.sta_bssid, gl_sta_bssid, 6);
            info.sta_bssid_set = true;
            info.sta_ssid = gl_sta_ssid;
            info.sta_ssid_len = gl_sta_ssid_len;
            esp_blufi_send_wifi_conn_report(mode, gl_sta_got_ip ? ESP_BLUFI_STA_CONN_SUCCESS : ESP_BLUFI_STA_NO_IP, softap_get_current_connection_number(), &info);
        } else if (gl_sta_is_connecting) {
            esp_blufi_send_wifi_conn_report(mode, ESP_BLUFI_STA_CONNECTING, softap_get_current_connection_number(), &gl_sta_conn_info);
        } else {
            esp_blufi_send_wifi_conn_report(mode, ESP_BLUFI_STA_CONN_FAIL, softap_get_current_connection_number(), &gl_sta_conn_info);
        }
        ESP_LOGI(TAG, "BLUFI get wifi status from AP\n");

        break;
    }
    case ESP_BLUFI_EVENT_RECV_SLAVE_DISCONNECT_BLE:
        ESP_LOGI(TAG, "blufi close a gatt connection");
        esp_blufi_disconnect();
        break;
    case ESP_BLUFI_EVENT_DEAUTHENTICATE_STA:
        /* TODO */
        break;
	case ESP_BLUFI_EVENT_RECV_STA_BSSID:
        memcpy(sta_config.sta.bssid, param->sta_bssid.bssid, 6);
        sta_config.sta.bssid_set = 1;
        esp_wifi_set_config(WIFI_IF_STA, &sta_config);
        ESP_LOGI(TAG, "Recv STA BSSID %s\n", sta_config.sta.ssid);
        break;
	case ESP_BLUFI_EVENT_RECV_STA_SSID:
        if (param->sta_ssid.ssid_len >= sizeof(sta_config.sta.ssid)/sizeof(sta_config.sta.ssid[0])) {
            esp_blufi_send_error_info(ESP_BLUFI_DATA_FORMAT_ERROR);
            ESP_LOGI(TAG, "Invalid STA SSID\n");
            break;
        }
        strncpy((char *)sta_config.sta.ssid, (char *)param->sta_ssid.ssid, param->sta_ssid.ssid_len);
        sta_config.sta.ssid[param->sta_ssid.ssid_len] = '\0';
        esp_wifi_set_config(WIFI_IF_STA, &sta_config);
        ESP_LOGI(TAG, "Recv STA SSID %s\n", sta_config.sta.ssid);
        blufi_storage_write_wifi_ssid((char *)sta_config.sta.ssid);
        
        break;
	case ESP_BLUFI_EVENT_RECV_STA_PASSWD:
        if (param->sta_passwd.passwd_len >= sizeof(sta_config.sta.password)/sizeof(sta_config.sta.password[0])) {
            esp_blufi_send_error_info(ESP_BLUFI_DATA_FORMAT_ERROR);
            ESP_LOGI(TAG, "Invalid STA PASSWORD\n");
            break;
        }
        strncpy((char *)sta_config.sta.password, (char *)param->sta_passwd.passwd, param->sta_passwd.passwd_len);
        sta_config.sta.password[param->sta_passwd.passwd_len] = '\0';
        // sta_config.sta.threshold.authmode = EXAMPLE_WIFI_SCAN_AUTH_MODE_THRESHOLD;
        esp_wifi_set_config(WIFI_IF_STA, &sta_config);
        ESP_LOGI(TAG, "Recv STA PASSWORD %s\n", sta_config.sta.password);
        blufi_storage_write_wifi_password((char *)sta_config.sta.password);
        break;
	case ESP_BLUFI_EVENT_RECV_SOFTAP_SSID:
        if (param->softap_ssid.ssid_len >= sizeof(ap_config.ap.ssid)/sizeof(ap_config.ap.ssid[0])) {
            esp_blufi_send_error_info(ESP_BLUFI_DATA_FORMAT_ERROR);
            ESP_LOGI(TAG, "Invalid SOFTAP SSID\n");
            break;
        }
        strncpy((char *)ap_config.ap.ssid, (char *)param->softap_ssid.ssid, param->softap_ssid.ssid_len);
        ap_config.ap.ssid[param->softap_ssid.ssid_len] = '\0';
        ap_config.ap.ssid_len = param->softap_ssid.ssid_len;
        esp_wifi_set_config(WIFI_IF_AP, &ap_config);
        ESP_LOGI(TAG, "Recv SOFTAP SSID %s, ssid len %d\n", ap_config.ap.ssid, ap_config.ap.ssid_len);
        break;
	case ESP_BLUFI_EVENT_RECV_SOFTAP_PASSWD:
        if (param->softap_passwd.passwd_len >= sizeof(ap_config.ap.password)/sizeof(ap_config.ap.password[0])) {
            esp_blufi_send_error_info(ESP_BLUFI_DATA_FORMAT_ERROR);
            ESP_LOGI(TAG, "Invalid SOFTAP PASSWD\n");
            break;
        }
        strncpy((char *)ap_config.ap.password, (char *)param->softap_passwd.passwd, param->softap_passwd.passwd_len);
        ap_config.ap.password[param->softap_passwd.passwd_len] = '\0';
        esp_wifi_set_config(WIFI_IF_AP, &ap_config);
        ESP_LOGI(TAG, "Recv SOFTAP PASSWORD %s len = %d\n", ap_config.ap.password, param->softap_passwd.passwd_len);
        break;
	case ESP_BLUFI_EVENT_RECV_SOFTAP_MAX_CONN_NUM:
        if (param->softap_max_conn_num.max_conn_num > 4) {
            return;
        }
        ap_config.ap.max_connection = param->softap_max_conn_num.max_conn_num;
        esp_wifi_set_config(WIFI_IF_AP, &ap_config);
        ESP_LOGI(TAG, "Recv SOFTAP MAX CONN NUM %d\n", ap_config.ap.max_connection);
        break;
	case ESP_BLUFI_EVENT_RECV_SOFTAP_AUTH_MODE:
        if (param->softap_auth_mode.auth_mode >= WIFI_AUTH_MAX) {
            return;
        }
        ap_config.ap.authmode = param->softap_auth_mode.auth_mode;
        esp_wifi_set_config(WIFI_IF_AP, &ap_config);
        ESP_LOGI(TAG, "Recv SOFTAP AUTH MODE %d\n", ap_config.ap.authmode);
        break;
	case ESP_BLUFI_EVENT_RECV_SOFTAP_CHANNEL:
        if (param->softap_channel.channel > 13) {
            return;
        }
        ap_config.ap.channel = param->softap_channel.channel;
        esp_wifi_set_config(WIFI_IF_AP, &ap_config);
        ESP_LOGI(TAG, "Recv SOFTAP CHANNEL %d\n", ap_config.ap.channel);
        break;
    case ESP_BLUFI_EVENT_GET_WIFI_LIST:{
        ESP_LOGI(TAG, "Recv GET WIFI LIST \n");

        wifi_scan_config_t scanConf = {
            .ssid = NULL,
            .bssid = NULL,
            .channel = 0,
            .show_hidden = false
        };
        esp_err_t ret = esp_wifi_scan_start(&scanConf, true);
        if (ret != ESP_OK) {
            esp_blufi_send_error_info(ESP_BLUFI_WIFI_SCAN_FAIL);
        }
        break;
    }
    case ESP_BLUFI_EVENT_RECV_CUSTOM_DATA:
        ESP_LOGI(TAG, "Recv Custom Data %" PRIu32 "\n", param->custom_data.data_len);
        ESP_LOG_BUFFER_HEX("Custom Data", param->custom_data.data, param->custom_data.data_len);
        
        // 解析AT+OTA命令
        if (param->custom_data.data_len > 7) { // 至少需要"AT+OTA="的长度
            char *data_str = (char*)malloc(param->custom_data.data_len + 1);
            if (data_str) {
                memcpy(data_str, param->custom_data.data, param->custom_data.data_len);
                data_str[param->custom_data.data_len] = '\0'; // 确保字符串以null结尾
                
                // 检查是否是AT+OTA命令
                if (strncmp(data_str, "AT+OTA=", 7) == 0) {
                    char *url = data_str + 7; // 跳过"AT+OTA="部分
                    ESP_LOGI(TAG, "解析到OTA URL: %s", url);
                    blufi_storage_write_ota_url(url);
                } else {
                    ESP_LOGI(TAG, "接收到的自定义数据: %s", data_str);
                }
                
                free(data_str);
            } else {
                ESP_LOGE(TAG, "内存分配失败");
            }
        }
        break;
	case ESP_BLUFI_EVENT_RECV_USERNAME:
        /* Not handle currently */
        break;
	case ESP_BLUFI_EVENT_RECV_CA_CERT:
        /* Not handle currently */
        break;
	case ESP_BLUFI_EVENT_RECV_CLIENT_CERT:
        /* Not handle currently */
        break;
	case ESP_BLUFI_EVENT_RECV_SERVER_CERT:
        /* Not handle currently */
        break;
	case ESP_BLUFI_EVENT_RECV_CLIENT_PRIV_KEY:
        /* Not handle currently */
        break;;
	case ESP_BLUFI_EVENT_RECV_SERVER_PRIV_KEY:
        /* Not handle currently */
        break;
    default:
        break;
    }
}

#define NVS_PARTITION_NAME "nvs"

static nvs_handle_t nvs_handle_;

// 读取blob，注意key最长16个字符
void _nvs_get_blob(const char *key, uint8_t *value) {
  size_t len = 0;
  ESP_ERROR_CHECK(nvs_open(NVS_PARTITION_NAME, NVS_READWRITE, &nvs_handle_));
  nvs_get_blob(nvs_handle_, key, NULL, &len);
  nvs_get_blob(nvs_handle_, key, value, &len);
  nvs_close(nvs_handle_);
}
// 读取blob长度，注意key最长16个字符
size_t _nvs_get_blob_len(const char *key) {
  size_t len = 0;
  ESP_ERROR_CHECK(nvs_open(NVS_PARTITION_NAME, NVS_READWRITE, &nvs_handle_));
  nvs_get_blob(nvs_handle_, key, NULL, &len);
  nvs_close(nvs_handle_);
  return len;
}
// 写入blob，注意key最长16个字符
void _nvs_set_blob(const char *key, uint8_t *value, size_t len) {
  ESP_ERROR_CHECK(nvs_open(NVS_PARTITION_NAME, NVS_READWRITE, &nvs_handle_));
  ESP_ERROR_CHECK(nvs_set_blob(nvs_handle_, key, value, len));
  nvs_commit(nvs_handle_);
  nvs_close(nvs_handle_);
}
// 读取设备是否已配网
bool blufi_storage_read_has_config() {
    bool has_config = false;
    _nvs_get_blob("blufi_has_cfg", (uint8_t *)&has_config);
    return has_config;
}
  
// 写入设备是否已配网
void blufi_storage_write_has_config(bool has_config) {
    _nvs_set_blob("blufi_has_cfg", (uint8_t *)&has_config, sizeof(bool));
}

// 读取wifi ssid
void blufi_storage_read_wifi_ssid(char *ssid) {
    _nvs_get_blob("blufi_wifi_ssid", (uint8_t *)ssid);
  }
  // 读取wifi ssid长度，包括\0
  size_t blufi_storage_read_wifi_ssid_length() {
    return _nvs_get_blob_len("blufi_wifi_ssid");
  }
  // 写入wifi ssid
  void blufi_storage_write_wifi_ssid(const char *ssid) {
    _nvs_set_blob("blufi_wifi_ssid", (uint8_t *)ssid, strlen(ssid) + 1);
  }
  
  // 读取wifi password
  void blufi_storage_read_wifi_password(char *password) {
    _nvs_get_blob("blufi_wifi_psw", (uint8_t *)password);
  }
  // 读取wifi password长度，包括\0
  size_t blufi_storage_read_wifi_password_length() {
    return _nvs_get_blob_len("blufi_wifi_psw");
  }
  // 写入wifi password
  void blufi_storage_write_wifi_password(const char *password) {
    _nvs_set_blob("blufi_wifi_psw", (uint8_t *)password, strlen(password) + 1);
  }
  
  // 读取ota url
  void blufi_storage_read_ota_url(char *url) {
    _nvs_get_blob("blufi_ota_url", (uint8_t *)url);
  }
  // 读取wifi url长度，包括\0
  size_t blufi_storage_read_ota_url_length() {
    return _nvs_get_blob_len("blufi_ota_url");
  }
  // 写入wifi url
  void blufi_storage_write_ota_url(const char *url) {
    _nvs_set_blob("blufi_ota_url", (uint8_t *)url, strlen(url) + 1);
  }

void doit_blufi_send_code(uint8_t *code){
    ESP_LOGI(TAG, "doit_blufi_send_code: %02x %02x %02x %02x %02x %02x", code[0], code[1], code[2], code[3], code[4], code[5]);
    if(ble_is_connected == false){
        ESP_LOGE(TAG, "BLUFI BLE is not connected yet");
        return;
    }
    esp_blufi_send_custom_data((uint8_t *)code, 6);
}

bool blufi_wifi_sta_get_connect_status(){
    return gl_sta_got_ip;
}

void blufi_wifi_start_connect() {
    initialise_wifi();
    ESP_LOGI(TAG, " ------ blufi_wifi_start_connect");
  
    wifi_config_t wifi_config = {
        .sta = {},
    };
    // 读取ssid
    size_t len = blufi_storage_read_wifi_ssid_length();
    if (len <= 0) {
      ESP_LOGI(TAG, "ssid not set");
      return ;
    }
    blufi_storage_read_wifi_ssid((char *)(wifi_config.sta.ssid));
    ESP_LOGI(TAG, "ssid:%s", wifi_config.sta.ssid);
    // 读取密码
    len = blufi_storage_read_wifi_password_length();
    if (len <= 0) {
      ESP_LOGI(TAG, "password not set");
      return ;
    }
    blufi_storage_read_wifi_password((char *)(wifi_config.sta.password));
    ESP_LOGI(TAG, "password:%s", wifi_config.sta.password);
    if (len == 0 || (len == 1 && wifi_config.sta.password[0] == 0)) {
      // 如果没有设密码，则代表wifi是开放的
      wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    } else {
      // 如果设了密码，则需要验证是否符合sae_pwe_h2e的要求
      wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    }
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
  
    esp_wifi_connect();

    ESP_LOGI(TAG, "wifi_init_sta finished.");

  
  }
void doit_blufi_init(void)
{
    esp_err_t ret;
    ESP_ERROR_CHECK(nvs_open(NVS_PARTITION_NAME, NVS_READWRITE, &nvs_handle_));

    ESP_LOGI(TAG, "BLUFI init start");

    initialise_wifi();

#if CONFIG_BT_CONTROLLER_ENABLED || !CONFIG_BT_NIMBLE_ENABLED
    ret = esp_blufi_controller_init();
    if (ret) {
        ESP_LOGE(TAG, "%s BLUFI controller init failed: %s\n", __func__, esp_err_to_name(ret));
        return;
    }
#endif

    ret = esp_blufi_host_and_cb_init(&example_callbacks);
    if (ret) {
        ESP_LOGE(TAG, "%s initialise failed: %s\n", __func__, esp_err_to_name(ret));
        return;
    }

}

void doit_blufi_deinit(void)
{
    esp_err_t ret;

    ESP_LOGI(TAG, "BLUFI deinit start");

    ret = esp_blufi_host_deinit();
    if (ret) {
        ESP_LOGE(TAG, "%s deinit host failed: %s\n", __func__, esp_err_to_name(ret));
    }
#if CONFIG_BT_CONTROLLER_ENABLED || !CONFIG_BT_NIMBLE_ENABLED
    ret = esp_blufi_controller_deinit();
    if (ret) {
        ESP_LOGE(TAG, "%s deinit controller failed: %s\n", __func__, esp_err_to_name(ret));
    }
#endif
    ESP_LOGI(TAG, "BLUFI deinit finished");

}

