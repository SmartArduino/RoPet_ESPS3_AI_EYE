/*   unified_csi_radar.c
 *  功能：
 *  1. 上电自动连接 Wi-Fi（使用 example_connect）
 *  2. 连接成功后等待用户按下 BOOT 键(GPIO0)，低电平有效
 *  3. 按键触发后倒计时 10 s，倒计时 log 实时输出
 *  4. 倒计时结束后进入“CSI 校准”阶段，校准时间可由变量 CALIB_TIME_SEC 设定
 *  5. 校准完成后自动：
 *     – 启动 Ping（或 NULL 数据）保持 CSI 更新
 *     – 启动雷达算法实时判断：房间有人/无人、静止/运动
 *     – 所有结果通过 ESP_LOGI 打印
 *
 *  代码在官方示例基础上裁剪、合并，保留全部 CLI 功能。
 */
#include "doit_csi.h"

static const char *TAG = "Doit_Csi";

/**
 * 准备扫描定时器回调
 */
static void pre_countdown_cb(TimerHandle_t t)
{
    ESP_LOGI(TAG, "准备校准倒计时 %d 秒...", g_pre_calib_countdown_left);
    if (--g_pre_calib_countdown_left == 0)
    {
        xTimerStop(g_pre_calib_countdown_timer, 0);
        // xTimerDelete(g_pre_calib_countdown_timer, 0);
        // g_pre_calib_countdown_timer = NULL;
        ESP_LOGI(TAG, "准备校准倒计时结束，开始 CSI 校准，持续 %d 秒...", CALIB_TIME_SEC);

        xTimerStart(g_pre_calib_countdown_timer, 0);
        /* 启动雷达训练（校准） */
        esp_radar_train_start();
    }
}

/**
 * 校准定时器回调
 */
static void calib_done_cb(TimerHandle_t t)
{
    ESP_LOGI(TAG, "校准完成倒计时 %d 秒...", g_start_calib_countdown_left);
    if (--g_start_calib_countdown_left == 0)
    {
        xTimerStop(g_start_calib_countdown_timer, 0);
        // xTimerDelete(g_start_calib_countdown_timer, 0);
        // g_start_calib_countdown_timer = NULL;
        ESP_LOGI(TAG, "倒计时结束，校准已完成，开始CSI检测...");

        /* 停止训练，获取阈值 */
        float someone_thr, move_thr;
        esp_radar_train_stop(&someone_thr, &move_thr);
        ESP_LOGI(TAG, "校准完成！有人阈值=%.6f，运动阈值=%.6f", someone_thr, move_thr);

        /* 启动 CSI 采集与雷达实时检测 */
        start_csi_radar();
    }
}

/**
 * ping触发路由发送任务
 */
