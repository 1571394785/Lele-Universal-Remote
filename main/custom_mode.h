#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "buttons.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t modifier;
    uint8_t keycode;
} custom_key_combo_t;

#define CUSTOM_SEQUENCE_MAX 16

esp_err_t custom_mode_init(void);
esp_err_t custom_mode_set_shortcut(button_key_t key, const char *shortcut);
esp_err_t custom_mode_clear_all(void);
const char *custom_mode_get_shortcut(button_key_t key);
bool custom_mode_get_combo(button_key_t key, custom_key_combo_t *combo);
bool custom_mode_get_sequence(button_key_t key, custom_key_combo_t *sequence, size_t max_count, size_t *count);

#ifdef __cplusplus
}
#endif
