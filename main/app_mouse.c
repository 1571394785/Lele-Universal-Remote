#include "app_mouse.h"

#include <limits.h>

#include "ble_hid.h"
#include "buttons.h"

#define MOUSE_DIRECTION_MASK (BUTTON_MASK_UP | BUTTON_MASK_DOWN | BUTTON_MASK_LEFT | BUTTON_MASK_RIGHT)
#define MOUSE_BUTTON_MASK    (BUTTON_MASK_FUNC3 | BUTTON_MASK_FUNC4)
#define MOUSE_CONTROL_MASK   (MOUSE_DIRECTION_MASK | MOUSE_BUTTON_MASK)
#define MOUSE_REPORT_PERIOD_MS 20

static uint8_t mouse_buttons_from_mask(uint16_t mask)
{
    uint8_t buttons = 0;
    if (mask & BUTTON_MASK_FUNC4) buttons |= 0x01; // Left button
    if (mask & BUTTON_MASK_FUNC3) buttons |= 0x02; // Right button
    return buttons;
}

void app_mouse_state_init(app_mouse_state_t *state)
{
    if (state == NULL) return;

    state->last_control_mask = UINT16_MAX;
    state->direction_mask = 0;
    state->direction_start_ms = 0;
    state->last_move_ms = 0;
    state->active = false;
    state->suppress_until_release = false;
}

void app_mouse_enter(app_mouse_state_t *state, uint16_t initial_mask)
{
    if (state == NULL) return;

    state->last_control_mask = initial_mask & MOUSE_CONTROL_MASK;
    state->direction_mask = 0;
    state->direction_start_ms = 0;
    state->last_move_ms = 0;
    state->active = true;
    state->suppress_until_release = initial_mask != 0;
}

esp_err_t app_mouse_leave(app_mouse_state_t *state)
{
    if (state == NULL || !state->active) return ESP_OK;

    state->active = false;
    state->last_control_mask = UINT16_MAX;
    state->direction_mask = 0;
    return ble_hid_send_mouse_report(0, 0, 0, 0);
}

esp_err_t app_mouse_handle(app_mouse_state_t *state, uint16_t button_mask, uint32_t now_ms)
{
    if (state == NULL || !state->active) return ESP_ERR_INVALID_STATE;

    uint16_t control_mask = button_mask & MOUSE_CONTROL_MASK;
    if (state->suppress_until_release) {
        if (control_mask == 0) {
            state->suppress_until_release = false;
            state->last_control_mask = 0;
        }
        return ESP_OK;
    }

    uint16_t directions = control_mask & MOUSE_DIRECTION_MASK;
    if (directions != state->direction_mask) {
        state->direction_mask = directions;
        state->direction_start_ms = now_ms;
        state->last_move_ms = 0;
    }

    bool control_changed = control_mask != state->last_control_mask;
    bool move_due = directions != 0 &&
                    (state->last_move_ms == 0 || now_ms - state->last_move_ms >= MOUSE_REPORT_PERIOD_MS);
    if (!control_changed && !move_due) return ESP_OK;

    int8_t dx = 0;
    int8_t dy = 0;
    if (directions != 0) {
        uint32_t held_ms = now_ms - state->direction_start_ms;
        uint32_t speed_value = 2 + held_ms / 180;
        if (speed_value > 18) speed_value = 18;
        int8_t speed = (int8_t)speed_value;

        if (directions & BUTTON_MASK_LEFT) dx -= speed;
        if (directions & BUTTON_MASK_RIGHT) dx += speed;
        if (directions & BUTTON_MASK_UP) dy -= speed;
        if (directions & BUTTON_MASK_DOWN) dy += speed;
        state->last_move_ms = now_ms;
    }

    state->last_control_mask = control_mask;
    return ble_hid_send_mouse_report(mouse_buttons_from_mask(control_mask), dx, dy, 0);
}
