#include "esp_err.h"
#include "esp_log.h"
#include <stdio.h>
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

static uint8_t read_battery_percent(int *battery_mv)
{
    int mv = 0;
    if (battery_adc_read_actual_mv(&mv) != ESP_OK) {
        if (battery_mv != NULL) {
            *battery_mv = 0;
        }
        return 0;
    }

    if (battery_mv != NULL) {
        *battery_mv = mv;
    }
    return battery_adc_percent_from_mv(mv);
}

void app_main(void)
{
    // 开机延时 1000ms，让电源电压稳定后再初始化外设
    vTaskDelay(pdMS_TO_TICKS(500));

    esp_err_t ret = ssd1306_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "OLED init failed: %s", esp_err_to_name(ret));
        return;
    }

    ESP_ERROR_CHECK(ssd1306_clear());
    ESP_ERROR_CHECK(ssd1306_draw_text24(4, 4, "万能遥控器"));
    ESP_ERROR_CHECK(ssd1306_draw_text16(5, 4, "Powered By 乐乐"));
    ESP_ERROR_CHECK(ssd1306_flush());
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
    ESP_ERROR_CHECK(ssd1306_draw_text16(0, 0, view.title));
    ESP_ERROR_CHECK(ssd1306_draw_battery_icon(0, 96, read_battery_percent(NULL)));
    ESP_ERROR_CHECK(ssd1306_draw_bluetooth_icon(0, 112, false));
    ESP_ERROR_CHECK(ssd1306_draw_text16(2, 0, view.line1));
    ESP_ERROR_CHECK(ssd1306_draw_text16(5, 0, view.line2));
    ESP_ERROR_CHECK(ssd1306_flush());

    ESP_LOGI(TAG, "Mode manager ready");

    bool prev_connected = false;
    uint32_t last_status_refresh_ms = 0;
    uint32_t last_header_refresh_ms = 0;

    while (true) {
        button_key_t key = buttons_poll();
        uint32_t now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        bool connected = ble_hid_is_connected();
        const char *addr = ble_hid_get_connected_addr();

        bool status_refresh = view.screen == APP_SCREEN_MENU && view.show_status &&
                              now_ms - last_status_refresh_ms >= 1000;
        bool header_refresh = view.screen == APP_SCREEN_MODE && now_ms - last_header_refresh_ms >= 1000;

        if (mode_manager_update(key, now_ms, &view, &action) || connected != prev_connected ||
            status_refresh || header_refresh) {
            ESP_ERROR_CHECK(ssd1306_clear());

            if (view.screen == APP_SCREEN_MENU) {
                // ── Menu screen ──
                // Title at page 0 (hw pages 0-1)
                ESP_ERROR_CHECK(ssd1306_draw_text16(0, 0, view.title));

                if (view.show_status) {
                    char mac_line[32];
                    char bat_line[24];
                    int battery_mv = 0;
                    uint8_t battery_pct = read_battery_percent(&battery_mv);

                    snprintf(mac_line, sizeof(mac_line), "MAC %s", addr ? addr : "No Link");
                    if (battery_mv > 0) {
                        snprintf(bat_line, sizeof(bat_line), "BAT %d.%02dV %u%%",
                                 battery_mv / 1000, (battery_mv % 1000) / 10, battery_pct);
                    } else {
                        snprintf(bat_line, sizeof(bat_line), "BAT --");
                    }

                    ESP_ERROR_CHECK(ssd1306_draw_text(2, 0, mac_line));
                    ESP_ERROR_CHECK(ssd1306_draw_text(4, 0, bat_line));
                    last_status_refresh_ms = now_ms;
                } else if (view.show_web_control) {
                    ESP_ERROR_CHECK(ssd1306_draw_text16(2, 0, "AP 自定义设置"));
                    ESP_ERROR_CHECK(ssd1306_draw_text(3, 0, ""));
                    ESP_ERROR_CHECK(ssd1306_draw_text(4, 0, "URL 192.168.4.1"));
                    ESP_ERROR_CHECK(ssd1306_draw_text(6, 0, "F2 Exit"));
                } else {
                    uint8_t base_page = 2;
                    uint8_t vis = 3; // 可见行数
                    uint8_t start = 0;
                    if (view.item_count > vis) {
                        if (view.selected >= vis - 1) {
                            start = view.selected - (vis - 1);
                        }
                        if (start + vis > view.item_count) {
                            start = view.item_count - vis;
                        }
                    }

                    for (uint8_t i = 0; i < vis && start + i < view.item_count; i++) {
                        uint8_t idx = start + i;
                        ESP_ERROR_CHECK(ssd1306_draw_text16(base_page + i * 2, 0, view.items[idx]));
                    }

                    // 选中行反色
                    if (view.item_count > 0) {
                        uint8_t sel_pos = view.selected - start;
                        ESP_ERROR_CHECK(ssd1306_invert_area(base_page + sel_pos * 2, base_page + 2 + sel_pos * 2, 0, OLED_WIDTH));
                    }
                }
            } else {
                // ── Mode screen ──
                uint8_t battery_pct = read_battery_percent(NULL);
                ESP_ERROR_CHECK(ssd1306_draw_text16(0, 0, view.title));
                ESP_ERROR_CHECK(ssd1306_draw_battery_icon(0, 96, battery_pct));
                ESP_ERROR_CHECK(ssd1306_draw_bluetooth_icon(0, 112, connected));
                ESP_ERROR_CHECK(ssd1306_draw_text16(2, 0, view.line1));
                ESP_ERROR_CHECK(ssd1306_draw_text16(5, 0, view.line2));
                last_header_refresh_ms = now_ms;
            }

            ESP_ERROR_CHECK(ssd1306_flush());

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
        } else if (action.type == MODE_ACTION_WEB_START) {
            ESP_ERROR_CHECK(web_control_start());
        } else if (action.type == MODE_ACTION_WEB_STOP) {
            ESP_ERROR_CHECK(web_control_stop());
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
