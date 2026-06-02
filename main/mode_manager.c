#include "mode_manager.h"

typedef enum {
    APP_MODE_DOUYIN_PC = 0,
    APP_MODE_DOUYIN_IOS,
    APP_MODE_MEDIA_CTRL,
} app_mode_t;

static const char * const MODE_NAMES[MODE_COUNT] = {
    "抖音电脑版",
    "抖音IOS",
    "多媒体控制",
};

static const char * const MODE_ACTIONS[MODE_COUNT][4] = {
    [APP_MODE_DOUYIN_PC]  = {"方向上", "方向下", "待机", "空"},
    [APP_MODE_DOUYIN_IOS] = {"上拖动", "下拖动", "待机", "空"},
    [APP_MODE_MEDIA_CTRL] = {"音量+", "音量-", "待机", "空"},
};

static app_mode_t s_mode;
static app_screen_t s_screen;
static button_key_t s_last_key;
static uint8_t s_menu_sel;
static const char *s_last_action;

static const char *action_text(app_mode_t mode, button_key_t key)
{
    int idx;
    switch (key) {
    case BUTTON_KEY_UP:    idx = 0; break;
    case BUTTON_KEY_DOWN:  idx = 1; break;
    case BUTTON_KEY_NONE:  idx = 2; break;
    default:               idx = 3; break;
    }
    return MODE_ACTIONS[mode][idx];
}

static mode_action_t make_action(app_mode_t mode, button_key_t key)
{
    mode_action_t a = {MODE_ACTION_NONE, 0};

    if (key == BUTTON_KEY_NONE || key == BUTTON_KEY_FUNC1 || key == BUTTON_KEY_FUNC2) {
        return a;
    }

    switch (mode) {
    case APP_MODE_DOUYIN_PC:
        if (key == BUTTON_KEY_UP) {
            a.type = MODE_ACTION_KEYBOARD_KEY; a.value = 0x52;
        } else if (key == BUTTON_KEY_DOWN) {
            a.type = MODE_ACTION_KEYBOARD_KEY; a.value = 0x51;
        }
        break;

    case APP_MODE_DOUYIN_IOS:
        if (key == BUTTON_KEY_UP) {
            a.type = MODE_ACTION_ABS_MOUSE_DRAG; a.value = 1;
        } else if (key == BUTTON_KEY_DOWN) {
            a.type = MODE_ACTION_ABS_MOUSE_DRAG; a.value = -1;
        }
        break;

    case APP_MODE_MEDIA_CTRL:
        if (key == BUTTON_KEY_UP || key == BUTTON_KEY_DOWN) {
            a.type = MODE_ACTION_MEDIA;
        }
        break;

    default:
        break;
    }
    return a;
}

static void fill_view(mode_view_t *view)
{
    if (view == NULL) return;

    if (s_screen == APP_SCREEN_MENU) {
        view->screen = APP_SCREEN_MENU;
        view->title = "选择模式";
        view->line1 = NULL;
        view->line2 = NULL;
        view->selected = s_menu_sel;
        view->item_count = MODE_COUNT;
        for (int i = 0; i < MODE_COUNT; i++) {
            view->items[i] = MODE_NAMES[i];
        }
        return;
    }

    view->screen = APP_SCREEN_MODE;
    view->title = MODE_NAMES[s_mode];
    view->line1 = s_last_action;
    view->line2 = "F1菜单";
    view->selected = 0;
    view->item_count = 0;
}

void mode_manager_init(void)
{
    s_mode = APP_MODE_DOUYIN_PC;
    s_screen = APP_SCREEN_MODE;
    s_last_key = BUTTON_KEY_NONE;
    s_menu_sel = 0;
    s_last_action = "待机";
}

bool mode_manager_update(button_key_t key, uint32_t now_ms, mode_view_t *view, mode_action_t *action)
{
    bool changed = key != s_last_key;

    if (action != NULL) {
        action->type = MODE_ACTION_NONE;
        action->value = 0;
    }

    /* ── Menu mode ── */
    if (s_screen == APP_SCREEN_MENU) {
        if (key == BUTTON_KEY_FUNC1 && key != s_last_key) {
            // Confirm selection → switch mode & exit
            s_mode = (app_mode_t)s_menu_sel;
            s_screen = APP_SCREEN_MODE;
            s_last_action = "待机";
            changed = true;
        } else if (key == BUTTON_KEY_FUNC2 && key != s_last_key) {
            // Exit menu without change
            s_screen = APP_SCREEN_MODE;
            changed = true;
        } else if (key == BUTTON_KEY_UP && key != s_last_key) {
            if (s_menu_sel > 0) s_menu_sel--;
            changed = true;
        } else if (key == BUTTON_KEY_DOWN && key != s_last_key) {
            if (s_menu_sel < MODE_COUNT - 1) s_menu_sel++;
            changed = true;
        }
        s_last_key = key;
        fill_view(view);
        return changed;
    }

    /* ── Mode screen ── */
    // FUNC1 → enter menu
    if (key == BUTTON_KEY_FUNC1 && key != s_last_key) {
        s_menu_sel = (uint8_t)s_mode;
        s_screen = APP_SCREEN_MENU;
        changed = true;
        s_last_key = key;
        fill_view(view);
        return changed;
    }

    // Direction keys → trigger action
    if (key != BUTTON_KEY_FUNC1 && key != BUTTON_KEY_FUNC2 && key != s_last_key) {
        s_last_action = action_text(s_mode, key);
        if (action != NULL) {
            *action = make_action(s_mode, key);
        }
    }

    s_last_key = key;
    fill_view(view);
    return changed;
}
