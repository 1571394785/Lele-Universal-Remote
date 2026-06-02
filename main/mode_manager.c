#include "mode_manager.h"

#include <stdio.h>

#include "ble_hid.h"
#include "custom_mode.h"
#include "nvs_flash.h"
#include "nvs.h"

#define NVS_NAMESPACE "remote"
#define NVS_KEY_MODE  "mode"
#define DEVICE_F1_LONG_MS 800

typedef enum {
    APP_MODE_DOUYIN_PC = 0,
    APP_MODE_DOUYIN_IOS,
    APP_MODE_DOUYIN_ANDROID,
    APP_MODE_MEDIA_CTRL,
    APP_MODE_CUSTOM,
} app_mode_t;

typedef enum {
    MENU_PAGE_ROOT = 0,
    MENU_PAGE_MODES,
    MENU_PAGE_SETTINGS,
    MENU_PAGE_STATUS,
    MENU_PAGE_WEB_CONTROL,
    MENU_PAGE_DEVICES,
} menu_page_t;

static const char * const MODE_NAMES[MODE_COUNT] = {
    "抖音电脑版",
    "抖音IOS",
    "抖音安卓",
    "多媒体控制",
    "自定义模式",
};

static const char * const MODE_ACTIONS[MODE_COUNT][4] = {
    [APP_MODE_DOUYIN_PC]     = {"方向上", "方向下", "待机", "空"},
    [APP_MODE_DOUYIN_IOS]    = {"上拖动", "下拖动", "待机", "空"},
    [APP_MODE_DOUYIN_ANDROID]= {"上拖动", "下拖动", "待机", "空"},
    [APP_MODE_MEDIA_CTRL]    = {"音量+", "音量-", "待机", "切歌"},
    [APP_MODE_CUSTOM]        = {"自定义", "自定义", "待机", "自定义"},
};

static const char * const ROOT_MENU_ITEMS[] = {
    "模式切换",
    "设置",
    "状态",
    "设备列表",
};

static const char * const SETTINGS_MENU_ITEMS[] = {
    "恢复出厂设置",
    "网页控制",
};

static const char * const DEVICE_MENU_ITEMS[] = {
    "设备一",
    "设备二",
    "设备三",
    "设备四",
};

static const char * const CURRENT_DEVICE_TITLES[] = {
    "当前设备一",
    "当前设备二",
    "当前设备三",
    "当前设备四",
};

static app_mode_t s_mode;
static app_screen_t s_screen;
static menu_page_t s_menu_page;
static button_key_t s_last_key;
static uint8_t s_menu_sel;
static const char *s_last_action;
static uint32_t s_menu_f1_down_ms;
static bool s_menu_f1_long_handled;
static bool s_device_pairing_blink;
static char s_device_item_labels[BLE_HID_DEVICE_SLOT_COUNT][20];

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
        }
        break;

    case APP_MODE_CUSTOM: {
        a.type = MODE_ACTION_CUSTOM_SHORTCUT_PRESS;
        a.key = key;
        break;
    }

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
        view->show_devices = s_menu_page == MENU_PAGE_DEVICES;
        view->blink_selected = s_menu_page == MENU_PAGE_DEVICES && s_device_pairing_blink;
        view->selected = s_menu_sel;

        switch (s_menu_page) {
        case MENU_PAGE_ROOT:
            view->title = "菜单";
            view->item_count = sizeof(ROOT_MENU_ITEMS) / sizeof(ROOT_MENU_ITEMS[0]);
            for (uint8_t i = 0; i < view->item_count; i++) {
                view->items[i] = ROOT_MENU_ITEMS[i];
            }
            break;

        case MENU_PAGE_MODES:
            view->title = "模式切换";
            view->item_count = MODE_COUNT;
            for (uint8_t i = 0; i < view->item_count; i++) {
                view->items[i] = MODE_NAMES[i];
            }
            break;

        case MENU_PAGE_SETTINGS:
            view->title = "设置";
            view->item_count = sizeof(SETTINGS_MENU_ITEMS) / sizeof(SETTINGS_MENU_ITEMS[0]);
            for (uint8_t i = 0; i < view->item_count; i++) {
                view->items[i] = SETTINGS_MENU_ITEMS[i];
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

        case MENU_PAGE_DEVICES:
            view->title = CURRENT_DEVICE_TITLES[ble_hid_get_selected_device_slot()];
            view->item_count = sizeof(DEVICE_MENU_ITEMS) / sizeof(DEVICE_MENU_ITEMS[0]);
            for (uint8_t i = 0; i < view->item_count; i++) {
                ble_hid_device_slot_t slot;
                if (ble_hid_get_device_slot(i, &slot)) {
                    snprintf(s_device_item_labels[i], sizeof(s_device_item_labels[i]),
                             "%s %02x%02x", DEVICE_MENU_ITEMS[i], slot.addr[4], slot.addr[5]);
                } else {
                    snprintf(s_device_item_labels[i], sizeof(s_device_item_labels[i]),
                             "%s ----", DEVICE_MENU_ITEMS[i]);
                }
                view->items[i] = s_device_item_labels[i];
            }
            break;
        }
        return;
    }

    view->screen = APP_SCREEN_MODE;
    view->title = MODE_NAMES[s_mode];
    view->line1 = s_last_action;
    view->line2 = "F1菜单";
    view->show_addr = false;
    view->show_status = false;
    view->show_web_control = false;
    view->show_devices = false;
    view->blink_selected = false;
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
    s_menu_f1_down_ms = 0;
    s_menu_f1_long_handled = false;
    s_device_pairing_blink = false;
}

