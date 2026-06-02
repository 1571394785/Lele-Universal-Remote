#include "ssd1306.h"

#include <stdbool.h>
#include <ctype.h>
#include <string.h>

#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"

#define OLED_I2C_PORT I2C_NUM_0
#define OLED_I2C_SPEED_HZ 400000
#define OLED_I2C_TIMEOUT_MS 1000

static const char *TAG = "ssd1306";

static i2c_master_bus_handle_t s_i2c_bus;
static i2c_master_dev_handle_t s_oled_dev;

extern const uint8_t gb2312_font_start[] asm("_binary_gb2312_16x16_bin_start");
extern const uint8_t gb2312_font_end[] asm("_binary_gb2312_16x16_bin_end");
extern const uint8_t gb2312_map_start[] asm("_binary_gb2312_unicode_map_bin_start");
extern const uint8_t gb2312_map_end[] asm("_binary_gb2312_unicode_map_bin_end");

static const uint8_t UNKNOWN_16X16[32] = {
    0xFF, 0xFF, 0x01, 0x80, 0x39, 0x9C, 0x45, 0xA2,
    0x41, 0x82, 0x31, 0x8C, 0x09, 0x90, 0x11, 0x88,
    0x11, 0x88, 0x00, 0x80, 0x11, 0x88, 0x11, 0x88,
    0x00, 0x80, 0x01, 0x80, 0xFF, 0xFF, 0x00, 0x00,
};

static const uint8_t FONT_5X7[][5] = {
    [' ' - ' '] = {0x00, 0x00, 0x00, 0x00, 0x00},
    ['-' - ' '] = {0x08, 0x08, 0x08, 0x08, 0x08},
    ['0' - ' '] = {0x3E, 0x51, 0x49, 0x45, 0x3E},
    ['1' - ' '] = {0x00, 0x42, 0x7F, 0x40, 0x00},
    ['2' - ' '] = {0x42, 0x61, 0x51, 0x49, 0x46},
    ['3' - ' '] = {0x21, 0x41, 0x45, 0x4B, 0x31},
    ['4' - ' '] = {0x18, 0x14, 0x12, 0x7F, 0x10},
    ['5' - ' '] = {0x27, 0x45, 0x45, 0x45, 0x39},
    ['6' - ' '] = {0x3C, 0x4A, 0x49, 0x49, 0x30},
    ['7' - ' '] = {0x01, 0x71, 0x09, 0x05, 0x03},
    ['8' - ' '] = {0x36, 0x49, 0x49, 0x49, 0x36},
    ['9' - ' '] = {0x06, 0x49, 0x49, 0x29, 0x1E},
    ['A' - ' '] = {0x7E, 0x11, 0x11, 0x11, 0x7E},
    ['B' - ' '] = {0x7F, 0x49, 0x49, 0x49, 0x36},
    ['C' - ' '] = {0x3E, 0x41, 0x41, 0x41, 0x22},
    ['D' - ' '] = {0x7F, 0x41, 0x41, 0x22, 0x1C},
    ['E' - ' '] = {0x7F, 0x49, 0x49, 0x49, 0x41},
    ['F' - ' '] = {0x7F, 0x09, 0x09, 0x09, 0x01},
    ['G' - ' '] = {0x3E, 0x41, 0x49, 0x49, 0x7A},
    ['H' - ' '] = {0x7F, 0x08, 0x08, 0x08, 0x7F},
    ['I' - ' '] = {0x00, 0x41, 0x7F, 0x41, 0x00},
    ['J' - ' '] = {0x20, 0x40, 0x41, 0x3F, 0x01},
    ['K' - ' '] = {0x7F, 0x08, 0x14, 0x22, 0x41},
    ['L' - ' '] = {0x7F, 0x40, 0x40, 0x40, 0x40},
    ['M' - ' '] = {0x7F, 0x02, 0x0C, 0x02, 0x7F},
    ['N' - ' '] = {0x7F, 0x04, 0x08, 0x10, 0x7F},
    ['O' - ' '] = {0x3E, 0x41, 0x41, 0x41, 0x3E},
    ['P' - ' '] = {0x7F, 0x09, 0x09, 0x09, 0x06},
    ['Q' - ' '] = {0x3E, 0x41, 0x51, 0x21, 0x5E},
    ['R' - ' '] = {0x7F, 0x09, 0x19, 0x29, 0x46},
    ['S' - ' '] = {0x46, 0x49, 0x49, 0x49, 0x31},
    ['T' - ' '] = {0x01, 0x01, 0x7F, 0x01, 0x01},
    ['U' - ' '] = {0x3F, 0x40, 0x40, 0x40, 0x3F},
    ['V' - ' '] = {0x1F, 0x20, 0x40, 0x20, 0x1F},
    ['W' - ' '] = {0x3F, 0x40, 0x38, 0x40, 0x3F},
    ['X' - ' '] = {0x63, 0x14, 0x08, 0x14, 0x63},
    ['Y' - ' '] = {0x07, 0x08, 0x70, 0x08, 0x07},
    ['Z' - ' '] = {0x61, 0x51, 0x49, 0x45, 0x43},
};

