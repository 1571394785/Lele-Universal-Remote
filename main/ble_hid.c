#include "ble_hid.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <inttypes.h>
#include <stdio.h>

#include "esp_bt.h"
#include "esp_bt_defs.h"
#include "esp_bt_main.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_hidd.h"
#include "esp_hidd_gatts.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define BLE_HID_KEYBOARD_MAP_INDEX 0
#define BLE_HID_KEYBOARD_REPORT_ID 1
#define BLE_HID_MOUSE_REPORT_ID 2
#define BLE_HID_DRAG_STEPS 20

static const char *TAG = "ble_hid";
static const char *DEVICE_NAME = "ESP32-HID";

static esp_hidd_dev_t *s_hid_dev;
static bool s_connected;
static bool s_advertising;
static bool s_adv_config_done;
static bool s_scan_rsp_config_done;
static char s_connected_addr[18]; // "XX:XX:XX:XX:XX:XX"

static uint8_t s_hid_service_uuid[] = {
    0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80,
    0x00, 0x10, 0x00, 0x00, 0x12, 0x18, 0x00, 0x00,
};

static const uint8_t HID_REPORT_MAP[] = {
    0x05, 0x01,       // Usage Page (Generic Desktop)
    0x09, 0x06,       // Usage (Keyboard)
    0xA1, 0x01,       // Collection (Application)
    0x85, BLE_HID_KEYBOARD_REPORT_ID,
    0x05, 0x07,       // Usage Page (Keyboard/Keypad)
    0x19, 0xE0,       // Usage Minimum (Keyboard LeftControl)
    0x29, 0xE7,       // Usage Maximum (Keyboard Right GUI)
    0x15, 0x00,
    0x25, 0x01,
    0x75, 0x01,
    0x95, 0x08,
    0x81, 0x02,       // Input (Modifier byte)
    0x95, 0x01,
    0x75, 0x08,
    0x81, 0x01,       // Input (Reserved byte)
    0x95, 0x06,
    0x75, 0x08,
    0x15, 0x00,
    0x25, 0x65,
    0x05, 0x07,
    0x19, 0x00,
    0x29, 0x65,
    0x81, 0x00,       // Input (6 key array)
    0xC0,             // End Collection

    0x05, 0x01,       // Usage Page (Generic Desktop)
    0x09, 0x02,       // Usage (Mouse)
    0xA1, 0x01,       // Collection (Application)
    0x85, BLE_HID_MOUSE_REPORT_ID,
    0x09, 0x01,       // Usage (Pointer)
    0xA1, 0x00,       // Collection (Physical)
    0x05, 0x09,       // Usage Page (Button)
    0x19, 0x01,
    0x29, 0x03,
    0x15, 0x00,
    0x25, 0x01,
    0x95, 0x03,
    0x75, 0x01,
    0x81, 0x02,       // Input (3 buttons)
    0x95, 0x01,
    0x75, 0x05,
    0x81, 0x01,       // Input (5 bits padding)
    0x05, 0x01,       // Usage Page (Generic Desktop)
    0x09, 0x30,       // Usage (X)
    0x09, 0x31,       // Usage (Y)
    0x09, 0x38,       // Usage (Wheel)
    0x15, 0x81,       // Logical Minimum (-127)
    0x25, 0x7F,       // Logical Maximum (127)
    0x75, 0x08,       // Report Size (8)
    0x95, 0x03,       // Report Count (3)
    0x81, 0x06,       // Input (Data, Variable, Relative)
    0xC0,             // End Physical Collection
    0xC0              // End Application Collection
};

static esp_hid_raw_report_map_t s_hid_report_maps[] = {
    {
        .data = HID_REPORT_MAP,
        .len = sizeof(HID_REPORT_MAP),
    },
};

static esp_hid_device_config_t s_hid_config = {
    .vendor_id = 0x16C0,
    .product_id = 0x05DF,
    .version = 0x0100,
    .device_name = "ESP32-HID",
    .manufacturer_name = "ESP32",
    .serial_number = "0001",
    .report_maps = s_hid_report_maps,
    .report_maps_len = 1,
};

static esp_ble_adv_data_t s_adv_data = {
    .set_scan_rsp = false,
    .include_name = true,
    .include_txpower = true,
    .min_interval = 0x0006,
    .max_interval = 0x0010,
    .appearance = ESP_BLE_APPEARANCE_HID_KEYBOARD,
    .manufacturer_len = 0,
    .p_manufacturer_data = NULL,
    .service_data_len = 0,
    .p_service_data = NULL,
    .service_uuid_len = sizeof(s_hid_service_uuid),
    .p_service_uuid = s_hid_service_uuid,
    .flag = ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT,
};

