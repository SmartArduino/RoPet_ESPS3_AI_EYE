set(incs )
set(srcs )
set(assets )
set(priv_requires lvgl__lvgl espressif__esp_lvgl_port esp_event)

file(GLOB IMG_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/assets/icon/*.c)

list(APPEND srcs
    manager/page_manager.c
    pages/page_cfg.c
    utils/anim_utils.c
    pages/page_home/home.c
    pages/page_progress/progress.c
    pages/page_toast/toast.c
    pages/page_tip/tip_on_u_disk.c
    pages/page_tip/tip_no_psd.c
    pages/page_tip/tip_wifi_config.c
    pages/page_tip/tip_del.c
    ${IMG_SOURCES}

        )

list(APPEND incs
        include
        manager
        pages
        pages/page_home
        pages/page_progress
        pages/page_toast
        utils
        )


idf_component_register(
    SRCS "ext_mp_ui_svc.c" "${srcs}"
    INCLUDE_DIRS "${incs}"
    EMBED_TXTFILES "${assets}"
    PRIV_REQUIRES "${priv_requires}"
)