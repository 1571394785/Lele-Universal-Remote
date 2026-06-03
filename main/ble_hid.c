#include "ble_hid.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <inttypes.h>
#include <stdio.h>

#include "battery_adc.h"
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
#include "nvs.h"
#include "nvs_flash.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define BLE_HID_REPORT_MAP_INDEX 0
#define BLE_HID_KEYBOARD_REPORT_ID 1
#define BLE_HID_MOUSE_REPORT_ID 2
#define BLE_HID_CONSUMER_REPORT_ID 3
#define BLE_HID_DRAG_STEPS 5
#define BLE_HID_BATTERY_FIRST_NOTIFY_MS 1000
#define BLE_HID_BATTERY_NOTIFY_INTERVAL_MS 60000
#define BLE_HID_BATTERY_TASK_STACK 4096
#define BLE_HID_DEVICE_NVS_NAMESPACE "bledev"
#define BLE_HID_DEVICE_NVS_CURRENT "current"

static const char *TAG = "ble_hid";
static const char *DEVICE_NAME = "ESP32-HID";

static esp_hidd_dev_t *s_hid_dev;
static bool s_connected;
static bool s_advertising;
static bool s_adv_config_done;
static bool s_scan_rsp_config_done;
static bool s_allow_auto_advertise;
static bool s_pairing_discoverable;
static bool s_slot_connect_pending;
static uint8_t s_selected_device_slot;
static ble_hid_device_slot_t s_device_slots[BLE_HID_DEVICE_SLOT_COUNT];
static esp_bd_addr_t s_connected_bda;
static char s_connected_addr[18]; // "XX:XX:XX:XX:XX:XX"
static TaskHandle_t s_battery_task;
static TickType_t s_next_battery_notify_tick;
static uint8_t s_last_battery_level;

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
    0x15, 0x81,       // Logical Minimum (-127)
    0x25, 0x7F,       // Logical Maximum (127)
    0x75, 0x08,       // Report Size (8)
    0x95, 0x01,       // Report Count (1)
    0x81, 0x06,       // Input (Data, Var, Rel)
    0x09, 0x31,       // Usage (Y)
    0x15, 0x81,       // Logical Minimum (-127)
    0x25, 0x7F,       // Logical Maximum (127)
    0x75, 0x08,       // Report Size (8)
    0x95, 0x01,       // Report Count (1)
    0x81, 0x06,       // Input (Data, Var, Rel)
    0x09, 0x38,       // Usage (Wheel)
    0x15, 0x81,       // Logical Minimum (-127)
    0x25, 0x7F,       // Logical Maximum (127)
    0x75, 0x08,       // Report Size (8)
    0x95, 0x01,       // Report Count (1)
    0x81, 0x06,       // Input (Data, Var, Rel)
    0xC0,             // End Physical Collection
    0xC0,             // End Application Collection

    0x05, 0x0C,       // Usage Page (Consumer)
    0x09, 0x01,       // Usage (Consumer Control)
    0xA1, 0x01,       // Collection (Application)
    0x85, BLE_HID_CONSUMER_REPORT_ID,
    0x15, 0x00,       // Logical Minimum (0)
    0x26, 0xFF, 0x03, // Logical Maximum (0x03FF)
    0x19, 0x00,       // Usage Minimum (0)
    0x2A, 0xFF, 0x03, // Usage Maximum (0x03FF)
    0x75, 0x10,       // Report Size (16)
    0x95, 0x01,       // Report Count (1)
    0x81, 0x00,       // Input (Data, Array, Abs)
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
    .appearance = ESP_BLE_APPEARANCE_GENERIC_HID,
    .manufacturer_len = 0,
    .p_manufacturer_data = NULL,
    .service_data_len = 0,
    .p_service_data = NULL,
    .service_uuid_len = sizeof(s_hid_service_uuid),
    .p_service_uuid = s_hid_service_uuid,
    .flag = ESP_BLE_ADV_FLAG_BREDR_NOT_SPT,
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

static char slot_key(uint8_t slot)
{
    return (char)('0' + slot);
}

