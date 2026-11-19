# cJSON vendored for ESP-IDF 6.1 (the in-tree `json` component was removed).
# Source: DaveGamble/cJSON v1.7.19 (MIT). Recreated per operator directive B2.
idf_component_register(
    SRCS "cJSON.c"
    INCLUDE_DIRS "."
)
