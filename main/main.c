#include "esp_err.h"
#include "esp_log.h"
#include "app_display.h"
#include "app_gamepad.h"
#include "battery_adc.h"
#include "ble_hid.h"
#include "buttons.h"
#include "custom_mode.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mode_manager.h"
#include "ssd1306.h"
#include "web_control.h"

static const char *TAG = "app";

void app_main(void)
{
    // 开机延时 1000ms，让电源电压稳定后再初始化外设
    vTaskDelay(pdMS_TO_TICKS(500));

    esp_err_t ret = ssd1306_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "OLED init failed: %s", esp_err_to_name(ret));
        return;
    }

    ESP_ERROR_CHECK(app_display_show_boot());
    vTaskDelay(pdMS_TO_TICKS(1500));

    ret = buttons_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Buttons init failed: %s", esp_err_to_name(ret));
        return;
    }

    ret = battery_adc_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Battery ADC init failed: %s", esp_err_to_name(ret));
    }

    ret = ble_hid_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "BLE HID init failed: %s", esp_err_to_name(ret));
        return;
    }

    ret = custom_mode_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Custom mode init failed: %s", esp_err_to_name(ret));
    }

    mode_manager_init();

    mode_view_t view = {0};
    mode_action_t action = {0};
    mode_manager_update(BUTTON_KEY_NONE, 0, &view, &action);
    ESP_ERROR_CHECK(app_display_render_view(&view, false, NULL, 0, NULL, NULL));

    ESP_LOGI(TAG, "Mode manager ready");

    bool prev_connected = false;
    uint32_t last_status_refresh_ms = 0;
    uint32_t last_header_refresh_ms = 0;
    app_gamepad_state_t gamepad = {0};
    app_gamepad_state_init(&gamepad);

    while (true) {
        uint16_t button_mask = buttons_poll_mask();
        button_key_t key = buttons_first_key_from_mask(button_mask);
        uint32_t now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        bool connected = ble_hid_is_connected();
        const char *addr = ble_hid_get_connected_addr();
        bool manager_changed = false;

        action.type = MODE_ACTION_NONE;
        action.value = 0;
        action.modifier = 0;
        action.key = BUTTON_KEY_NONE;

        if (view.screen == APP_SCREEN_MODE && view.is_gamepad_mode) {
            ESP_ERROR_CHECK(app_gamepad_handle_mode(&gamepad, button_mask, now_ms,
                                                    &view, &manager_changed));
        } else {
            app_gamepad_filter_menu_key(&gamepad, button_mask, &key);
            manager_changed = mode_manager_update(key, now_ms, &view, &action);
            app_gamepad_reset_if_inactive(&gamepad, &view);
        }

        bool status_refresh = view.screen == APP_SCREEN_MENU && view.show_status &&
                              now_ms - last_status_refresh_ms >= 1000;
        bool header_refresh = view.screen == APP_SCREEN_MODE && now_ms - last_header_refresh_ms >= 1000;

        if (manager_changed || connected != prev_connected ||
            status_refresh || header_refresh) {
            ESP_ERROR_CHECK(app_display_render_view(&view, connected, addr, now_ms,
                                                    &last_status_refresh_ms,
                                                    &last_header_refresh_ms));

            ESP_LOGI(TAG, "Screen: %s / %s / %s  conn=%s",
                     view.title, view.line1 ? view.line1 : "-",
                     view.line2 ? view.line2 : "-", addr ? addr : "none");
            prev_connected = connected;
        }
        if (action.type == MODE_ACTION_KEYBOARD_KEY) {
            ESP_ERROR_CHECK(ble_hid_send_key_combo(action.modifier, (uint8_t)action.value));
        } else if (action.type == MODE_ACTION_CUSTOM_SHORTCUT_TAP ||
                   action.type == MODE_ACTION_CUSTOM_SHORTCUT_PRESS) {
            custom_key_combo_t sequence[CUSTOM_SEQUENCE_MAX];
            size_t sequence_count = 0;
            if (custom_mode_get_sequence(action.key, sequence, CUSTOM_SEQUENCE_MAX, &sequence_count)) {
                if (action.type == MODE_ACTION_CUSTOM_SHORTCUT_PRESS && sequence_count == 1) {
                    ESP_ERROR_CHECK(ble_hid_keyboard_press(sequence[0].modifier, sequence[0].keycode));
                } else {
                    for (size_t i = 0; i < sequence_count; i++) {
                        ESP_ERROR_CHECK(ble_hid_send_key_combo(sequence[i].modifier, sequence[i].keycode));
                        vTaskDelay(pdMS_TO_TICKS(15));
                    }
                }
            }
        } else if (action.type == MODE_ACTION_CUSTOM_SHORTCUT_RELEASE) {
            ESP_ERROR_CHECK(ble_hid_keyboard_release());
        } else if (action.type == MODE_ACTION_ABS_MOUSE_DRAG) {
            ESP_ERROR_CHECK(ble_hid_drag_vertical(action.value > 0));
        } else if (action.type == MODE_ACTION_MEDIA) {
            ESP_ERROR_CHECK(ble_hid_send_consumer((uint16_t)action.value));
        } else if (action.type == MODE_ACTION_DISCONNECT) {
            ESP_ERROR_CHECK(ble_hid_disconnect());
        } else if (action.type == MODE_ACTION_PAIRING_MODE) {
            ESP_ERROR_CHECK(ble_hid_enter_pairing_mode());
        } else if (action.type == MODE_ACTION_CLEAR_BONDS) {
            ESP_ERROR_CHECK(ble_hid_clear_pairing());
            ESP_ERROR_CHECK(custom_mode_clear_all());
        } else if (action.type == MODE_ACTION_HID_MODE_TOGGLE) {
            ESP_ERROR_CHECK(ble_hid_toggle_hid_mode());
        } else if (action.type == MODE_ACTION_WEB_START) {
            ESP_ERROR_CHECK(web_control_start());
        } else if (action.type == MODE_ACTION_WEB_STOP) {
            ESP_ERROR_CHECK(web_control_stop());
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
