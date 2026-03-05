#include "page_cfg.h"

static const page_info_t s_page_info_tbl[PAGE_ID_MAX] = {
    [PAGE_ID_HOME] = {
        .name = "home",
        .impl_getter = page_home_impl,
    },
    [PAGE_ID_PROGRESS] = {
        .name = "progress",
        .impl_getter = page_progress_impl,
    },
    [PAGE_ID_WIFI_CFG] = {
        .name = "wifi_cfg",
        .impl_getter = page_tip_wifi_config_impl,
    },
    [PAGE_ID_NOR_PSD] = {
        .name = "tip_no_psd",
        .impl_getter = page_tip_no_psd_impl,
    },
    [PAGE_ID_U_DISK] = {
        .name = "tip_usb",
        .impl_getter = page_tip_u_disk_impl,
    },
    [PAGE_ID_DEL] = {
        .name = "tip_del",
        .impl_getter = page_tip_del_impl,
    },
    [PAGE_ID_TOAST] = {
        .name = "toast",
        .impl_getter = page_toast_impl,
    }
    // [PAGE_ID_PROGRESS] = {.name = "page2"},
    // [PAGE_ID_TOAST] = {.name = "page3"},
};

const page_info_t *page_info_get(page_id_t id) {
    if (id < 0 || id >= PAGE_ID_MAX) return NULL;
    return &s_page_info_tbl[id];
}

/* 4. 统一注册所有页面 —— 唯一对外接口 */
void page_install(void) {
    for (int i = 0; i < PAGE_ID_MAX; ++i) {
        const page_info_t *info = page_info_get((page_id_t)i);
        if (!info || !info->name) continue;

        if (!info->impl_getter) continue; // 没实现就跳过

        const page_impl_t *impl = info->impl_getter();
        if (!impl) continue;

        page_manager_register_impl(info->name, impl);
    }
}