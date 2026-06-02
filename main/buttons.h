#pragma once

#include "esp_err.h"

typedef enum {
    BUTTON_KEY_NONE = -1,
    BUTTON_KEY_UP = 0,
    BUTTON_KEY_DOWN,
    BUTTON_KEY_LEFT,
    BUTTON_KEY_RIGHT,
    BUTTON_KEY_FUNC1,
    BUTTON_KEY_FUNC2,
    BUTTON_KEY_FUNC3,
    BUTTON_KEY_FUNC4,
    BUTTON_KEY_COUNT,
} button_key_t;

esp_err_t buttons_init(void);
button_key_t buttons_poll(void);
const char *buttons_key_name_cn(button_key_t key);
