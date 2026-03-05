set(incs )
set(srcs )
set(priv_requires fatfs vfs wear_levelling esp_partition)
set(assets )
list(APPEND srcs
    ext_usb_disk_svc.c
        )

list(APPEND incs
        include
        )



idf_component_register(
    SRCS "ext_usb_disk_svc.c" "${srcs}"
    INCLUDE_DIRS "${incs}"
    PRIV_REQUIRES "${priv_requires}"
)