set(incs )
set(srcs )
set(priv_requires espressif__esp_new_jpeg lvgl__lvgl espressif__esp_lvgl_port dot_page_manager)
set(assets )
list(APPEND srcs
    ext_vpg_svc.c
    inc_decode.c
    inc_vpg_decode.c
    inc_img_decode.c
        )

list(APPEND incs
        include
        )


idf_component_register(
    SRCS "inc_decode.c" "inc_img_decode.c" "${srcs}"
    INCLUDE_DIRS "${incs}"
    EMBED_TXTFILES "${assets}"
    PRIV_REQUIRES "${priv_requires}"
)