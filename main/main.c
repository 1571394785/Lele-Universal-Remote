#include "esp_err.h"
#include "esp_log.h"
#include <stdio.h>
#include "ble_hid.h"
#include "buttons.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mode_manager.h"
#include "ssd1306.h"

static const char *TAG = "app";

void app_main(void)
{
    // 开机延时 1000ms，让电源电压稳定后再初始化外设
    vTaskDelay(pdMS_TO_TICKS(1000));

    esp_err_t ret = ssd1306_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "OLED init failed: %s", esp_err_to_name(ret));
        return;
    }

    ret = buttons_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Buttons init failed: %s", esp_err_to_name(ret));
        return;
    }

    ret = ble_hid_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "BLE HID init failed: %s", esp_err_to_name(ret));
        return;
    }

    mode_manager_init();

    mode_view_t view = {0};
    mode_action_t action = {0};
    mode_manager_update(BUTTON_KEY_NONE, 0, &view, &action);
    ESP_ERROR_CHECK(ssd1306_draw_text16(0, 0, view.title));
    ESP_ERROR_CHECK(ssd1306_draw_text16(2, 0, view.line1));
    ESP_ERROR_CHECK(ssd1306_draw_text16(5, 0, view.line2));
    ESP_ERROR_CHECK(ssd1306_flush());

    ESP_LOGI(TAG, "Mode manager ready");

    bool prev_connected = false;

    while (true) {
        button_key_t key = buttons_poll();
        uint32_t now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        bool connected = ble_hid_is_connected();
        const char *addr = ble_hid_get_connected_addr();

        if (mode_manager_update(key, now_ms, &view, &action) || connected != prev_connected) {
            ESP_ERROR_CHECK(ssd1306_clear());

            if (view.screen == APP_SCREEN_MENU) {
                // ── Menu screen ──
                // Title at page 0 (hw pages 0-1)
                ESP_ERROR_CHECK(ssd1306_draw_text16(0, 0, view.title));
                // Items at pages 2, 4, 6 (each uses 2 hw pages, no overlap)
                for (uint8_t i = 0; i < view.item_count && i < 3; i++) {
                    ESP_ERROR_CHECK(ssd1306_draw_text16(2 + i * 2, 0, view.items[i]));
                }
                // 选中行整行反色（覆盖 2 个硬件页）
                uint8_t sel = view.selected;
                if (sel < view.item_count && sel < 3) {
                    ESP_ERROR_CHECK(ssd1306_invert_area(2 + sel * 2, 4 + sel * 2, 0, OLED_WIDTH));
                }
            } else {
                // ── Mode screen ──
                ESP_ERROR_CHECK(ssd1306_draw_text16(0, 0, view.title));
                ESP_ERROR_CHECK(ssd1306_draw_text16(2, 0, view.line1));
                if (connected && addr) {
                    ESP_ERROR_CHECK(ssd1306_draw_text16(4, 0, addr));
                }
                ESP_ERROR_CHECK(ssd1306_draw_text16(5, 0, view.line2));
            }

            ESP_ERROR_CHECK(ssd1306_flush());

            ESP_LOGI(TAG, "Screen: %s / %s / %s  conn=%s",
                     view.title, view.line1 ? view.line1 : "-",
                     view.line2 ? view.line2 : "-", addr ? addr : "none");
            prev_connected = connected;
        }
        if (action.type == MODE_ACTION_KEYBOARD_KEY) {
            ESP_ERROR_CHECK(ble_hid_send_key((uint8_t)action.value));
        } else if (action.type == MODE_ACTION_ABS_MOUSE_DRAG) {
            ESP_ERROR_CHECK(ble_hid_drag_vertical(action.value > 0));
        } else if (action.type == MODE_ACTION_MEDIA) {
            // TODO: media control placeholder
            ESP_LOGI(TAG, "media action (placeholder)");
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
