set(incs )
set(srcs )
set(priv_requires nvs_flash bt driver json dot_http_download)
set(assets )
list(APPEND srcs
    ext_nimble_svc.c
    inc_gap.c
    inc_gatt_svc.c
        )

list(APPEND incs
        include
        )


idf_component_register(
    SRCS "inc_gap.c" "ext_nimble_svc.c" "inc_custom_svc.c" "${srcs}"
    INCLUDE_DIRS "${incs}"
    EMBED_TXTFILES "${assets}"
    PRIV_REQUIRES "${priv_requires}"
)