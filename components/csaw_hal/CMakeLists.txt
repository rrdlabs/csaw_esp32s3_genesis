idf_component_register(
    SRCS "csaw_hal.c"
    INCLUDE_DIRS "include"
    REQUIRES
        driver esp_driver_gpio esp_driver_i2c esp_driver_spi esp_driver_sdmmc
        esp_lcd esp_timer esp_driver_i2s
    PRIV_REQUIRES fatfs vfs esp_psram log
)
