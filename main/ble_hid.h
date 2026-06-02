#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Mouse motion step types */
#define MOUSE_STEP_MOVE    0  // Relative move (dx, dy)
#define MOUSE_STEP_SCROLL  1  // Wheel scroll
#define MOUSE_STEP_RESET   2  // Send zero report
#define MOUSE_STEP_DELAY   3  // Wait milliseconds

/** A single step in a mouse motion pipeline */
typedef struct {
    uint8_t  type;          // MOUSE_STEP_xxx
    int8_t   dx;            // Relative X movement
    int8_t   dy;            // Relative Y movement
    int8_t   wheel;         // Wheel delta
    uint8_t  repeat;        // Repeat count
    uint32_t delay_ms;      // Delay AFTER the step(s)
} mouse_step_t;

esp_err_t ble_hid_init(void);
bool ble_hid_is_connected(void);
const char *ble_hid_get_connected_addr(void);
esp_err_t ble_hid_send_key(uint8_t keycode);

/** Execute a mouse motion pipeline defined by steps array */
esp_err_t ble_hid_mouse_exec(const mouse_step_t *steps, size_t count);

/** Convenience: vertical scroll with center-positioning preamble */
esp_err_t ble_hid_drag_vertical(bool upward);

#ifdef __cplusplus
}
#endif
