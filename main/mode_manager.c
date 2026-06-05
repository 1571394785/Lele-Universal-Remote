#include "mode_manager.h"

#include "ble_hid.h"
#include "custom_mode.h"
#include "nvs_flash.h"
#include "nvs.h"

#define NVS_NAMESPACE "remote"
#define NVS_KEY_MODE  "mode"

typedef enum {
    APP_MODE_DOUYIN_PC = 0,
    APP_MODE_DOUYIN_IOS,
    APP_MODE_DOUYIN_ANDROID,
    APP_MODE_MEDIA_CTRL,
    APP_MODE_CUSTOM,
    APP_MODE_GAMEPAD,
    APP_MODE_MOUSE,
} app_mode_t;

typedef enum {
    MENU_PAGE_ROOT = 0,
    MENU_PAGE_MODES,
    MENU_PAGE_SETTINGS,
    MENU_PAGE_GAMES,
    MENU_PAGE_STATUS,
    MENU_PAGE_WEB_CONTROL,
    MENU_PAGE_HID_CONFIRM,
} menu_page_t;

static const char * const MODE_NAMES[MODE_COUNT] = {
    "抖音电脑版",
    "抖音IOS",
    "抖音安卓",
    "多媒体控制",
    "自定义模式",
    "手柄模式",
    "鼠标模式",
};

static const char * const MODE_ACTIONS[MODE_COUNT][4] = {
    [APP_MODE_DOUYIN_PC]     = {"方向上", "方向下", "待机", "右键锁定"},
    [APP_MODE_DOUYIN_IOS]    = {"上拖动", "下拖动", "待机", "空"},
    [APP_MODE_DOUYIN_ANDROID]= {"上拖动", "下拖动", "待机", "空"},
    [APP_MODE_MEDIA_CTRL]    = {"音量+", "音量-", "待机", "媒体键"},
    [APP_MODE_CUSTOM]        = {"自定义", "自定义", "待机", "自定义"},
    [APP_MODE_GAMEPAD]       = {"十字上", "十字下", "待机", "ABXY"},
    [APP_MODE_MOUSE]         = {"向上移动", "向下移动", "待机", "左右移动"},
};

static const char * const ROOT_MENU_ITEMS[] = {
    "模式切换",
    "设置",
    "游戏",
    "状态",
};

static const char * const GAME_MENU_ITEMS[] = {
    "俄罗斯方块",
    "雷霆战机",
    "弹砖块",
    "贪吃蛇",
};

static const char * const SETTINGS_MENU_ITEMS[] = {
    "断开连接",
    "配对模式",
    "恢复出厂设置",
    "网页控制",
    "兼容模式",
};

static app_mode_t s_mode;
static app_screen_t s_screen;
static menu_page_t s_menu_page;
static button_key_t s_last_key;
static uint8_t s_menu_sel;
static const char *s_last_action;
static bool s_pc_right_held;
static app_mode_t s_temporary_return_mode;
static bool s_temporary_mouse;

static void nvs_save_mode(uint8_t mode)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, NVS_KEY_MODE, mode);
        nvs_commit(h);
        nvs_close(h);
    }
}

static uint8_t nvs_load_mode(void)
{
    uint8_t mode = 0;
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u8(h, NVS_KEY_MODE, &mode);
        nvs_close(h);
    }
    return mode;
}

static const char *action_text(app_mode_t mode, button_key_t key)
{
    int idx;
    switch (key) {
    case BUTTON_KEY_UP:    idx = 0; break;
    case BUTTON_KEY_DOWN:  idx = 1; break;
    case BUTTON_KEY_NONE:  idx = 2; break;
    case BUTTON_KEY_LEFT:
    case BUTTON_KEY_RIGHT: idx = 3; break;
    default:               idx = 3; break;
    }
    return MODE_ACTIONS[mode][idx];
}

