#pragma once
#include <stdio.h>

/*  ========== 日志总开关 ============== */
#ifndef USE_LOG
#define USE_LOG 1
#endif


/* 二选一：0=printf（默认）；1=esp_log */
#ifndef USE_ESP_LOG
#define USE_ESP_LOG 1
#endif

/* 如果启用 esp_log，就包含 esp_log.h */
#if USE_ESP_LOG
#include "esp_log.h"
#endif

/* 日志标签 */
#ifndef LOG_TAG
#define LOG_TAG "doit_mp_psd"
#endif

/*  ========== 宏实现 ============== */

#if USE_LOG
    #if USE_ESP_LOG          /* ---------- 使用 esp_log  ---------- */

        #define MP_LOGE(fmt, ...) ESP_LOGE(LOG_TAG, fmt, ##__VA_ARGS__)
        #define MP_LOGW(fmt, ...) ESP_LOGW(LOG_TAG, fmt, ##__VA_ARGS__)
        #define MP_LOGI(fmt, ...) ESP_LOGI(LOG_TAG, fmt, ##__VA_ARGS__)
        #define MP_LOGD(fmt, ...) ESP_LOGD(LOG_TAG, fmt, ##__VA_ARGS__)
        #define MP_LOGV(fmt, ...) ESP_LOGV(LOG_TAG, fmt, ##__VA_ARGS__)

    #else                     /* ---------- 使用 printf  ---------- */

        #define _LOG_PREFIX(LEVEL) "[" LEVEL "][" LOG_TAG "] "
        #define MP_LOGI(fmt, ...) printf(_LOG_PREFIX("INFO")  fmt "\r\n", ##__VA_ARGS__)
        #define MP_LOGE(fmt, ...) printf(_LOG_PREFIX("ERROR") fmt "\r\n", ##__VA_ARGS__)
        #define MP_LOGW(fmt, ...) printf(_LOG_PREFIX("WARN")  fmt "\r\n", ##__VA_ARGS__)
        #define MP_LOGD(fmt, ...) printf(_LOG_PREFIX("Debug") fmt "\r\n", ##__VA_ARGS__)
        #define MP_LOGV(fmt, ...) printf(_LOG_PREFIX("Verbose")fmt "\r\n", ##__VA_ARGS__)

    #endif

#else
    #define MP_LOGI(...)
    #define MP_LOGE(...)
    #define MP_LOGW(...)
    #define MP_LOGI(...)
    #define MP_LOGD(...)
    #define MP_LOGV(...)

#endif  /* USE_ESP_LOG */
/*  ======================================= */