static void trigger_router_send_data_task(void *arg)
{
    wifi_radar_config_t radar_config = {0};
    wifi_ap_record_t ap_info = {0};
    uint8_t sta_mac[6] = {0};

    esp_radar_get_config(&radar_config);
    esp_wifi_sta_get_ap_info(&ap_info);
    ESP_ERROR_CHECK(esp_wifi_get_mac(WIFI_IF_STA, sta_mac));

    // 打印 AP 信息
    ESP_LOGI(TAG, "AP Information:");
    ESP_LOGI(TAG, "SSID: %s", ap_info.ssid);
    ESP_LOGI(TAG, "BSSID: " MACSTR, MAC2STR(ap_info.bssid));
    ESP_LOGI(TAG, "RSSI: %d", ap_info.rssi);
    ESP_LOGI(TAG, "Channel: %d", ap_info.primary);
    ESP_LOGI(TAG, "Authmode: %d", ap_info.authmode);

    radar_config.csi_recv_interval = g_send_data_interval;
    memcpy(radar_config.filter_dmac, sta_mac, sizeof(radar_config.filter_dmac));

#if WIFI_CSI_SEND_NULL_DATA_ENABLE
    ESP_LOGI(TAG, "Send null data to router");

    memset(radar_config.filter_mac, 0, sizeof(radar_config.filter_mac));
    esp_radar_set_config(&radar_config);

    typedef struct
    {
        uint8_t frame_control[2];
        uint16_t duration;
        uint8_t destination_address[6];
        uint8_t source_address[6];
        uint8_t broadcast_address[6];
        uint16_t sequence_control;
    } __attribute__((packed)) wifi_null_data_t;

    wifi_null_data_t null_data = {
        .frame_control = {0x48, 0x01},
        .duration = 0x0000,
        .sequence_control = 0x0000,
    };

    memcpy(null_data.destination_address, ap_info.bssid, 6);
    memcpy(null_data.broadcast_address, ap_info.bssid, 6);
    memcpy(null_data.source_address, sta_mac, 6);

    ESP_LOGW(TAG, "null_data, destination_address: " MACSTR ", source_address: " MACSTR ", broadcast_address: " MACSTR,
             MAC2STR(null_data.destination_address), MAC2STR(null_data.source_address), MAC2STR(null_data.broadcast_address));

    ESP_ERROR_CHECK(esp_wifi_config_80211_tx_rate(WIFI_IF_STA, WIFI_PHY_RATE_6M));

    vTaskSuspend(NULL);
    while (true)
    {
        esp_err_t ret = esp_wifi_80211_tx(WIFI_IF_STA, &null_data, sizeof(wifi_null_data_t), true);
        if (ret != ESP_OK)
        {
            ESP_LOGW(TAG, "esp_wifi_80211_tx, %s", esp_err_to_name(ret));
            vTaskDelay(pdMS_TO_TICKS(1000));
        }

        vTaskDelay(pdMS_TO_TICKS(g_send_data_interval));
    }

#else
    ESP_LOGI(TAG, "Send ping data to router");

    memcpy(radar_config.filter_mac, ap_info.bssid, sizeof(radar_config.filter_mac));
    esp_radar_set_config(&radar_config);

    static esp_ping_handle_t ping_handle = NULL;
    esp_ping_config_t config = ESP_PING_DEFAULT_CONFIG();
    config.count = 0;
    config.data_size = 1;
    config.interval_ms = g_send_data_interval;

    /**
     * @brief Get the Router IP information from the esp-netif
     */
    esp_netif_ip_info_t local_ip;
    esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"), &local_ip);
    ESP_LOGI(TAG, "Ping: got ip:" IPSTR ", gw: " IPSTR, IP2STR(&local_ip.ip), IP2STR(&local_ip.gw));
    config.target_addr.u_addr.ip4.addr = ip4_addr_get_u32(&local_ip.gw);
    config.target_addr.type = ESP_IPADDR_TYPE_V4;

    esp_ping_callbacks_t cbs = {0};
    esp_ping_new_session(&config, &cbs, &ping_handle);
    esp_ping_start(ping_handle);
#endif

    vTaskDelete(NULL);
}

/**
 * CSI 数据打印任务
 */