static mode_action_t make_action(app_mode_t mode, button_key_t key)
{
    mode_action_t a = {MODE_ACTION_NONE, 0, 0, BUTTON_KEY_NONE};

    if (key == BUTTON_KEY_NONE) {
        return a;
    }

    switch (mode) {
    case APP_MODE_DOUYIN_PC:
        if (s_pc_right_held && key != BUTTON_KEY_RIGHT) {
            s_pc_right_held = false;
        }
        if (key == BUTTON_KEY_UP) {
            a.type = MODE_ACTION_KEYBOARD_KEY;
            a.value = 0x52;
        } else if (key == BUTTON_KEY_DOWN) {
            a.type = MODE_ACTION_KEYBOARD_KEY;
            a.value = 0x51;
        } else if (key == BUTTON_KEY_LEFT) {
            a.type = MODE_ACTION_KEYBOARD_KEY;
            a.value = 0x1B; // Keyboard X
        } else if (key == BUTTON_KEY_FUNC2) {
            a.type = MODE_ACTION_KEYBOARD_KEY;
            a.value = 0x0B; // Keyboard H
        } else if (key == BUTTON_KEY_FUNC3) {
            a.type = MODE_ACTION_KEYBOARD_KEY;
            a.value = 0x2C; // Keyboard Space
        } else if (key == BUTTON_KEY_RIGHT) {
            s_pc_right_held = !s_pc_right_held;
            a.type = s_pc_right_held ? MODE_ACTION_KEYBOARD_PRESS : MODE_ACTION_KEYBOARD_RELEASE;
            a.value = 0x4F; // Keyboard Right Arrow
        }
        break;

    case APP_MODE_DOUYIN_IOS:
        if (key == BUTTON_KEY_UP) {
            a.type = MODE_ACTION_ABS_MOUSE_DRAG; a.value = 1;
        } else if (key == BUTTON_KEY_DOWN) {
            a.type = MODE_ACTION_ABS_MOUSE_DRAG; a.value = -1;
        }
        break;

    case APP_MODE_DOUYIN_ANDROID:
        if (key == BUTTON_KEY_UP) {
            a.type = MODE_ACTION_ABS_MOUSE_DRAG; a.value = -1;
        } else if (key == BUTTON_KEY_DOWN) {
            a.type = MODE_ACTION_ABS_MOUSE_DRAG; a.value = 1;
        }
        break;

    case APP_MODE_MEDIA_CTRL:
        if (key == BUTTON_KEY_UP) {
            a.type = MODE_ACTION_MEDIA; a.value = 0x00E9; // Volume Increment
        } else if (key == BUTTON_KEY_DOWN) {
            a.type = MODE_ACTION_MEDIA; a.value = 0x00EA; // Volume Decrement
        } else if (key == BUTTON_KEY_LEFT) {
            a.type = MODE_ACTION_MEDIA; a.value = 0x00B6; // Scan Previous Track
        } else if (key == BUTTON_KEY_RIGHT) {
            a.type = MODE_ACTION_MEDIA; a.value = 0x00B5; // Scan Next Track
        } else if (key == BUTTON_KEY_FUNC2) {
            a.type = MODE_ACTION_MEDIA; a.value = 0x00CD; // Play/Pause
        } else if (key == BUTTON_KEY_FUNC3) {
            a.type = MODE_ACTION_MEDIA; a.value = 0x00E2; // Mute
        }
        break;

    case APP_MODE_CUSTOM: {
        a.type = MODE_ACTION_CUSTOM_SHORTCUT_PRESS;
        a.key = key;
        break;
    }

    case APP_MODE_GAMEPAD:
    case APP_MODE_MOUSE:
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
        view->line1 = NULL;
        view->line2 = NULL;
        view->show_addr = false;
        view->show_status = s_menu_page == MENU_PAGE_STATUS;
        view->show_web_control = s_menu_page == MENU_PAGE_WEB_CONTROL;
        view->show_hid_confirm = s_menu_page == MENU_PAGE_HID_CONFIRM;
        view->show_devices = false;
        view->blink_selected = false;
        view->is_gamepad_mode = false;
        view->is_mouse_mode = false;
        view->is_temporary_mouse = false;
        view->show_scrollbar = false;
        view->selected = s_menu_sel;

        switch (s_menu_page) {
        case MENU_PAGE_ROOT:
            view->title = "菜单";
            view->item_count = sizeof(ROOT_MENU_ITEMS) / sizeof(ROOT_MENU_ITEMS[0]);
            view->show_scrollbar = view->item_count > 3;
            for (uint8_t i = 0; i < view->item_count; i++) {
                view->items[i] = ROOT_MENU_ITEMS[i];
            }
            break;

        case MENU_PAGE_MODES:
            view->title = "模式切换";
            view->item_count = MODE_COUNT;
            view->show_scrollbar = view->item_count > 3;
            for (uint8_t i = 0; i < view->item_count; i++) {
                view->items[i] = MODE_NAMES[i];
            }
            break;

        case MENU_PAGE_GAMES:
            view->title = "游戏";
            view->item_count = sizeof(GAME_MENU_ITEMS) / sizeof(GAME_MENU_ITEMS[0]);
            view->show_scrollbar = view->item_count > 3;
            for (uint8_t i = 0; i < view->item_count; i++) {
                view->items[i] = GAME_MENU_ITEMS[i];
            }
            break;

        case MENU_PAGE_SETTINGS:
            view->title = "设置";
            view->item_count = sizeof(SETTINGS_MENU_ITEMS) / sizeof(SETTINGS_MENU_ITEMS[0]);
            view->show_scrollbar = view->item_count > 3;
            for (uint8_t i = 0; i < view->item_count; i++) {
                view->items[i] = (i == 4) ? (ble_hid_is_full_mode() ? "当前是完全模式" : "当前是兼容模式") :
                                 SETTINGS_MENU_ITEMS[i];
            }
            break;

        case MENU_PAGE_STATUS:
            view->title = "状态";
            view->item_count = 0;
            break;

        case MENU_PAGE_WEB_CONTROL:
            view->title = "网页控制";
            view->item_count = 0;
            break;

        case MENU_PAGE_HID_CONFIRM:
            view->title = ble_hid_is_full_mode() ? "切到兼容模式" : "切到完全模式";
            view->line1 = "切换会重启";
            view->line2 = "F1确认 F2取消";
            view->item_count = 0;
            break;
        }
        return;
    }

    view->screen = APP_SCREEN_MODE;
    view->title = MODE_NAMES[s_mode];
    view->line1 = s_last_action;
    view->line2 = (s_mode == APP_MODE_GAMEPAD) ? "BX菜单" :
                  (s_mode == APP_MODE_MOUSE && s_temporary_mouse) ? "F2返回" : "F1菜单";
    view->show_addr = false;
    view->show_status = false;
    view->show_web_control = false;
    view->show_hid_confirm = false;
    view->show_devices = false;
    view->blink_selected = false;
    view->is_gamepad_mode = s_mode == APP_MODE_GAMEPAD;
    view->is_mouse_mode = s_mode == APP_MODE_MOUSE;
    view->is_temporary_mouse = s_mode == APP_MODE_MOUSE && s_temporary_mouse;
    view->show_scrollbar = false;
    view->selected = 0;
    view->item_count = 0;
}