static esp_err_t ssd1306_write(const uint8_t *data, size_t len)
{
    return i2c_master_transmit(s_oled_dev, data, len, OLED_I2C_TIMEOUT_MS);
}

static esp_err_t ssd1306_cmd(uint8_t cmd)
{
    uint8_t data[] = {0x00, cmd};
    return ssd1306_write(data, sizeof(data));
}

static esp_err_t ssd1306_cmds(const uint8_t *cmds, size_t len)
{
    uint8_t buffer[32];

    if (len + 1 > sizeof(buffer)) {
        return ESP_ERR_INVALID_SIZE;
    }

    buffer[0] = 0x00;
    memcpy(&buffer[1], cmds, len);
    return ssd1306_write(buffer, len + 1);
}

static esp_err_t ssd1306_set_cursor(int page, int col)
{
    if (page < 0 || page >= OLED_HEIGHT / 8 || col < 0 || col >= OLED_WIDTH) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(ssd1306_cmd(0xB0 | (uint8_t)page), TAG, "set page failed");
    ESP_RETURN_ON_ERROR(ssd1306_cmd(0x00 | (uint8_t)(col & 0x0F)), TAG, "set low column failed");
    ESP_RETURN_ON_ERROR(ssd1306_cmd(0x10 | (uint8_t)((col >> 4) & 0x0F)), TAG, "set high column failed");
    return ESP_OK;
}

static const uint8_t *font_for_char(char c)
{
    static const uint8_t unknown[5] = {0x7F, 0x49, 0x49, 0x49, 0x7F};

    if (c >= 'a' && c <= 'z') {
        c = (char)toupper((unsigned char)c);
    }

    if (c < ' ' || c > 'Z') {
        return unknown;
    }

    const uint8_t *glyph = FONT_5X7[c - ' '];
    if (glyph[0] == 0 && glyph[1] == 0 && glyph[2] == 0 && glyph[3] == 0 && glyph[4] == 0 && c != ' ') {
        return unknown;
    }

    return glyph;
}

static bool decode_utf8(const char *text, uint32_t *codepoint, size_t *bytes)
{
    const uint8_t *s = (const uint8_t *)text;

    if (s[0] < 0x80) {
        *codepoint = s[0];
        *bytes = 1;
        return true;
    }

    if ((s[0] & 0xE0) == 0xC0 && (s[1] & 0xC0) == 0x80) {
        *codepoint = ((uint32_t)(s[0] & 0x1F) << 6) | (uint32_t)(s[1] & 0x3F);
        *bytes = 2;
        return true;
    }

    if ((s[0] & 0xF0) == 0xE0 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80) {
        *codepoint = ((uint32_t)(s[0] & 0x0F) << 12) |
                     ((uint32_t)(s[1] & 0x3F) << 6) |
                     (uint32_t)(s[2] & 0x3F);
        *bytes = 3;
        return true;
    }

    *codepoint = 0;
    *bytes = 1;
    return false;
}