static void csi_data_print_task(void *arg)
{
    wifi_csi_filtered_info_t *info = NULL;
    char *buffer = malloc(8 * 1024);
    static uint32_t count = 0;

    vTaskSuspend(NULL);
    while (xQueueReceive(g_csi_info_queue, &info, portMAX_DELAY))
    {
        size_t len = 0;
        wifi_pkt_rx_ctrl_t *rx_ctrl = &info->rx_ctrl;

        if (!count)
        {
            ESP_LOGI(TAG, "================ CSI RECV ================");
            len += sprintf(buffer + len, "type,sequence,timestamp,taget_seq,taget,mac,rssi,rate,sig_mode,mcs,bandwidth,smoothing,not_sounding,aggregation,stbc,fec_coding,sgi,noise_floor,ampdu_cnt,channel,secondary_channel,local_timestamp,ant,sig_len,rx_state,agc_gain,fft_gain,len,first_word,data\n");
        }

        if (!strcasecmp(g_console_input_config.csi_output_type, "LLFT"))
        {
            info->valid_len = info->valid_llft_len;
        }
        else if (!strcasecmp(g_console_input_config.csi_output_type, "HT-LFT"))
        {
            info->valid_len = info->valid_llft_len + info->valid_ht_lft_len;
        }
        else if (!strcasecmp(g_console_input_config.csi_output_type, "STBC-HT-LTF"))
        {
            info->valid_len = info->valid_llft_len + info->valid_ht_lft_len + info->valid_stbc_ht_lft_len;
        }

        len += sprintf(buffer + len, "CSI_DATA,%lu,%lu,%lu,%s," MACSTR ",%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%u,%d,%d,%d,%d,%d,%d,%d,",
                       count++, esp_log_timestamp(), g_console_input_config.collect_number, g_console_input_config.collect_taget,
                       MAC2STR(info->mac), rx_ctrl->rssi, rx_ctrl->rate, rx_ctrl->sig_mode,
                       rx_ctrl->mcs, rx_ctrl->cwb, rx_ctrl->smoothing, rx_ctrl->not_sounding,
                       rx_ctrl->aggregation, rx_ctrl->stbc, rx_ctrl->fec_coding, rx_ctrl->sgi,
                       rx_ctrl->noise_floor, rx_ctrl->ampdu_cnt, rx_ctrl->channel, rx_ctrl->secondary_channel,
                       rx_ctrl->timestamp, rx_ctrl->ant, rx_ctrl->sig_len, rx_ctrl->rx_state, info->agc_gain, info->fft_gain, info->valid_len, 0);

        if (!strcasecmp(g_console_input_config.csi_output_format, "base64"))
        {
            size_t size = 0;
            mbedtls_base64_encode((uint8_t *)buffer + len, sizeof(buffer) - len, &size, (uint8_t *)info->valid_data, info->valid_len);
            len += size;
            len += sprintf(buffer + len, "\n");
        }
        else
        {
            len += sprintf(buffer + len, "\"[%d", info->valid_data[0]);

            for (int i = 1; i < info->valid_len; i++)
            {
                len += sprintf(buffer + len, ",%d", info->valid_data[i]);
            }

            len += sprintf(buffer + len, "]\"\n");
        }

        printf("%s", buffer);
        free(info);
    }

    free(buffer);
    vTaskDelete(NULL);
}

