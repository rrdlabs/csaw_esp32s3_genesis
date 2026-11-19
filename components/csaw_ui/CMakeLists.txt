idf_component_register(
    SRCS "csaw_ui.c"
    INCLUDE_DIRS "include"
    REQUIRES lvgl csaw_hal esp_lcd esp_timer esp_driver_i2c
    PRIV_REQUIRES log
)