void mode_manager_init(void)
{
    uint8_t saved = nvs_load_mode();
    if (saved < MODE_COUNT) {
        s_mode = (app_mode_t)saved;
    } else {
        s_mode = APP_MODE_DOUYIN_PC;
    }
    s_screen = APP_SCREEN_MODE;
    s_menu_page = MENU_PAGE_ROOT;
    s_last_key = BUTTON_KEY_NONE;
    s_menu_sel = 0;
    s_last_action = "待机";
    s_pc_right_held = false;
    s_temporary_return_mode = APP_MODE_DOUYIN_PC;
    s_temporary_mouse = false;
}

void mode_manager_enter_menu(mode_view_t *view)
{
    s_menu_page = MENU_PAGE_ROOT;
    s_menu_sel = 0;
    s_screen = APP_SCREEN_MENU;
    s_last_key = BUTTON_KEY_NONE;
    fill_view(view);
}

bool mode_manager_update(button_key_t key, uint32_t now_ms, mode_view_t *view, mode_action_t *action)
{
    bool changed = key != s_last_key;

    if (action != NULL) {
        action->type = MODE_ACTION_NONE;
        action->value = 0;
        action->modifier = 0;
        action->key = BUTTON_KEY_NONE;
    }

    /* ── Menu mode ── */
    if (s_screen == APP_SCREEN_MENU) {
        if (key == BUTTON_KEY_FUNC1 && key != s_last_key) {
            if (s_menu_page == MENU_PAGE_ROOT) {
                if (s_menu_sel == 0) {
                    s_menu_page = MENU_PAGE_MODES;
                } else if (s_menu_sel == 1) {
                    s_menu_page = MENU_PAGE_SETTINGS;
                } else if (s_menu_sel == 2) {
                    s_menu_page = MENU_PAGE_GAMES;
                } else {
                    s_menu_page = MENU_PAGE_STATUS;
                }
                s_menu_sel = (s_menu_page == MENU_PAGE_MODES) ? (uint8_t)s_mode : 0;
            } else if (s_menu_page == MENU_PAGE_MODES) {
                s_mode = (app_mode_t)s_menu_sel;
                s_temporary_mouse = false;
                nvs_save_mode((uint8_t)s_mode);
                s_screen = APP_SCREEN_MODE;
                s_menu_page = MENU_PAGE_ROOT;
                s_last_action = "待机";
            } else if (s_menu_page == MENU_PAGE_GAMES) {
                if (action != NULL) {
                    if (s_menu_sel == 0) {
                        action->type = MODE_ACTION_GAME_TETRIS;
                    } else if (s_menu_sel == 1) {
                        action->type = MODE_ACTION_GAME_SHOOTER;
                    } else if (s_menu_sel == 2) {
                        action->type = MODE_ACTION_GAME_BREAKOUT;
                    } else {
                        action->type = MODE_ACTION_GAME_SNAKE;
                    }
                }
                s_screen = APP_SCREEN_MODE;
                s_menu_page = MENU_PAGE_ROOT;
                s_last_action = GAME_MENU_ITEMS[s_menu_sel];
            } else if (s_menu_page == MENU_PAGE_SETTINGS) {
                if (action != NULL) {
                    if (s_menu_sel == 0) {
                        action->type = MODE_ACTION_DISCONNECT;
                    } else if (s_menu_sel == 1) {
                        action->type = MODE_ACTION_PAIRING_MODE;
                    } else if (s_menu_sel == 2) {
                        action->type = MODE_ACTION_CLEAR_BONDS;
                    } else if (s_menu_sel == 3) {
                        action->type = MODE_ACTION_WEB_START;
                    }
                }
                if (s_menu_sel == 0) {
                    s_screen = APP_SCREEN_MODE;
                    s_menu_page = MENU_PAGE_ROOT;
                    s_last_action = "已断开";
                } else if (s_menu_sel == 1) {
                    s_screen = APP_SCREEN_MODE;
                    s_menu_page = MENU_PAGE_ROOT;
                    s_last_action = "配对模式";
                } else if (s_menu_sel == 2) {
                    s_screen = APP_SCREEN_MODE;
                    s_menu_page = MENU_PAGE_ROOT;
                    s_mode = APP_MODE_DOUYIN_PC;
                    nvs_save_mode((uint8_t)s_mode);
                    s_last_action = "恢复出厂";
                } else if (s_menu_sel == 3) {
                    s_menu_page = MENU_PAGE_WEB_CONTROL;
                    s_menu_sel = 0;
                } else {
                    s_menu_page = MENU_PAGE_HID_CONFIRM;
                    s_menu_sel = 0;
                }
            } else if (s_menu_page == MENU_PAGE_STATUS) {
                s_menu_sel = 0;
            } else if (s_menu_page == MENU_PAGE_WEB_CONTROL) {
                s_menu_sel = 0;
            } else if (s_menu_page == MENU_PAGE_HID_CONFIRM) {
                if (action != NULL) {
                    action->type = MODE_ACTION_HID_MODE_TOGGLE;
                }
                s_screen = APP_SCREEN_MODE;
                s_menu_page = MENU_PAGE_ROOT;
                s_last_action = ble_hid_is_full_mode() ? "兼容模式" : "完全模式";
            }
            changed = true;
        } else if (key == BUTTON_KEY_FUNC2 && key != s_last_key) {
            if (s_menu_page == MENU_PAGE_ROOT) {
                s_screen = APP_SCREEN_MODE;
            } else if (s_menu_page == MENU_PAGE_WEB_CONTROL) {
                if (action != NULL) {
                    action->type = MODE_ACTION_WEB_STOP;
                }
                s_menu_page = MENU_PAGE_ROOT;
                s_menu_sel = 0;
            } else if (s_menu_page == MENU_PAGE_HID_CONFIRM) {
                s_menu_page = MENU_PAGE_SETTINGS;
                s_menu_sel = 4;
            } else {
                s_menu_page = MENU_PAGE_ROOT;
                s_menu_sel = 0;
            }
            changed = true;
        } else if (key == BUTTON_KEY_UP && key != s_last_key) {
            if (s_menu_sel > 0) s_menu_sel--;
            changed = true;
        } else if (key == BUTTON_KEY_DOWN && key != s_last_key) {
            uint8_t max_sel = 0;
            if (s_menu_page == MENU_PAGE_ROOT) {
                max_sel = (sizeof(ROOT_MENU_ITEMS) / sizeof(ROOT_MENU_ITEMS[0])) - 1;
            } else if (s_menu_page == MENU_PAGE_MODES) {
                max_sel = MODE_COUNT - 1;
            } else if (s_menu_page == MENU_PAGE_GAMES) {
                max_sel = (sizeof(GAME_MENU_ITEMS) / sizeof(GAME_MENU_ITEMS[0])) - 1;
            } else if (s_menu_page == MENU_PAGE_SETTINGS) {
                max_sel = (sizeof(SETTINGS_MENU_ITEMS) / sizeof(SETTINGS_MENU_ITEMS[0])) - 1;
            } else if (s_menu_page == MENU_PAGE_STATUS) {
                max_sel = 0;
            } else if (s_menu_page == MENU_PAGE_WEB_CONTROL) {
                max_sel = 0;
            } else if (s_menu_page == MENU_PAGE_HID_CONFIRM) {
                max_sel = 0;
            }
            if (s_menu_sel < max_sel) s_menu_sel++;
            changed = true;
        }
        s_last_key = key;
        fill_view(view);
        return changed;
    }

    /* ── Mode screen ── */
    if (s_mode == APP_MODE_MOUSE && s_temporary_mouse &&
        key == BUTTON_KEY_FUNC2 && key != s_last_key) {
        s_mode = s_temporary_return_mode;
        s_temporary_mouse = false;
        s_last_action = "待机";
        changed = true;
        s_last_key = key;
        fill_view(view);
        return changed;
    }

    if (s_mode == APP_MODE_DOUYIN_PC && key == BUTTON_KEY_FUNC4 && key != s_last_key) {
        if (s_pc_right_held && action != NULL) {
            action->type = MODE_ACTION_KEYBOARD_RELEASE;
            action->value = 0x4F;
            s_pc_right_held = false;
        }
        s_temporary_return_mode = s_mode;
        s_temporary_mouse = true;
        s_mode = APP_MODE_MOUSE;
        s_last_action = "临时鼠标";
        changed = true;
        s_last_key = key;
        fill_view(view);
        return changed;
    }

    // FUNC1 enters menu in normal modes. Gamepad mode uses FUNC1+FUNC2 long press.
    if (s_mode != APP_MODE_GAMEPAD && key == BUTTON_KEY_FUNC1 && key != s_last_key) {
        if (s_pc_right_held && action != NULL) {
            action->type = MODE_ACTION_KEYBOARD_RELEASE;
            action->value = 0x4F;
            s_pc_right_held = false;
        }
        if (s_mode == APP_MODE_MOUSE && s_temporary_mouse) {
            s_mode = s_temporary_return_mode;
            s_temporary_mouse = false;
        }
        mode_manager_enter_menu(view);
        changed = true;
        s_last_key = key;
        return changed;
    }

    // Direction keys → trigger action
    if (s_mode == APP_MODE_CUSTOM && key == BUTTON_KEY_NONE &&
        s_last_key != BUTTON_KEY_NONE && s_last_key != BUTTON_KEY_FUNC1) {
        if (action != NULL) {
            action->type = MODE_ACTION_CUSTOM_SHORTCUT_RELEASE;
            action->key = s_last_key;
        }
        changed = true;
    } else if (key != BUTTON_KEY_FUNC1 && key != s_last_key) {
        s_last_action = action_text(s_mode, key);
        if (action != NULL) {
            *action = make_action(s_mode, key);
        }
        if (s_mode == APP_MODE_DOUYIN_PC && key == BUTTON_KEY_RIGHT) {
            s_last_action = s_pc_right_held ? "右键已锁定" : "右键已解除";
        } else if (s_mode == APP_MODE_DOUYIN_PC && key == BUTTON_KEY_LEFT) {
            s_last_action = "X";
        } else if (s_mode == APP_MODE_DOUYIN_PC && key == BUTTON_KEY_FUNC2) {
            s_last_action = "H";
        } else if (s_mode == APP_MODE_DOUYIN_PC && key == BUTTON_KEY_FUNC3) {
            s_last_action = "空格";
        } else if (s_mode == APP_MODE_MOUSE && key == BUTTON_KEY_FUNC4) {
            s_last_action = "左键";
        } else if (s_mode == APP_MODE_MOUSE && key == BUTTON_KEY_FUNC3) {
            s_last_action = "右键";
        }
    }

    s_last_key = key;
    fill_view(view);
    return changed;
}
