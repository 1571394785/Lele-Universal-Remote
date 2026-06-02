#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "buttons.h"

#define MODE_COUNT 3

typedef enum {
    APP_SCREEN_MODE = 0,
    APP_SCREEN_MENU,
} app_screen_t;

typedef struct {
    app_screen_t screen;
    const char *title;
    const char *line1;
    const char *line2;
    uint8_t selected;           // current selection in menu
    uint8_t item_count;         // number of menu items
    const char *items[MODE_COUNT]; // menu item names
} mode_view_t;

typedef enum {
    MODE_ACTION_NONE = 0,
    MODE_ACTION_KEYBOARD_KEY,
    MODE_ACTION_ABS_MOUSE_DRAG,
    MODE_ACTION_MEDIA,
} mode_action_type_t;

typedef struct {
    mode_action_type_t type;
    int8_t value;
} mode_action_t;

void mode_manager_init(void);
bool mode_manager_update(button_key_t key, uint32_t now_ms, mode_view_t *view, mode_action_t *action);