static esp_ble_adv_data_t s_scan_rsp_data = {
    .set_scan_rsp = true,
    .include_name = false,
    .include_txpower = false,
};

static esp_ble_adv_params_t s_adv_params = {
    .adv_int_min = 0x20,
    .adv_int_max = 0x30,
    .adv_type = ADV_TYPE_IND,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .channel_map = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

static void start_advertising_if_ready(void)
{
    if (s_adv_config_done && s_scan_rsp_config_done && !s_advertising) {
        esp_ble_gap_start_advertising(&s_adv_params);
    }
}

static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
        if (param->adv_data_cmpl.status == ESP_BT_STATUS_SUCCESS) {
            s_adv_config_done = true;
            start_advertising_if_ready();
        } else {
            ESP_LOGE(TAG, "adv data config failed: %d", param->adv_data_cmpl.status);
        }
        break;

    case ESP_GAP_BLE_SCAN_RSP_DATA_SET_COMPLETE_EVT:
        if (param->scan_rsp_data_cmpl.status == ESP_BT_STATUS_SUCCESS) {
            s_scan_rsp_config_done = true;
            start_advertising_if_ready();
        } else {
            ESP_LOGE(TAG, "scan rsp config failed: %d", param->scan_rsp_data_cmpl.status);
        }
        break;

    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        if (param->adv_start_cmpl.status == ESP_BT_STATUS_SUCCESS) {
            s_advertising = true;
            ESP_LOGI(TAG, "advertising started");
        } else {
            ESP_LOGE(TAG, "advertising start failed: %d", param->adv_start_cmpl.status);
        }
        break;

    case ESP_GAP_BLE_SEC_REQ_EVT:
        ESP_LOGI(TAG, "security request, responding");
        esp_ble_gap_security_rsp(param->ble_security.ble_req.bd_addr, true);
        break;

    case ESP_GAP_BLE_PASSKEY_NOTIF_EVT:
        ESP_LOGI(TAG, "passkey notify: %06" PRIu32, param->ble_security.key_notif.passkey);
        break;

    case ESP_GAP_BLE_AUTH_CMPL_EVT:
        if (param->ble_security.auth_cmpl.success) {
            ESP_LOGI(TAG, "pairing succeeded");
            snprintf(s_connected_addr, sizeof(s_connected_addr),
                     "%02x:%02x:%02x:%02x:%02x:%02x",
                     param->ble_security.auth_cmpl.bd_addr[0],
                     param->ble_security.auth_cmpl.bd_addr[1],
                     param->ble_security.auth_cmpl.bd_addr[2],
                     param->ble_security.auth_cmpl.bd_addr[3],
                     param->ble_security.auth_cmpl.bd_addr[4],
                     param->ble_security.auth_cmpl.bd_addr[5]);
            ESP_LOGI(TAG, "connected to: %s", s_connected_addr);
        } else {
            ESP_LOGI(TAG, "pairing failed");
        }
        break;

    default:
        break;
    }
}

static void hidd_event_callback(void *handler_args, esp_event_base_t base, int32_t id, void *event_data)
{
    switch (id) {
    case ESP_HIDD_START_EVENT:
        ESP_LOGI(TAG, "HID started");
        start_advertising_if_ready();
        break;

    case ESP_HIDD_CONNECT_EVENT:
        s_connected = true;
        s_advertising = false;
        ESP_LOGI(TAG, "connected");
        break;

    case ESP_HIDD_DISCONNECT_EVENT:
        s_connected = false;
        s_advertising = false;
        s_connected_addr[0] = '\0';
        ESP_LOGI(TAG, "disconnected");
        esp_ble_gap_start_advertising(&s_adv_params);
        break;

    default:
        break;
    }
}

static esp_err_t init_nvs(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "nvs erase failed");
        ret = nvs_flash_init();
    }
    return ret;
}

