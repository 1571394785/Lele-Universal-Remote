idf_component_register(SRCS "main.c" "ssd1306.c" "buttons.c" "mode_manager.c" "ble_hid.c"
                    INCLUDE_DIRS "."
                    REQUIRES bt esp_driver_gpio esp_driver_i2c esp_event esp_hid nvs_flash
                    EMBED_FILES "assets/gb2312_16x16.bin" "assets/gb2312_unicode_map.bin")
