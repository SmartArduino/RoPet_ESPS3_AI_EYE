#ifndef __DOIT_CSI_H__
#define __DOIT_CSI_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/timers.h"

#include "esp_mac.h"
#include "esp_log.h"
#include "esp_console.h"
#include "esp_ping.h"
#include "mbedtls/base64.h"
#include "esp_radar.h"

#define CONFIG_SEND_DATA_FREQUENCY 100 /* CSI 发送频率，单位 Hz */
#define RADAR_BUFF_MAX_LEN 25
#define PRE_CALIB_TIME_SEC 10 /* 准备校准倒计时秒数 */
#define CALIB_TIME_SEC 15     /* 校准持续秒数 */

    static bool is_pre_calib_running = false;
    static bool is_start_calib_running = false;
    static bool is_csi_radar_running = false;

    static uint8_t g_pre_calib_countdown_left = PRE_CALIB_TIME_SEC;
    static uint8_t g_start_calib_countdown_left = CALIB_TIME_SEC;
    static uint32_t g_send_data_interval = 1000 / CONFIG_SEND_DATA_FREQUENCY; /* Ping/NULL 包间隔 */
    static TimerHandle_t g_pre_calib_countdown_timer = NULL;                  /* 准备校准定时器*/
    static TimerHandle_t g_start_calib_countdown_timer = NULL;                /* 校准定时器*/
    static QueueHandle_t g_csi_info_queue = NULL;                             /* CSI 信息队列 */
    static TaskHandle_t csi_data_print_task_handler = NULL;                   /* CSI 数据打印任务句柄 */
    static TaskHandle_t trigger_router_send_data_task_handler = NULL;         /* 触发路由器发送数据任务句柄 */
    // static struct
    // {
    //     struct arg_lit *train_start;
    //     struct arg_lit *train_stop;
    //     struct arg_lit *train_add;
    //     struct arg_str *predict_someone_threshold;
    //     struct arg_str *predict_someone_sensitivity;
    //     struct arg_str *predict_move_threshold;
    //     struct arg_str *predict_move_sensitivity;
    //     struct arg_int *predict_buff_size;
    //     struct arg_int *predict_outliers_number;
    //     struct arg_str *collect_taget;
    //     struct arg_int *collect_number;
    //     struct arg_int *collect_duration;
    //     struct arg_lit *csi_start;
    //     struct arg_lit *csi_stop;
    //     struct arg_str *csi_output_type;
    //     struct arg_str *csi_output_format;
    //     struct arg_int *csi_scale_shift;
    //     struct arg_int *channel_filter;
    //     struct arg_int *send_data_interval;
    //     struct arg_end *end;
    // } radar_args;
    static struct console_input_config
    {
        bool train_start;
        float predict_someone_threshold;
        float predict_someone_sensitivity;
        float predict_move_threshold;
        float predict_move_sensitivity;
        uint32_t predict_buff_size;
        uint32_t predict_outliers_number;
        char collect_taget[16];
        uint32_t collect_number;
        char csi_output_type[16];
        char csi_output_format[16];
    } g_console_input_config = {
        .train_start = false,
        .predict_someone_threshold = 0,
        .predict_someone_sensitivity = 0.15,
        .predict_move_threshold = 0.01,
        .predict_move_sensitivity = 0.20,
        .predict_buff_size = 5,
        .predict_outliers_number = 2,
        .collect_taget = "unknown",
        .csi_output_type = "LLFT",
        .csi_output_format = "decimal"};

    /* ---------- 函数声明 ---------- */

    static void pre_countdown_cb(TimerHandle_t t);
    static void calib_done_cb(TimerHandle_t t);
    static void wifi_radar_cb(const wifi_radar_info_t *info, void *ctx);
    static void trigger_router_send_data_task(void *arg);
    void doit_csi_init(void);
    void start_csi_radar(void);
    void stop_csi_radar(void);
    void start_csi_calib(void);
    void stop_csi_calib(void);

    bool GetIsPreCalibRunning();
    bool GetIsStartCalibRunning();
    bool GetIsCsiRadarRunning();
    void SetIsPreCalibRunning(bool value);
    void SetIsStartCalibRunning(bool value);
    void SetIsCsiRadarRunning(bool value);

#ifdef __cplusplus
}
#endif

#endif