static void load_device_slots(void)
{
    nvs_handle_t h;
    memset(s_device_slots, 0, sizeof(s_device_slots));
    s_selected_device_slot = 0;

    if (nvs_open(BLE_HID_DEVICE_NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        return;
    }

    nvs_get_u8(h, BLE_HID_DEVICE_NVS_CURRENT, &s_selected_device_slot);
    if (s_selected_device_slot >= BLE_HID_DEVICE_SLOT_COUNT) {
        s_selected_device_slot = 0;
    }

    for (uint8_t i = 0; i < BLE_HID_DEVICE_SLOT_COUNT; i++) {
        char key[6] = {'s', 'l', 'o', 't', slot_key(i), '\0'};
        size_t len = sizeof(s_device_slots[i]);
        nvs_get_blob(h, key, &s_device_slots[i], &len);
        if (len != sizeof(s_device_slots[i])) {
            memset(&s_device_slots[i], 0, sizeof(s_device_slots[i]));
        }
    }

    nvs_close(h);
}

static void save_selected_slot(void)
{
    nvs_handle_t h;
    if (nvs_open(BLE_HID_DEVICE_NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, BLE_HID_DEVICE_NVS_CURRENT, s_selected_device_slot);
        nvs_commit(h);
        nvs_close(h);
    }
}

static void save_device_slot(uint8_t slot)
{
    if (slot >= BLE_HID_DEVICE_SLOT_COUNT) {
        return;
    }

    nvs_handle_t h;
    if (nvs_open(BLE_HID_DEVICE_NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        char key[6] = {'s', 'l', 'o', 't', slot_key(slot), '\0'};
        nvs_set_blob(h, key, &s_device_slots[slot], sizeof(s_device_slots[slot]));
        nvs_commit(h);
        nvs_close(h);
    }
}

static void remove_bond_for_addr(const uint8_t addr[6])
{
    int dev_num = esp_ble_get_bond_device_num();
    if (dev_num <= 0) {
        return;
    }

    esp_ble_bond_dev_t bonds[dev_num];
    if (esp_ble_get_bond_device_list(&dev_num, bonds) != ESP_OK) {
        return;
    }

    for (int i = 0; i < dev_num; i++) {
        if (memcmp(bonds[i].bd_addr, addr, 6) == 0) {
            esp_ble_remove_bond_device(bonds[i].bd_addr);
            return;
        }
    }
}

static void stop_advertising_now(void)
{
    if (s_advertising) {
        esp_ble_gap_stop_advertising();
        s_advertising = false;
    }
}

static esp_err_t configure_discoverable(bool discoverable)
{
    s_adv_data.flag = ESP_BLE_ADV_FLAG_BREDR_NOT_SPT;
    if (discoverable) {
        s_adv_data.flag |= ESP_BLE_ADV_FLAG_GEN_DISC;
    }
    s_adv_config_done = false;
    return esp_ble_gap_config_adv_data(&s_adv_data);
}

static bool addr_is_any_slot(const uint8_t addr[6])
{
    for (uint8_t i = 0; i < BLE_HID_DEVICE_SLOT_COUNT; i++) {
        if (s_device_slots[i].valid && memcmp(s_device_slots[i].addr, addr, 6) == 0) {
            return true;
        }
    }
    return false;
}

static bool has_bond_device(void)
{
    return esp_ble_get_bond_device_num() > 0;
}

static bool addr_is_allowed_for_security(const uint8_t addr[6])
{
    if (s_pairing_discoverable || addr_is_any_slot(addr)) {
        return true;
    }

    return has_bond_device();
}

static void start_advertising_if_ready(void)
{
    if (s_slot_connect_pending && s_adv_config_done && s_scan_rsp_config_done && !s_advertising) {
        if (has_bond_device()) {
            esp_ble_gap_start_advertising(&s_adv_params);
        }
        s_slot_connect_pending = false;
        return;
    }

    if (s_allow_auto_advertise && s_pairing_discoverable && s_adv_config_done && s_scan_rsp_config_done && !s_advertising) {
        esp_ble_gap_start_advertising(&s_adv_params);
    }
}

static bool tick_reached(TickType_t now, TickType_t target)
{
    return (int32_t)(now - target) >= 0;
}

static void battery_notify_task(void *arg)
{
    (void)arg;

    while (true) {
        if (s_connected && s_hid_dev != NULL) {
            TickType_t now = xTaskGetTickCount();
            if (s_next_battery_notify_tick == 0 || tick_reached(now, s_next_battery_notify_tick)) {
                int battery_mv = 0;
                if (battery_adc_read_actual_mv(&battery_mv) == ESP_OK) {
                    s_last_battery_level = battery_adc_percent_from_mv(battery_mv);
                }
                esp_hidd_dev_battery_set(s_hid_dev, s_last_battery_level);
                ESP_LOGI(TAG, "battery level notified: %u%%", s_last_battery_level);
                s_next_battery_notify_tick = now + pdMS_TO_TICKS(BLE_HID_BATTERY_NOTIFY_INTERVAL_MS);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
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

    case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
        if (param->adv_stop_cmpl.status == ESP_BT_STATUS_SUCCESS) {
            s_advertising = false;
            ESP_LOGI(TAG, "advertising stopped");
        } else {
            ESP_LOGE(TAG, "advertising stop failed: %d", param->adv_stop_cmpl.status);
        }
        break;

    case ESP_GAP_BLE_SEC_REQ_EVT:
        if (addr_is_allowed_for_security(param->ble_security.ble_req.bd_addr)) {
            ESP_LOGI(TAG, "security request accepted");
            esp_ble_gap_security_rsp(param->ble_security.ble_req.bd_addr, true);
        } else {
            ESP_LOGW(TAG, "security request rejected");
            esp_ble_gap_security_rsp(param->ble_security.ble_req.bd_addr, false);
            esp_ble_gap_disconnect(param->ble_security.ble_req.bd_addr);
        }
        break;

    case ESP_GAP_BLE_PASSKEY_NOTIF_EVT:
        ESP_LOGI(TAG, "passkey notify: %06" PRIu32, param->ble_security.key_notif.passkey);
        break;

    case ESP_GAP_BLE_AUTH_CMPL_EVT:
        if (param->ble_security.auth_cmpl.success) {
            if (!addr_is_allowed_for_security(param->ble_security.auth_cmpl.bd_addr)) {
                ESP_LOGW(TAG, "auth completed for unauthorized device, disconnecting");
                esp_ble_gap_disconnect(param->ble_security.auth_cmpl.bd_addr);
                break;
            }
            ESP_LOGI(TAG, "pairing succeeded");
            memcpy(s_connected_bda, param->ble_security.auth_cmpl.bd_addr, sizeof(s_connected_bda));
            snprintf(s_connected_addr, sizeof(s_connected_addr),
                     "%02x:%02x:%02x:%02x:%02x:%02x",
                     param->ble_security.auth_cmpl.bd_addr[0],
                     param->ble_security.auth_cmpl.bd_addr[1],
                     param->ble_security.auth_cmpl.bd_addr[2],
                     param->ble_security.auth_cmpl.bd_addr[3],
                     param->ble_security.auth_cmpl.bd_addr[4],
                     param->ble_security.auth_cmpl.bd_addr[5]);
            ESP_LOGI(TAG, "connected to: %s", s_connected_addr);
            if (s_pairing_discoverable) {
                s_device_slots[s_selected_device_slot].valid = true;
                memcpy(s_device_slots[s_selected_device_slot].addr, param->ble_security.auth_cmpl.bd_addr, 6);
                s_device_slots[s_selected_device_slot].addr_type = (uint8_t)param->ble_security.auth_cmpl.addr_type;
                save_device_slot(s_selected_device_slot);
                s_pairing_discoverable = false;
                s_allow_auto_advertise = false;
            }
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
        if (!s_pairing_discoverable && has_bond_device()) {
            s_allow_auto_advertise = true;
            s_slot_connect_pending = true;
            configure_discoverable(false);
        }
        start_advertising_if_ready();
        break;

    case ESP_HIDD_CONNECT_EVENT:
        s_connected = true;
        s_advertising = false;
        s_next_battery_notify_tick = xTaskGetTickCount() + pdMS_TO_TICKS(BLE_HID_BATTERY_FIRST_NOTIFY_MS);
        ESP_LOGI(TAG, "connected");
        break;

    case ESP_HIDD_DISCONNECT_EVENT:
        s_connected = false;
        s_advertising = false;
        s_next_battery_notify_tick = 0;
        memset(s_connected_bda, 0, sizeof(s_connected_bda));
        s_connected_addr[0] = '\0';
        ESP_LOGI(TAG, "disconnected");
        if (!s_pairing_discoverable && s_allow_auto_advertise && has_bond_device()) {
            s_slot_connect_pending = true;
        }
        start_advertising_if_ready();
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
    load_device_slots();
    s_allow_auto_advertise = false;
    s_pairing_discoverable = false;
    s_slot_connect_pending = false;

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

    int battery_mv = 0;
    if (battery_adc_read_actual_mv(&battery_mv) == ESP_OK) {
        s_last_battery_level = battery_adc_percent_from_mv(battery_mv);
    }

    if (s_battery_task == NULL) {
        BaseType_t task_ret = xTaskCreate(battery_notify_task, "battery_notify", BLE_HID_BATTERY_TASK_STACK, NULL, 5, &s_battery_task);
        if (task_ret != pdPASS) {
            return ESP_ERR_NO_MEM;
        }
    }

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
    return ble_hid_send_key_combo(0, keycode);
}

esp_err_t ble_hid_send_key_combo(uint8_t modifier, uint8_t keycode)
{
    ESP_RETURN_ON_ERROR(ble_hid_keyboard_press(modifier, keycode), TAG, "key press failed");
    vTaskDelay(pdMS_TO_TICKS(20));
    return ble_hid_keyboard_release();
}

esp_err_t ble_hid_keyboard_press(uint8_t modifier, uint8_t keycode)
{
    if (!s_connected || s_hid_dev == NULL) {
        return ESP_OK;
    }

    uint8_t report[8] = {0};
    report[0] = modifier;
    report[2] = keycode;

    ESP_LOGI(TAG, "keyboard modifier=0x%02x keycode=0x%02x", modifier, keycode);
    return esp_hidd_dev_input_set(s_hid_dev, BLE_HID_REPORT_MAP_INDEX, BLE_HID_KEYBOARD_REPORT_ID, report, sizeof(report));
}

esp_err_t ble_hid_keyboard_release(void)
{
    if (!s_connected || s_hid_dev == NULL) {
        return ESP_OK;
    }

    uint8_t report[8] = {0};
    return esp_hidd_dev_input_set(s_hid_dev, BLE_HID_REPORT_MAP_INDEX, BLE_HID_KEYBOARD_REPORT_ID, report, sizeof(report));
}

esp_err_t ble_hid_send_key(uint8_t keycode)
{
    if (keycode == 0) {
        return ESP_OK;
    }

    return send_keyboard_report(keycode);
}

esp_err_t ble_hid_send_consumer(uint16_t usage)
{
    if (usage == 0 || !s_connected || s_hid_dev == NULL) {
        return ESP_OK;
    }

    uint8_t report[2] = {
        (uint8_t)(usage & 0xFF),
        (uint8_t)(usage >> 8),
    };

    ESP_LOGI(TAG, "consumer usage=0x%04x", usage);
    ESP_RETURN_ON_ERROR(esp_hidd_dev_input_set(s_hid_dev, BLE_HID_REPORT_MAP_INDEX, BLE_HID_CONSUMER_REPORT_ID,
                                               report, sizeof(report)),
                        TAG, "consumer press failed");

    vTaskDelay(pdMS_TO_TICKS(20));
    memset(report, 0, sizeof(report));
    return esp_hidd_dev_input_set(s_hid_dev, BLE_HID_REPORT_MAP_INDEX, BLE_HID_CONSUMER_REPORT_ID,
                                  report, sizeof(report));
}

esp_err_t ble_hid_disconnect(void)
{
    if (!s_connected) {
        s_allow_auto_advertise = false;
        s_pairing_discoverable = false;
        s_slot_connect_pending = false;
        stop_advertising_now();
        ESP_LOGI(TAG, "disconnect ignored (not connected)");
        return ESP_OK;
    }

    s_allow_auto_advertise = false;
    s_pairing_discoverable = false;
    s_slot_connect_pending = false;
    esp_err_t ret = esp_ble_gap_disconnect(s_connected_bda);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "disconnect failed: %s", esp_err_to_name(ret));
        return ret;
    }

    s_connected = false;
    s_connected_addr[0] = '\0';
    s_next_battery_notify_tick = 0;
    memset(s_connected_bda, 0, sizeof(s_connected_bda));
    ESP_LOGI(TAG, "disconnected by user");
    return ESP_OK;
}

esp_err_t ble_hid_enable_connection(void)
{
    return ble_hid_connect_selected_device();
}

esp_err_t ble_hid_select_device_slot(uint8_t slot)
{
    if (slot >= BLE_HID_DEVICE_SLOT_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    s_selected_device_slot = slot;
    save_selected_slot();
    ESP_LOGI(TAG, "selected device slot %u", (unsigned)(slot + 1));
    return ESP_OK;
}

uint8_t ble_hid_get_selected_device_slot(void)
{
    return s_selected_device_slot;
}

bool ble_hid_get_device_slot(uint8_t slot, ble_hid_device_slot_t *out)
{
    if (slot >= BLE_HID_DEVICE_SLOT_COUNT || out == NULL) {
        return false;
    }

    *out = s_device_slots[slot];
    return s_device_slots[slot].valid;
}

bool ble_hid_is_pairing_mode(void)
{
    return s_pairing_discoverable;
}

esp_err_t ble_hid_connect_selected_device(void)
{
    s_allow_auto_advertise = true;
    s_pairing_discoverable = false;
    s_slot_connect_pending = false;
    stop_advertising_now();

    if (s_connected) {
        ESP_LOGI(TAG, "connection already active");
        return ESP_OK;
    }

    if (!has_bond_device()) {
        ESP_LOGI(TAG, "no bonded device to connect");
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(configure_discoverable(false), TAG, "config connection advertising failed");
    s_slot_connect_pending = true;
    start_advertising_if_ready();
    ESP_LOGI(TAG, "connection advertising for bonded device");
    return ESP_OK;
}

esp_err_t ble_hid_enter_pairing_mode(void)
{
    if (s_connected) {
        ESP_RETURN_ON_ERROR(ble_hid_disconnect(), TAG, "disconnect before pairing failed");
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    int dev_num = esp_ble_get_bond_device_num();
    if (dev_num > 0) {
        esp_ble_bond_dev_t bonds[dev_num];
        ESP_RETURN_ON_ERROR(esp_ble_get_bond_device_list(&dev_num, bonds), TAG, "get bond list failed");
        for (int i = 0; i < dev_num; i++) {
            esp_ble_remove_bond_device(bonds[i].bd_addr);
        }
    }
    memset(s_device_slots, 0, sizeof(s_device_slots));
    save_device_slot(0);

    s_pairing_discoverable = true;
    s_allow_auto_advertise = true;
    s_slot_connect_pending = false;
    stop_advertising_now();
    ESP_RETURN_ON_ERROR(configure_discoverable(true), TAG, "config pairing advertising failed");
    start_advertising_if_ready();
    ESP_LOGI(TAG, "pairing mode enabled");
    return ESP_OK;
}

esp_err_t ble_hid_pair_selected_device(void)
{
    return ble_hid_enter_pairing_mode();
}

esp_err_t ble_hid_clear_pairing(void)
{
    int dev_num = esp_ble_get_bond_device_num();
    ESP_LOGI(TAG, "clear pairing, bonded devices=%d", dev_num);

    s_allow_auto_advertise = false;
    s_pairing_discoverable = false;
    s_slot_connect_pending = false;
    if (s_connected && ble_hid_disconnect() != ESP_OK) {
        ESP_LOGW(TAG, "disconnect before clear failed");
    }

    if (dev_num > 0) {
        esp_ble_bond_dev_t bonds[dev_num];
        ESP_RETURN_ON_ERROR(esp_ble_get_bond_device_list(&dev_num, bonds), TAG, "get bond list failed");

        for (int i = 0; i < dev_num; i++) {
            esp_err_t ret = esp_ble_remove_bond_device(bonds[i].bd_addr);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "remove bond[%d] failed: %s", i, esp_err_to_name(ret));
            }
        }
    }

    stop_advertising_now();
    memset(s_device_slots, 0, sizeof(s_device_slots));
    s_selected_device_slot = 0;
    save_selected_slot();
    for (uint8_t i = 0; i < BLE_HID_DEVICE_SLOT_COUNT; i++) {
        save_device_slot(i);
    }
    ESP_LOGI(TAG, "pairing memory cleared");
    return ESP_OK;
}

static esp_err_t send_mouse_report(uint8_t buttons, int8_t dx, int8_t dy, int8_t wheel)
{
    if (!s_connected || s_hid_dev == NULL) {
        ESP_LOGW(TAG, "mouse report dropped (not connected)");
        return ESP_OK;
    }

    uint8_t report[4] = {buttons, (uint8_t)dx, (uint8_t)dy, (uint8_t)wheel};

    ESP_LOGI(TAG, "mouse buttons=%u dx=%d dy=%d wheel=%d", buttons, dx, dy, wheel);
    esp_err_t ret = esp_hidd_dev_input_set(s_hid_dev, BLE_HID_REPORT_MAP_INDEX, BLE_HID_MOUSE_REPORT_ID, report, sizeof(report));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "mouse report failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t ble_hid_drag_vertical(bool upward)
{
    int8_t wheel = upward ? -1 : 1;

    ESP_LOGI(TAG, "scroll %s start (%d steps, wheel=%d)", upward ? "up" : "down", BLE_HID_DRAG_STEPS, wheel);

    // Define motion pipeline: corner → center → scroll → reset
    const mouse_step_t steps[] = {
        // ① fly to top-left corner
        {MOUSE_STEP_MOVE,   0, -100, -100, 0,  10, 0},
        {MOUSE_STEP_RESET,  0,  0,   0,   0,   1, 30},
        // ② move toward center
        {MOUSE_STEP_MOVE,   0,  0,   60,  0,   1, 0},
        {MOUSE_STEP_RESET,  0,  0,   0,   0,   1, 30},
        // ③ scroll
        {MOUSE_STEP_SCROLL, 0,  0,   0,   wheel, BLE_HID_DRAG_STEPS, 0},
        // ④ reset
        {MOUSE_STEP_RESET,  0,  0,   0,   0,   1, 0},
    };

    return ble_hid_mouse_exec(steps, sizeof(steps) / sizeof(steps[0]));
}

esp_err_t ble_hid_mouse_exec(const mouse_step_t *steps, size_t count)
{
    if (!s_connected || s_hid_dev == NULL) {
        ESP_LOGW(TAG, "mouse exec dropped (not connected)");
        return ESP_OK;
    }

    for (size_t i = 0; i < count; i++) {
        const mouse_step_t *s = &steps[i];

        switch (s->type) {
        case MOUSE_STEP_MOVE:
            for (uint8_t r = 0; r < s->repeat; r++) {
                ESP_RETURN_ON_ERROR(send_mouse_report(s->buttons, s->dx, s->dy, 0),
                                    TAG, "move step[%u] failed", (unsigned)i);
                vTaskDelay(pdMS_TO_TICKS(10));
            }
            break;

        case MOUSE_STEP_SCROLL:
            for (uint8_t r = 0; r < s->repeat; r++) {
                ESP_RETURN_ON_ERROR(send_mouse_report(s->buttons, 0, 0, s->wheel),
                                    TAG, "scroll step[%u] failed", (unsigned)i);
                vTaskDelay(pdMS_TO_TICKS(10));
            }
            break;

        case MOUSE_STEP_RESET:
            ESP_RETURN_ON_ERROR(send_mouse_report(0, 0, 0, 0),
                                TAG, "reset step[%u] failed", (unsigned)i);
            break;

        case MOUSE_STEP_DELAY:
            vTaskDelay(pdMS_TO_TICKS(s->delay_ms));
            break;

        default:
            break;
        }

        if (s->delay_ms > 0) {
            vTaskDelay(pdMS_TO_TICKS(s->delay_ms));
        }
    }

    return ESP_OK;
}