static uint32_t read_le32(const uint8_t *data)
{
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

static uint16_t read_le16(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static bool unicode_to_gb2312(uint32_t codepoint, uint16_t *gb)
{
    const size_t record_size = 6;
    size_t count = (size_t)(gb2312_map_end - gb2312_map_start) / record_size;
    size_t left = 0;
    size_t right = count;

    while (left < right) {
        size_t mid = left + (right - left) / 2;
        const uint8_t *record = gb2312_map_start + mid * record_size;
        uint32_t record_codepoint = read_le32(record);

        if (record_codepoint == codepoint) {
            *gb = read_le16(record + 4);
            return true;
        }

        if (record_codepoint < codepoint) {
            left = mid + 1;
        } else {
            right = mid;
        }
    }

    return false;
}

static const uint8_t *gb2312_glyph(uint16_t gb)
{
    uint8_t high = (uint8_t)(gb >> 8);
    uint8_t low = (uint8_t)(gb & 0xFF);

    if (high < 0xA1 || high > 0xFE || low < 0xA1 || low > 0xFE) {
        return UNKNOWN_16X16;
    }

    size_t index = ((size_t)(high - 0xA1) * 94 + (size_t)(low - 0xA1)) * 32;
    if (gb2312_font_start + index + 32 > gb2312_font_end) {
        return UNKNOWN_16X16;
    }

    return gb2312_font_start + index;
}

static esp_err_t ssd1306_draw_glyph16(int page, int col, const uint8_t glyph[32])
{
    uint8_t upper[17] = {0x40};
    uint8_t lower[17] = {0x40};

    if (page < 0 || page + 1 >= OLED_HEIGHT / 8 || col < 0 || col + 16 > OLED_WIDTH) {
        return ESP_ERR_INVALID_ARG;
    }

    for (int x = 0; x < 16; ++x) {
        upper[x + 1] = glyph[x * 2];
        lower[x + 1] = glyph[x * 2 + 1];
    }

    ESP_RETURN_ON_ERROR(ssd1306_set_cursor(page, col), TAG, "glyph upper cursor failed");
    ESP_RETURN_ON_ERROR(ssd1306_write(upper, sizeof(upper)), TAG, "glyph upper write failed");
    ESP_RETURN_ON_ERROR(ssd1306_set_cursor(page + 1, col), TAG, "glyph lower cursor failed");
    ESP_RETURN_ON_ERROR(ssd1306_write(lower, sizeof(lower)), TAG, "glyph lower write failed");
    return ESP_OK;
}

esp_err_t ssd1306_init(void)
{
    i2c_master_bus_config_t bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = OLED_I2C_PORT,
        .scl_io_num = OLED_SCL_GPIO,
        .sda_io_num = OLED_SDA_GPIO,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_config, &s_i2c_bus), TAG, "i2c bus init failed");

    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = OLED_I2C_ADDR,
        .scl_speed_hz = OLED_I2C_SPEED_HZ,
    };

    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_i2c_bus, &dev_config, &s_oled_dev), TAG, "i2c add oled failed");

    const uint8_t init_cmds[] = {
        0xAE,       // Display off
        0x20, 0x02, // Page addressing mode
        0xB0,
        0xC8,
        0x00,
        0x10,
        0x40,
        0x81, 0x7F,
        0xA1,
        0xA6,
        0xA8, 0x3F,
        0xA4,
        0xD3, 0x00,
        0xD5, 0x80,
        0xD9, 0xF1,
        0xDA, 0x12,
        0xDB, 0x40,
        0x8D, 0x14,
        0xAF,       // Display on
    };

    ESP_RETURN_ON_ERROR(ssd1306_cmds(init_cmds, sizeof(init_cmds)), TAG, "oled init commands failed");
    ESP_RETURN_ON_ERROR(ssd1306_clear(), TAG, "oled clear failed");
    ESP_LOGI(TAG, "OLED initialized: addr=0x%02X sda=%d scl=%d", OLED_I2C_ADDR, OLED_SDA_GPIO, OLED_SCL_GPIO);
    return ESP_OK;
}

esp_err_t ssd1306_clear(void)
{
    uint8_t data[OLED_WIDTH + 1] = {0};
    data[0] = 0x40;

    for (int page = 0; page < OLED_HEIGHT / 8; ++page) {
        ESP_RETURN_ON_ERROR(ssd1306_set_cursor(page, 0), TAG, "clear set cursor failed");
        ESP_RETURN_ON_ERROR(ssd1306_write(data, sizeof(data)), TAG, "clear write failed");
    }

    return ESP_OK;
}

esp_err_t ssd1306_draw_text(int page, int col, const char *text)
{
    if (text == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(ssd1306_set_cursor(page, col), TAG, "text set cursor failed");

    while (*text != '\0' && col + 6 <= OLED_WIDTH) {
        const uint8_t *glyph = font_for_char(*text++);
        uint8_t data[] = {0x40, glyph[0], glyph[1], glyph[2], glyph[3], glyph[4], 0x00};
        ESP_RETURN_ON_ERROR(ssd1306_write(data, sizeof(data)), TAG, "text write failed");
        col += 6;
    }

    return ESP_OK;
}

esp_err_t ssd1306_draw_text16(int page, int col, const char *text)
{
    if (text == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    while (*text != '\0' && col < OLED_WIDTH) {
        uint32_t codepoint = 0;
        size_t consumed = 1;

        if (!decode_utf8(text, &codepoint, &consumed)) {
            codepoint = 0;
        }

        if (codepoint >= 0x80) {
            uint16_t gb = 0;
            const uint8_t *glyph = unicode_to_gb2312(codepoint, &gb) ? gb2312_glyph(gb) : UNKNOWN_16X16;
            if (col + 16 > OLED_WIDTH) {
                break;
            }
            ESP_RETURN_ON_ERROR(ssd1306_draw_glyph16(page, col, glyph), TAG, "draw gb2312 glyph failed");
            col += 16;
        } else {
            if (col + 6 > OLED_WIDTH) {
                break;
            }
            const uint8_t *ascii = font_for_char((char)codepoint);
            uint8_t data[] = {0x40, ascii[0], ascii[1], ascii[2], ascii[3], ascii[4], 0x00};
            ESP_RETURN_ON_ERROR(ssd1306_set_cursor(page, col), TAG, "draw ascii cursor failed");
            ESP_RETURN_ON_ERROR(ssd1306_write(data, sizeof(data)), TAG, "draw ascii failed");
            col += 6;
        }

        text += consumed;
    }

    return ESP_OK;
}
