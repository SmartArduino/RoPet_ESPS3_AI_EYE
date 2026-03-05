set(incs )
set(srcs )
set(priv_requires fatfs vfs wear_levelling esp_partition dot_vpg)
set(assets )
list(APPEND srcs
    ext_fat_svc.c
        )

list(APPEND incs
        include
        )


idf_component_register(
    SRCS "${srcs}"
    INCLUDE_DIRS "${incs}"
    EMBED_TXTFILES "${assets}"
    PRIV_REQUIRES "${priv_requires}"
)