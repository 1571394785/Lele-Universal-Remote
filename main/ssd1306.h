#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define OLED_I2C_ADDR 0x3C
#define OLED_SCL_GPIO 17
#define OLED_SDA_GPIO 18
#define OLED_WIDTH 128
#define OLED_HEIGHT 64

esp_err_t ssd1306_init(void);
esp_err_t ssd1306_clear(void);
esp_err_t ssd1306_draw_text(int page, int col, const char *text);
esp_err_t ssd1306_draw_text16(int page, int col, const char *text);
esp_err_t ssd1306_draw_text24(int y, int col, const char *text);
esp_err_t ssd1306_draw_bluetooth_icon(int page, int col, bool connected);
esp_err_t ssd1306_draw_battery_icon(int page, int col, uint8_t percent);
esp_err_t ssd1306_invert_area(int start_page, int end_page, int col_start, int col_end);
esp_err_t ssd1306_flush(void);
