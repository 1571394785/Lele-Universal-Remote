#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    uint16_t last_control_mask;
    uint16_t direction_mask;
    uint32_t direction_start_ms;
    uint32_t last_move_ms;
    bool active;
    bool suppress_until_release;
} app_mouse_state_t;

void app_mouse_state_init(app_mouse_state_t *state);
void app_mouse_enter(app_mouse_state_t *state, uint16_t initial_mask);
esp_err_t app_mouse_leave(app_mouse_state_t *state);
esp_err_t app_mouse_handle(app_mouse_state_t *state, uint16_t button_mask, uint32_t now_ms);
