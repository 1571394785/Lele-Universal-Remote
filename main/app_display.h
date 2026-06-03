#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "mode_manager.h"

uint8_t app_display_read_battery_percent(int *battery_mv);
esp_err_t app_display_show_boot(void);
esp_err_t app_display_render_view(const mode_view_t *view, bool connected, const char *addr,
                                  uint32_t now_ms, uint32_t *last_status_refresh_ms,
                                  uint32_t *last_header_refresh_ms);
