#include "mode_manager.h"

#define MENU_HOLD_MS 3000

typedef enum {
    APP_MODE_KEYBOARD_UP_DOWN = 0,
    APP_MODE_ABS_MOUSE_DRAG,
} app_mode_t;

static app_mode_t s_mode;
static app_screen_t s_screen;
static button_key_t s_last_key;
static uint32_t s_func1_press_started_ms;
static bool s_menu_entered_for_press;
static const char *s_last_action;

static const char *mode_name(app_mode_t mode)
{
    switch (mode) {
    case APP_MODE_KEYBOARD_UP_DOWN:
        return "模式一";
    case APP_MODE_ABS_MOUSE_DRAG:
        return "模式二";
    default:
        return "未知";
    }
}

static const char *mode_action_text(app_mode_t mode, button_key_t key)
{
    switch (mode) {
    case APP_MODE_KEYBOARD_UP_DOWN:
        switch (key) {
        case BUTTON_KEY_UP:
            return "方向上";
        case BUTTON_KEY_DOWN:
            return "方向下";
        case BUTTON_KEY_NONE:
            return "待机";
        default:
            return "空";
        }
    case APP_MODE_ABS_MOUSE_DRAG:
        switch (key) {
        case BUTTON_KEY_UP:
            return "上拖动";
        case BUTTON_KEY_DOWN:
            return "下拖动";
        case BUTTON_KEY_NONE:
            return "待机";
        default:
            return "空";
        }
    default:
        return "空";
    }
}

static mode_action_t mode_action(app_mode_t mode, button_key_t key)
{
    mode_action_t action = {
        .type = MODE_ACTION_NONE,
        .value = 0,
    };

    switch (mode) {
    case APP_MODE_KEYBOARD_UP_DOWN:
        if (key == BUTTON_KEY_UP) {
            action.type = MODE_ACTION_KEYBOARD_KEY;
            action.value = 0x52;
        } else if (key == BUTTON_KEY_DOWN) {
            action.type = MODE_ACTION_KEYBOARD_KEY;
            action.value = 0x51;
        }
        break;

    case APP_MODE_ABS_MOUSE_DRAG:
        if (key == BUTTON_KEY_UP) {
            action.type = MODE_ACTION_ABS_MOUSE_DRAG;
            action.value = 1;
        } else if (key == BUTTON_KEY_DOWN) {
            action.type = MODE_ACTION_ABS_MOUSE_DRAG;
            action.value = -1;
        }
        break;

    default:
        break;
    }

    return action;
}

static void fill_view(mode_view_t *view)
{
    if (view == NULL) {
        return;
    }

    if (s_screen == APP_SCREEN_MENU) {
        view->screen = APP_SCREEN_MENU;
        view->title = "菜单";
        view->line1 = mode_name(s_mode);
        view->line2 = "功能一退出";
        return;
    }

    view->screen = APP_SCREEN_MODE;
    view->title = mode_name(s_mode);
    view->line1 = s_last_action;
    view->line2 = "功能一进菜单";
}

void mode_manager_init(void)
{
    s_mode = APP_MODE_KEYBOARD_UP_DOWN;
    s_screen = APP_SCREEN_MODE;
    s_last_key = BUTTON_KEY_NONE;
    s_func1_press_started_ms = 0;
    s_menu_entered_for_press = false;
    s_last_action = "待机";
}

bool mode_manager_update(button_key_t key, uint32_t now_ms, mode_view_t *view, mode_action_t *action)
{
    bool changed = key != s_last_key;

    if (action != NULL) {
        action->type = MODE_ACTION_NONE;
        action->value = 0;
    }

    if (key == BUTTON_KEY_FUNC1) {
        if (s_last_key != BUTTON_KEY_FUNC1) {
            s_func1_press_started_ms = now_ms;
            s_menu_entered_for_press = false;
        }

        if (!s_menu_entered_for_press && now_ms - s_func1_press_started_ms >= MENU_HOLD_MS) {
            s_screen = s_screen == APP_SCREEN_MENU ? APP_SCREEN_MODE : APP_SCREEN_MENU;
            s_menu_entered_for_press = true;
            changed = true;
        }
    } else {
        s_func1_press_started_ms = 0;
        s_menu_entered_for_press = false;
    }

    if (s_screen == APP_SCREEN_MENU && key != s_last_key) {
        if (key == BUTTON_KEY_UP) {
            s_mode = APP_MODE_KEYBOARD_UP_DOWN;
            changed = true;
        } else if (key == BUTTON_KEY_DOWN) {
            s_mode = APP_MODE_ABS_MOUSE_DRAG;
            changed = true;
        }
    }

    if (s_screen == APP_SCREEN_MODE && key != BUTTON_KEY_FUNC1 && key != s_last_key) {
        s_last_action = mode_action_text(s_mode, key);
        if (action != NULL) {
            *action = mode_action(s_mode, key);
        }
    }

    s_last_key = key;
    fill_view(view);
    return changed;
}
