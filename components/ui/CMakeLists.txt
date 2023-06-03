SET(SOURCES screens/ui_Screen1.c
    ui.c
    ui_helpers.c
    images/ui_img_aa_2_png.c)


idf_component_register(SRCS ${SOURCES}
                       INCLUDE_DIRS "."
                       REQUIRES lvgl)