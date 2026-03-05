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
#define LOG_TAG "doit_tinyusb"
#endif

/*  ========== 宏实现 ============== */

#if USE_LOG
#if USE_ESP_LOG /* ---------- 使用 esp_log  ---------- */

#define DUD_LOGE(fmt, ...) ESP_LOGE(LOG_TAG, "[%s:%d] " fmt, __FILE__, __LINE__, ##__VA_ARGS__)
#define DUD_LOGW(fmt, ...) ESP_LOGW(LOG_TAG, "[%s:%d] " fmt, __FILE__, __LINE__, ##__VA_ARGS__)
#define DUD_LOGI(fmt, ...) ESP_LOGI(LOG_TAG, "[%s:%d] " fmt, __FILE__, __LINE__, ##__VA_ARGS__)
#define DUD_LOGD(fmt, ...) ESP_LOGD(LOG_TAG, "[%s:%d] " fmt, __FILE__, __LINE__, ##__VA_ARGS__)
#define DUD_LOGV(fmt, ...) ESP_LOGV(LOG_TAG, "[%s:%d] " fmt, __FILE__, __LINE__, ##__VA_ARGS__)

#else /* ---------- 使用 printf  ---------- */

#define _LOG_PREFIX(LEVEL) "[" LEVEL "][" LOG_TAG "] [%s:%d] "
#define DUD_LOGI(fmt, ...) printf(_LOG_PREFIX("INFO") __FILE__, __LINE__, fmt "\r\n", ##__VA_ARGS__)
#define DUD_LOGE(fmt, ...) printf(_LOG_PREFIX("ERROR") __FILE__, __LINE__, fmt "\r\n", ##__VA_ARGS__)
#define DUD_LOGW(fmt, ...) printf(_LOG_PREFIX("WARN") __FILE__, __LINE__, fmt "\r\n", ##__VA_ARGS__)
#define DUD_LOGD(fmt, ...) printf(_LOG_PREFIX("DEBUG") __FILE__, __LINE__, fmt "\r\n", ##__VA_ARGS__)
#define DUD_LOGV(fmt, ...) printf(_LOG_PREFIX("VERBOSE") __FILE__, __LINE__, fmt "\r\n", ##__VA_ARGS__)

#endif

#else
#define DUD_LOGI(...)
#define DUD_LOGE(...)

#define DUD_LOGW(...)
#define DUD_LOGD(...)
#define DUD_LOGV(...)

#endif /* USE_ESP_LOG */

/*  ======================================= */
