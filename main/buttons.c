#include "buttons.h"

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"

#define BUTTON_ACTIVE_LEVEL 0
#define BUTTON_DEBOUNCE_POLLS 2

typedef struct {
    button_key_t key;
    gpio_num_t gpio;
    const char *name_cn;
    uint8_t stable_count;
    bool raw_pressed;
    bool debounced_pressed;
} button_config_t;

static const char *TAG = "buttons";

static button_config_t s_buttons[] = {
    {BUTTON_KEY_UP,    GPIO_NUM_5,  "上",     0, false, false},
    {BUTTON_KEY_DOWN,  GPIO_NUM_6,  "下",     0, false, false},
    {BUTTON_KEY_LEFT,  GPIO_NUM_7,  "左",     0, false, false},
    {BUTTON_KEY_RIGHT, GPIO_NUM_4,  "右",     0, false, false},
    {BUTTON_KEY_FUNC1, GPIO_NUM_41, "功能一", 0, false, false},
    {BUTTON_KEY_FUNC2, GPIO_NUM_40, "功能二", 0, false, false},
    {BUTTON_KEY_FUNC3, GPIO_NUM_42, "功能三", 0, false, false},
    {BUTTON_KEY_FUNC4, GPIO_NUM_39, "功能四", 0, false, false},
};

esp_err_t buttons_init(void)
{
    uint64_t pin_mask = 0;

    for (size_t i = 0; i < sizeof(s_buttons) / sizeof(s_buttons[0]); ++i) {
        pin_mask |= 1ULL << s_buttons[i].gpio;
    }

    gpio_config_t io_config = {
        .pin_bit_mask = pin_mask,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_RETURN_ON_ERROR(gpio_config(&io_config), TAG, "gpio config failed");
    ESP_LOGI(TAG, "Buttons initialized");
    return ESP_OK;
}

uint16_t buttons_poll_mask(void)
{
    uint16_t pressed_mask = 0;

    for (size_t i = 0; i < sizeof(s_buttons) / sizeof(s_buttons[0]); ++i) {
        button_config_t *button = &s_buttons[i];
        bool raw_pressed = gpio_get_level(button->gpio) == BUTTON_ACTIVE_LEVEL;

        if (raw_pressed == button->raw_pressed) {
            if (button->stable_count < BUTTON_DEBOUNCE_POLLS) {
                button->stable_count++;
            }
        } else {
            button->raw_pressed = raw_pressed;
            button->stable_count = 0;
        }

        if (button->stable_count >= BUTTON_DEBOUNCE_POLLS) {
            button->debounced_pressed = button->raw_pressed;
        }

        if (button->debounced_pressed) {
            pressed_mask |= BUTTON_MASK(button->key);
        }
    }

    return pressed_mask;
}

button_key_t buttons_first_key_from_mask(uint16_t mask)
{
    for (size_t i = 0; i < sizeof(s_buttons) / sizeof(s_buttons[0]); ++i) {
        if ((mask & BUTTON_MASK(s_buttons[i].key)) != 0) {
            return s_buttons[i].key;
        }
    }

    return BUTTON_KEY_NONE;
}

button_key_t buttons_poll(void)
{
    return buttons_first_key_from_mask(buttons_poll_mask());
}

const char *buttons_key_name_cn(button_key_t key)
{
    for (size_t i = 0; i < sizeof(s_buttons) / sizeof(s_buttons[0]); ++i) {
        if (s_buttons[i].key == key) {
            return s_buttons[i].name_cn;
        }
    }

    return "无";
}