esp_err_t ble_hid_init(void)
{
    ESP_RETURN_ON_ERROR(init_nvs(), TAG, "nvs init failed");

    esp_err_t event_ret = esp_event_loop_create_default();
    if (event_ret != ESP_OK && event_ret != ESP_ERR_INVALID_STATE) {
        return event_ret;
    }

    ESP_RETURN_ON_ERROR(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT), TAG, "release classic bt failed");

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_bt_controller_init(&bt_cfg), TAG, "bt controller init failed");
    ESP_RETURN_ON_ERROR(esp_bt_controller_enable(ESP_BT_MODE_BLE), TAG, "bt controller enable failed");
    ESP_RETURN_ON_ERROR(esp_bluedroid_init(), TAG, "bluedroid init failed");
    ESP_RETURN_ON_ERROR(esp_bluedroid_enable(), TAG, "bluedroid enable failed");

    ESP_RETURN_ON_ERROR(esp_ble_gap_register_callback(gap_event_handler), TAG, "gap callback failed");
    ESP_RETURN_ON_ERROR(esp_ble_gatts_register_callback(esp_hidd_gatts_event_handler), TAG, "gatts callback failed");
    ESP_RETURN_ON_ERROR(esp_ble_gap_set_device_name(DEVICE_NAME), TAG, "set device name failed");

    uint8_t auth_req = ESP_LE_AUTH_REQ_SC_BOND;
    uint8_t iocap = ESP_IO_CAP_NONE;
    uint8_t key_size = 16;
    uint8_t init_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    uint8_t rsp_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    ESP_RETURN_ON_ERROR(esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE, &auth_req, sizeof(auth_req)), TAG, "auth param failed");
    ESP_RETURN_ON_ERROR(esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &iocap, sizeof(iocap)), TAG, "iocap param failed");
    ESP_RETURN_ON_ERROR(esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE, &key_size, sizeof(key_size)), TAG, "key size param failed");
    ESP_RETURN_ON_ERROR(esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY, &init_key, sizeof(init_key)), TAG, "init key param failed");
    ESP_RETURN_ON_ERROR(esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY, &rsp_key, sizeof(rsp_key)), TAG, "rsp key param failed");

    ESP_RETURN_ON_ERROR(esp_ble_gap_config_adv_data(&s_adv_data), TAG, "adv data config failed");
    ESP_RETURN_ON_ERROR(esp_ble_gap_config_adv_data(&s_scan_rsp_data), TAG, "scan rsp config failed");
    ESP_RETURN_ON_ERROR(esp_hidd_dev_init(&s_hid_config, ESP_HID_TRANSPORT_BLE, hidd_event_callback, &s_hid_dev), TAG, "hid init failed");

    return ESP_OK;
}

bool ble_hid_is_connected(void)
{
    return s_connected;
}

const char *ble_hid_get_connected_addr(void)
{
    return s_connected_addr[0] ? s_connected_addr : NULL;
}

static esp_err_t send_keyboard_report(uint8_t keycode)
{
    if (!s_connected || s_hid_dev == NULL) {
        return ESP_OK;
    }

    uint8_t report[8] = {0};
    report[2] = keycode;

    ESP_LOGI(TAG, "keyboard keycode=0x%02x", keycode);
    ESP_RETURN_ON_ERROR(esp_hidd_dev_input_set(s_hid_dev, BLE_HID_KEYBOARD_MAP_INDEX, BLE_HID_KEYBOARD_REPORT_ID, report, sizeof(report)),
                        TAG, "key press failed");

    vTaskDelay(pdMS_TO_TICKS(20));
    memset(report, 0, sizeof(report));
    return esp_hidd_dev_input_set(s_hid_dev, BLE_HID_KEYBOARD_MAP_INDEX, BLE_HID_KEYBOARD_REPORT_ID, report, sizeof(report));
}

esp_err_t ble_hid_send_key(uint8_t keycode)
{
    if (keycode == 0) {
        return ESP_OK;
    }

    return send_keyboard_report(keycode);
}

static esp_err_t send_mouse_report(uint8_t buttons, int8_t dx, int8_t dy, int8_t wheel)
{
    if (!s_connected || s_hid_dev == NULL) {
        ESP_LOGW(TAG, "mouse report dropped (not connected)");
        return ESP_OK;
    }

    uint8_t report[4] = {buttons, (uint8_t)dx, (uint8_t)dy, (uint8_t)wheel};

    ESP_LOGI(TAG, "mouse buttons=%u dx=%d dy=%d wheel=%d", buttons, dx, dy, wheel);
    esp_err_t ret = esp_hidd_dev_input_set(s_hid_dev, BLE_HID_KEYBOARD_MAP_INDEX, BLE_HID_MOUSE_REPORT_ID, report, sizeof(report));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "mouse report failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t ble_hid_drag_vertical(bool upward)
{
    int8_t wheel = upward ? -20 : 20;

    ESP_LOGI(TAG, "scroll %s start (%d steps, wheel=%d)", upward ? "up" : "down", BLE_HID_DRAG_STEPS, wheel);

    for (int step = 0; step < BLE_HID_DRAG_STEPS; ++step) {
        ESP_RETURN_ON_ERROR(send_mouse_report(0, 0, 0, wheel), TAG, "scroll step failed");
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    return ESP_OK;
}
