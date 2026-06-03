#include "app_display.h"

#include <stdio.h>

#include "battery_adc.h"
#include "esp_check.h"
#include "ssd1306.h"

static const char *TAG = "app_display";

uint8_t app_display_read_battery_percent(int *battery_mv)
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

esp_err_t app_display_show_boot(void)
{
    ESP_RETURN_ON_ERROR(ssd1306_clear(), TAG, "clear boot screen");
    ESP_RETURN_ON_ERROR(ssd1306_draw_text24(4, 4, "万能遥控器"), TAG, "draw boot title");
    ESP_RETURN_ON_ERROR(ssd1306_draw_text16(5, 4, "Powered By 乐乐"), TAG, "draw boot subtitle");
    return ssd1306_flush();
}

static esp_err_t draw_gamepad_mode_screen(void)
{
    ESP_RETURN_ON_ERROR(ssd1306_draw_text16_ccw(6, 0, "手柄模式"), TAG, "draw gamepad title");
    ESP_RETURN_ON_ERROR(ssd1306_draw_text16_ccw(48, 0, "十字ABXY"), TAG, "draw gamepad keys");
    ESP_RETURN_ON_ERROR(ssd1306_draw_text16_ccw(88, 0, "BX三秒"), TAG, "draw gamepad menu hint");
    return ESP_OK;
}

static esp_err_t draw_menu_list(const mode_view_t *view)
{
    const uint8_t base_page = 2;
    const uint8_t visible = 3;
    uint8_t start = 0;

    if (view->item_count > visible) {
        if (view->selected >= visible - 1) {
            start = view->selected - (visible - 1);
        }
        if (start + visible > view->item_count) {
            start = view->item_count - visible;
        }
    }

    for (uint8_t i = 0; i < visible && start + i < view->item_count; i++) {
        uint8_t idx = start + i;
        ESP_RETURN_ON_ERROR(ssd1306_draw_text16(base_page + i * 2, 0, view->items[idx]),
                            TAG, "draw menu item");
    }

    if (view->show_scrollbar) {
        ESP_RETURN_ON_ERROR(ssd1306_draw_scrollbar(base_page, visible * 2, view->item_count,
                                                   visible, view->selected),
                            TAG, "draw scrollbar");
    }

    if (view->item_count > 0) {
        uint8_t sel_pos = view->selected - start;
        int invert_end_col = view->show_scrollbar ? OLED_WIDTH - 4 : OLED_WIDTH;
        ESP_RETURN_ON_ERROR(ssd1306_invert_area(base_page + sel_pos * 2,
                                                base_page + 2 + sel_pos * 2,
                                                0, invert_end_col),
                            TAG, "invert menu item");
    }

    return ESP_OK;
}

static esp_err_t draw_menu_view(const mode_view_t *view, const char *addr,
                                uint32_t now_ms, uint32_t *last_status_refresh_ms)
{
    ESP_RETURN_ON_ERROR(ssd1306_draw_text16(0, 0, view->title), TAG, "draw menu title");

    if (view->show_status) {
        char mac_line[32];
        char bat_line[24];
        int battery_mv = 0;
        uint8_t battery_pct = app_display_read_battery_percent(&battery_mv);

        snprintf(mac_line, sizeof(mac_line), "MAC %s", addr ? addr : "No Link");
        if (battery_mv > 0) {
            snprintf(bat_line, sizeof(bat_line), "BAT %d.%02dV %u%%",
                     battery_mv / 1000, (battery_mv % 1000) / 10, battery_pct);
        } else {
            snprintf(bat_line, sizeof(bat_line), "BAT --");
        }

        ESP_RETURN_ON_ERROR(ssd1306_draw_text(2, 0, mac_line), TAG, "draw mac line");
        ESP_RETURN_ON_ERROR(ssd1306_draw_text(4, 0, bat_line), TAG, "draw battery line");
        if (last_status_refresh_ms != NULL) {
            *last_status_refresh_ms = now_ms;
        }
    } else if (view->show_web_control) {
        ESP_RETURN_ON_ERROR(ssd1306_draw_text16(2, 0, "AP 自定义设置"), TAG, "draw web ap");
        ESP_RETURN_ON_ERROR(ssd1306_draw_text(3, 0, ""), TAG, "draw web spacer");
        ESP_RETURN_ON_ERROR(ssd1306_draw_text(4, 0, "URL 192.168.4.1"), TAG, "draw web url");
        ESP_RETURN_ON_ERROR(ssd1306_draw_text(6, 0, "F2 Exit"), TAG, "draw web exit");
    } else if (view->show_hid_confirm) {
        ESP_RETURN_ON_ERROR(ssd1306_draw_text16(2, 0, view->line1), TAG, "draw hid confirm line1");
        ESP_RETURN_ON_ERROR(ssd1306_draw_text16(5, 0, view->line2), TAG, "draw hid confirm line2");
    } else {
        ESP_RETURN_ON_ERROR(draw_menu_list(view), TAG, "draw menu list");
    }

    return ESP_OK;
}

static esp_err_t draw_mode_view(const mode_view_t *view, bool connected)
{
    if (view->is_gamepad_mode) {
        return draw_gamepad_mode_screen();
    }

    uint8_t battery_pct = app_display_read_battery_percent(NULL);
    ESP_RETURN_ON_ERROR(ssd1306_draw_text16(0, 0, view->title), TAG, "draw mode title");
    ESP_RETURN_ON_ERROR(ssd1306_draw_battery_icon(0, 96, battery_pct), TAG, "draw battery icon");
    ESP_RETURN_ON_ERROR(ssd1306_draw_bluetooth_icon(0, 112, connected), TAG, "draw bluetooth icon");
    ESP_RETURN_ON_ERROR(ssd1306_draw_text16(2, 0, view->line1), TAG, "draw mode line1");
    ESP_RETURN_ON_ERROR(ssd1306_draw_text16(5, 0, view->line2), TAG, "draw mode line2");
    return ESP_OK;
}

esp_err_t app_display_render_view(const mode_view_t *view, bool connected, const char *addr,
                                  uint32_t now_ms, uint32_t *last_status_refresh_ms,
                                  uint32_t *last_header_refresh_ms)
{
    if (view == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(ssd1306_clear(), TAG, "clear screen");

    if (view->screen == APP_SCREEN_MENU) {
        ESP_RETURN_ON_ERROR(draw_menu_view(view, addr, now_ms, last_status_refresh_ms),
                            TAG, "draw menu view");
    } else {
        ESP_RETURN_ON_ERROR(draw_mode_view(view, connected), TAG, "draw mode view");
        if (last_header_refresh_ms != NULL) {
            *last_header_refresh_ms = now_ms;
        }
    }

    return ssd1306_flush();
}
