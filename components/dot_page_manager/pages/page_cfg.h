#ifndef __PAGE_CFG_H__
#define __PAGE_CFG_H__

#include "page_manager.h"
#include "progress.h"

#ifdef __cplusplus
extern "C" {
#endif

const page_impl_t *page_home_impl(void);
const page_impl_t *page_progress_impl(void);
const page_impl_t *page_tip_u_disk_impl(void);
const page_impl_t *page_tip_no_psd_impl(void);
const page_impl_t *page_toast_impl(void);
const page_impl_t *page_tip_wifi_config_impl(void);
const page_impl_t *page_tip_del_impl(void);

/* 1. 页面 ID 枚举 */
typedef enum {
    PAGE_ID_NONE = -1,
    PAGE_ID_HOME,     /* 主页 */
    PAGE_ID_PROGRESS, /* 下载进度条页面 */
    PAGE_ID_TOAST,    /* 提示页面 */
    PAGE_ID_DEL,     /* 删除页面 */
    PAGE_ID_NOR_PSD,  /* 没有素材的界面提示 */
    PAGE_ID_U_DISK,   /* U盘模式的界面提示 */
    PAGE_ID_WIFI_CFG, /* wifi配置页面 */
    /* 新增页面继续往下写 */
    PAGE_ID_MAX
} page_id_t;

typedef const page_impl_t *(*page_impl_getter_t)(void); // 页面注册的函数指针

typedef struct {
    const char *name;
    page_impl_getter_t impl_getter;
} page_info_t;

/* 3. 统一访问接口 */
const page_info_t *page_info_get(page_id_t id);
void page_install(void);

/* 4. 宏：一键切换，不用手写字符串 */
#define PAGE_SWITCH(id, anim) \
    page_manager_switch_to(page_info_get(id)->name, anim)

#ifdef __cplusplus
}
#endif

#endif