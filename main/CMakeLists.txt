idf_component_register(SRCS "app_display.c" "app_gamepad.c" "web_control.c" "custom_mode.c" "main.c" "ssd1306.c" "buttons.c" "mode_manager.c" "ble_hid.c" "battery_adc.c"
                    INCLUDE_DIRS "."
                    REQUIRES bt esp_adc esp_driver_gpio esp_driver_i2c esp_event esp_hid esp_http_server esp_netif esp_wifi lwip nvs_flash
                    EMBED_FILES "assets/gb2312_16x16.bin" "assets/gb2312_unicode_map.bin")