bool mode_manager_update(button_key_t key, uint32_t now_ms, mode_view_t *view, mode_action_t *action)
{
    bool changed = key != s_last_key;

    if (s_device_pairing_blink && !ble_hid_is_pairing_mode() &&
        !(s_screen == APP_SCREEN_MENU && s_menu_page == MENU_PAGE_DEVICES &&
          key == BUTTON_KEY_FUNC1 && s_last_key == BUTTON_KEY_FUNC1 &&
          now_ms - s_menu_f1_down_ms >= DEVICE_F1_LONG_MS)) {
        s_device_pairing_blink = false;
        changed = true;
    }

    if (action != NULL) {
        action->type = MODE_ACTION_NONE;
        action->value = 0;
        action->modifier = 0;
        action->key = BUTTON_KEY_NONE;
    }

    /* ── Menu mode ── */
    if (s_screen == APP_SCREEN_MENU) {
        if (s_menu_page == MENU_PAGE_DEVICES && key == BUTTON_KEY_FUNC1 && s_last_key != BUTTON_KEY_FUNC1) {
            s_menu_f1_down_ms = now_ms;
            s_menu_f1_long_handled = false;
            changed = true;
        } else if (s_menu_page == MENU_PAGE_DEVICES && key == BUTTON_KEY_FUNC1 &&
                   !s_menu_f1_long_handled && now_ms - s_menu_f1_down_ms >= DEVICE_F1_LONG_MS) {
            if (action != NULL) {
                action->type = MODE_ACTION_DEVICE_PAIR;
                action->value = s_menu_sel;
            }
            s_device_pairing_blink = true;
            s_menu_f1_long_handled = true;
            changed = true;
        } else if (s_menu_page == MENU_PAGE_DEVICES && key != BUTTON_KEY_FUNC1 &&
                   s_last_key == BUTTON_KEY_FUNC1 && !s_menu_f1_long_handled) {
            if (action != NULL) {
                action->type = MODE_ACTION_DEVICE_CONNECT;
                action->value = s_menu_sel;
            }
            s_device_pairing_blink = false;
            changed = true;
        } else if (key == BUTTON_KEY_FUNC1 && key != s_last_key) {
            if (s_menu_page == MENU_PAGE_ROOT) {
                if (s_menu_sel == 0) {
                    s_menu_page = MENU_PAGE_MODES;
                } else if (s_menu_sel == 1) {
                    s_menu_page = MENU_PAGE_SETTINGS;
                } else if (s_menu_sel == 2) {
                    s_menu_page = MENU_PAGE_STATUS;
                } else {
                    s_menu_page = MENU_PAGE_DEVICES;
                }
                s_menu_sel = (s_menu_page == MENU_PAGE_MODES) ? (uint8_t)s_mode : 0;
                if (s_menu_page == MENU_PAGE_DEVICES) {
                    s_menu_sel = ble_hid_get_selected_device_slot();
                    s_device_pairing_blink = false;
                    s_menu_f1_down_ms = now_ms;
                    s_menu_f1_long_handled = true;
                }
            } else if (s_menu_page == MENU_PAGE_MODES) {
                s_mode = (app_mode_t)s_menu_sel;
                nvs_save_mode((uint8_t)s_mode);
                s_screen = APP_SCREEN_MODE;
                s_menu_page = MENU_PAGE_ROOT;
                s_last_action = "待机";
            } else if (s_menu_page == MENU_PAGE_SETTINGS) {
                if (action != NULL) {
                    if (s_menu_sel == 0) {
                        action->type = MODE_ACTION_CLEAR_BONDS;
                    } else {
                        action->type = MODE_ACTION_WEB_START;
                    }
                }
                if (s_menu_sel == 0) {
                    s_screen = APP_SCREEN_MODE;
                    s_menu_page = MENU_PAGE_ROOT;
                    s_mode = APP_MODE_DOUYIN_PC;
                    nvs_save_mode((uint8_t)s_mode);
                    s_last_action = "恢复出厂";
                } else {
                    s_menu_page = MENU_PAGE_WEB_CONTROL;
                    s_menu_sel = 0;
                }
            } else if (s_menu_page == MENU_PAGE_STATUS) {
                s_menu_sel = 0;
            } else if (s_menu_page == MENU_PAGE_WEB_CONTROL) {
                s_menu_sel = 0;
            } else if (s_menu_page == MENU_PAGE_DEVICES) {
                s_menu_sel = 0;
                s_device_pairing_blink = false;
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
            } else {
                s_menu_page = MENU_PAGE_ROOT;
                s_menu_sel = 0;
            }
            changed = true;
        } else if (key == BUTTON_KEY_UP && key != s_last_key) {
            if (s_menu_sel > 0) s_menu_sel--;
            if (s_menu_page == MENU_PAGE_DEVICES) s_device_pairing_blink = false;
            changed = true;
        } else if (key == BUTTON_KEY_DOWN && key != s_last_key) {
            uint8_t max_sel = 0;
            if (s_menu_page == MENU_PAGE_ROOT) {
                max_sel = (sizeof(ROOT_MENU_ITEMS) / sizeof(ROOT_MENU_ITEMS[0])) - 1;
            } else if (s_menu_page == MENU_PAGE_MODES) {
                max_sel = MODE_COUNT - 1;
            } else if (s_menu_page == MENU_PAGE_SETTINGS) {
                max_sel = (sizeof(SETTINGS_MENU_ITEMS) / sizeof(SETTINGS_MENU_ITEMS[0])) - 1;
            } else if (s_menu_page == MENU_PAGE_STATUS) {
                max_sel = 0;
            } else if (s_menu_page == MENU_PAGE_WEB_CONTROL) {
                max_sel = 0;
            } else if (s_menu_page == MENU_PAGE_DEVICES) {
                max_sel = (sizeof(DEVICE_MENU_ITEMS) / sizeof(DEVICE_MENU_ITEMS[0])) - 1;
            }
            if (s_menu_sel < max_sel) s_menu_sel++;
            if (s_menu_page == MENU_PAGE_DEVICES) s_device_pairing_blink = false;
            changed = true;
        }
        s_last_key = key;
        fill_view(view);
        return changed;
    }

    /* ── Mode screen ── */
    // FUNC1 → enter menu
    if (key == BUTTON_KEY_FUNC1 && key != s_last_key) {
        s_menu_page = MENU_PAGE_ROOT;
        s_menu_sel = 0;
        s_screen = APP_SCREEN_MENU;
        changed = true;
        s_last_key = key;
        fill_view(view);
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
    }

    s_last_key = key;
    fill_view(view);
    return changed;
}
