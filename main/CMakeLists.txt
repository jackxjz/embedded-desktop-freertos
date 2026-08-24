file(GLOB_RECURSE srcs *.c
                    lvgl-file/*.c
                    lvgl-file/components/*.c
                    lvgl-file/fonts/*.c
                    lvgl-file/images/*.c
                    lvgl-file/screens/*.c
                    )


set(include_dirs 
    . 
    lvgl-file
    )

idf_component_register( SRCS ${srcs}
                        INCLUDE_DIRS ${include_dirs}
                        )

idf_component_get_property(lvgl_lib lvgl__lvgl COMPONENT_LIB)
target_compile_options(${lvgl_lib} PRIVATE -Wno-format)
