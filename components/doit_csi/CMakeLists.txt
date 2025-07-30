set(incs )
set(srcs )
set(assets )
list(APPEND srcs
        doit_csi.c
        )

list(APPEND incs
        .
        )



idf_component_register(SRCS "doit_csi.c" "${srcs}"
                            INCLUDE_DIRS "${incs}"
                            EMBED_TXTFILES "${assets}"
                            REQUIRES driver esp-radar mbedtls console esp_wifi)