static void wifi_radar_cb(const wifi_radar_info_t *info, void *ctx)
{
    ESP_LOGI(TAG, "qwe");
    static float *s_buff_wander = NULL;
    static float *s_buff_jitter = NULL;
    static uint32_t s_buff_count = 0;
    uint32_t buff_max_size = g_console_input_config.predict_buff_size;
    uint32_t buff_outliers_num = g_console_input_config.predict_outliers_number;
    uint32_t someone_count = 0;
    uint32_t move_count = 0;
    bool room_status = false;
    bool human_status = false;

    if (!s_buff_wander)
    {
        s_buff_wander = calloc(RADAR_BUFF_MAX_LEN, sizeof(float));
    }

    if (!s_buff_jitter)
    {
        s_buff_jitter = calloc(RADAR_BUFF_MAX_LEN, sizeof(float));
    }

    s_buff_wander[s_buff_count % RADAR_BUFF_MAX_LEN] = info->waveform_wander;
    s_buff_jitter[s_buff_count % RADAR_BUFF_MAX_LEN] = info->waveform_jitter;
    s_buff_count++;

    if (s_buff_count < buff_max_size)
    {
        return;
    }

    extern float trimmean(const float *array, size_t len, float percent);
    extern float median(const float *a, size_t len);

    float wander_average = trimmean(s_buff_wander, RADAR_BUFF_MAX_LEN, 0.5);
    float jitter_midean = median(s_buff_jitter, RADAR_BUFF_MAX_LEN);

    for (int i = 0; i < buff_max_size; i++)
    {
        uint32_t index = (s_buff_count - 1 - i) % RADAR_BUFF_MAX_LEN;

        if ((wander_average * g_console_input_config.predict_someone_sensitivity > g_console_input_config.predict_someone_threshold))
        {
            someone_count++;
        }

        if (s_buff_jitter[index] * g_console_input_config.predict_move_sensitivity > g_console_input_config.predict_move_threshold || (s_buff_jitter[index] * g_console_input_config.predict_move_sensitivity > jitter_midean && s_buff_jitter[index] > 0.0002))
        {
            move_count++;
        }
    }

    if (someone_count >= 1)
    {
        room_status = true;
    }

    if (move_count >= buff_outliers_num)
    {
        human_status = true;
    }

    static uint32_t s_count = 0;

    if (!s_count)
    {
        ESP_LOGI(TAG, "================ RADAR RECV ================");
        ESP_LOGI(TAG, "type,sequence,timestamp,waveform_wander,someone_threshold,someone_status,waveform_jitter,move_threshold,move_status");
    }

    char timestamp_str[32] = {0};
    sprintf(timestamp_str, "%lu", esp_log_timestamp());

    if (ctx)
    {
        strncpy(timestamp_str, (char *)ctx, 31);
    }

    static uint32_t s_last_move_time = 0;
    static uint32_t s_last_someone_time = 0;

    if (g_console_input_config.train_start)
    {
        s_last_move_time = esp_log_timestamp();
        s_last_someone_time = esp_log_timestamp();

        // static bool led_status = false;

        // if (led_status)
        // {
        //     ws2812_led_set_rgb(0, 0, 0);
        // }
        // else
        // {
        //     ws2812_led_set_rgb(255, 255, 0);
        // }

        // led_status = !led_status;

        return;
    }

    printf("RADAR_DADA,%lu,%s,%.6f,%.6f,%.6f,%d,%.6f,%.6f,%.6f,%d\n", s_count++, timestamp_str,
           info->waveform_wander, wander_average, g_console_input_config.predict_someone_threshold / g_console_input_config.predict_someone_sensitivity, room_status,
           info->waveform_jitter, jitter_midean, jitter_midean / g_console_input_config.predict_move_sensitivity, human_status);

    if (room_status)
    {
        if (human_status)
        {
            // ws2812_led_set_rgb(0, 255, 0);
            ESP_LOGI(TAG, "Someone moved");
            s_last_move_time = esp_log_timestamp();
        }
        else if (esp_log_timestamp() - s_last_move_time > 3 * 1000)
        {
            // ws2812_led_set_rgb(255, 255, 255);
            ESP_LOGI(TAG, "Someone static");
        }

        s_last_someone_time = esp_log_timestamp();
    }
    else if (esp_log_timestamp() - s_last_someone_time > 3 * 1000)
    {
        ESP_LOGI(TAG, "No People");
        if (human_status)
        {
            s_last_move_time = esp_log_timestamp();
            // ws2812_led_set_rgb(255, 0, 0);
        }
        else if (esp_log_timestamp() - s_last_move_time > 3 * 1000)
        {
            // ws2812_led_set_rgb(0, 0, 0);
        }
    }
}

/**
 * CSI初始化
 */
