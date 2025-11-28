set(incs )
set(srcs )
set(assets )
list(APPEND srcs
       file/doit_decode.c
       file/doit_file.c
       file/doit_ui.c
       file/file_download.c
       file/vpg_decode.c
       file/img_decode.c
       ble/doit_ble.c
       ble/gap.c
       ble/gatt_svc.c
       doit_nvs.c
        )

list(APPEND incs
        include/file
        include/ble
        )

# littlefs 镜像的源目录
if(CONFIG_SUPPORT_MINI_PROGRAMS_REPLACE_PSD)
        if(CONFIG_USE_PSD_ONE)
                if(CONFIG_LCD_ST77916_360X360) 
                littlefs_create_partition_image(storage ./assets/360/unifile FLASH_IN_PROJECT)
                elseif(CONFIG_LCD_ST7796_240X240) 
                littlefs_create_partition_image(storage ./assets/240/unifile FLASH_IN_PROJECT)
                elseif(CONFIG_LCD_GC9A01_240X240) 
                littlefs_create_partition_image(storage ./assets/240/unifile FLASH_IN_PROJECT)
                elseif(CONFIG_LCD_GC9A01_160X160)
                littlefs_create_partition_image(storage ./assets/160/unifile FLASH_IN_PROJECT)
                endif()
        elseif(CONFIG_USE_PSD_MULTIPLE)
                 if(CONFIG_LCD_ST77916_360X360) 
                littlefs_create_partition_image(storage ./assets/360/multifile FLASH_IN_PROJECT)
                elseif(CONFIG_LCD_ST7796_240X240) 
                littlefs_create_partition_image(storage ./assets/240/multifile FLASH_IN_PROJECT)
                elseif(CONFIG_LCD_GC9A01_240X240) 
                littlefs_create_partition_image(storage ./assets/240/multifile FLASH_IN_PROJECT)
                elseif(CONFIG_LCD_GC9A01_160X160)
                littlefs_create_partition_image(storage ./assets/160/multifile FLASH_IN_PROJECT)
                endif()
        endif()
endif()



idf_component_register(SRCS  ${srcs}
                            INCLUDE_DIRS ${incs}
                            EMBED_TXTFILES ${assets}
                            REQUIRES driver esp_netif esp_event esp-tls esp_http_client spiffs nvs_flash espressif__esp_new_jpeg lvgl__lvgl espressif__esp_lvgl_port bt json)
