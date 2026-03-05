set(incs )
set(srcs )
set(priv_requires nvs_flash esp_netif esp_event esp-tls esp_http_client dot_vpg dot_fs_fat)
set(assets )
list(APPEND srcs
    ext_http_download.c
        )

list(APPEND incs
        include
        )


idf_component_register(
    SRCS "ext_http_download.c" "${srcs}"
    INCLUDE_DIRS "${incs}"
    EMBED_TXTFILES "${assets}"
    PRIV_REQUIRES "${priv_requires}"
)