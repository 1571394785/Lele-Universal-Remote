#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

esp_err_t ble_hid_init(void);
bool ble_hid_is_connected(void);
const char *ble_hid_get_connected_addr(void);
esp_err_t ble_hid_send_key(uint8_t keycode);
esp_err_t ble_hid_drag_vertical(bool upward);