void doit_csi_init(void)
{
    /* ---------- 初始化雷达库 ---------- */
    esp_radar_init();
    wifi_radar_config_t radar_config = WIFI_RADAR_CONFIG_DEFAULT();
    radar_config.wifi_radar_cb = wifi_radar_cb;            /* 雷达结果回调 */
    radar_config.csi_recv_interval = g_send_data_interval; // 它决定了“每隔多少毫秒向路由器发一次数据包”，以便触发路由器回包，从而拿到一份新的 CSI 数据。

    // 当宏 WIFI_CSI_SEND_NULL_DATA_ENABLE 被定义成 1 时，才把 dump_ack_en 设为 true，从而让 Wi-Fi 驱动把 ACK（握手应答帧） 的 CSI 也一并采集并上报,普通数据帧（Ping、Null Data）已经能提供 CSI，但 ACK 帧是路由器→ESP32 的下行帧，也能携带 CSI。打开后 CSI 采样量翻倍（上行 + 下行），时间分辨率更高 → 适合做 高精度呼吸/微动检测。

#if WIFI_CSI_SEND_NULL_DATA_ENABLE
    radar_config.csi_config.dump_ack_en = true;
#endif

    // // 1.准备校准定时器
    // if (!g_pre_calib_countdown_timer)
    // {
    //     xTimerCreate("pre_calib_countdown", pdMS_TO_TICKS(1000), pdTRUE, NULL, pre_countdown_cb);
    // }

    // /* 2. 把校准定时器 */
    // if (!calib_done_cb)
    // {
    //     g_start_calib_countdown_timer = xTimerCreate("start_calib_countdown", pdMS_TO_TICKS(1000), pdTRUE, NULL, calib_done_cb);
    // }

    /* 创建 CSI 打印任务（官方实现，避免阻塞回调） */
    if (!g_csi_info_queue)
    {
        g_csi_info_queue = xQueueCreate(64, sizeof(void *));
    }

    // if (!csi_data_print_task_handler)
    // {
    //     xTaskCreate(csi_data_print_task, "csi_data_print", 4 * 1024, NULL, 0, &csi_data_print_task_handler);
    // }

    if (!trigger_router_send_data_task_handler)
    {
        xTaskCreate(trigger_router_send_data_task, "trigger_router_send_data", 4 * 1024, NULL, 5, &trigger_router_send_data_task_handler);
    }

    /* 只接收路由器 CSI，MAC 前缀随意填，后续会被替换 */
    memcpy(radar_config.filter_mac, "\x1a\x00\x00\x00\x00\x00", 6);
    esp_radar_set_config(&radar_config);
}

/* 启动雷达校准 */
void start_csi_calib(void)
{
    /* 启动雷达训练（校准） */
    esp_radar_train_start();
}

/* 关闭雷达校准 */
void stop_csi_calib(void)
{
    /* 停止训练，获取阈值 */
    float someone_thr, move_thr;
    esp_radar_train_stop(&someone_thr, &move_thr);
    ESP_LOGI(TAG, "校准完成！有人阈值=%.6f，运动阈值=%.6f", someone_thr, move_thr);
}

/* 启动 Ping/NULL 数据 + 雷达算法 */
void start_csi_radar(void)
{
    /* 启动雷达算法 */
    if (!GetIsCsiRadarRunning())
    {
        // vTaskResume(csi_data_print_task_handler);
        vTaskResume(trigger_router_send_data_task_handler);
        esp_radar_start();
        SetIsCsiRadarRunning(true);
        ESP_LOGI(TAG, "系统已就绪，实时检测中...");
    }
}

/* 关闭 Ping/NULL 数据 + 雷达算法 */
void stop_csi_radar(void)
{
    if (GetIsCsiRadarRunning())
    {
        /* 关闭雷达算法 */
        esp_radar_stop();
        // vTaskSuspend(csi_data_print_task_handler);
        vTaskSuspend(trigger_router_send_data_task_handler);
        SetIsCsiRadarRunning(false);
        ESP_LOGI(TAG, "系统已停止，实时检测已关闭");
    }
}

bool GetIsPreCalibRunning()
{
    return is_pre_calib_running;
}
bool GetIsStartCalibRunning()
{
    return is_start_calib_running;
}
bool GetIsCsiRadarRunning()
{
    return is_csi_radar_running;
}
void SetIsPreCalibRunning(bool value)
{
    is_pre_calib_running = value;
}
void SetIsStartCalibRunning(bool value)
{
    is_start_calib_running = value;
}
void SetIsCsiRadarRunning(bool value)
{
    is_csi_radar_running = value;
}