set(incs )
set(srcs )
set(assets )
list(APPEND srcs
        doit_csi.c
        )

list(APPEND incs
        .
        )



idf_component_register(SRCS "${srcs}"
                            INCLUDE_DIRS "${incs}"
                            EMBED_TXTFILES "${assets}"
                            REQUIRES driver)

