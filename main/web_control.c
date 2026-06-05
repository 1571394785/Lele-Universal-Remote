#include "web_control.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "custom_mode.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"

#define WEB_AP_SSID "自定义设置"
#define WEB_AP_PASSWORD ""
#define WEB_AP_CHANNEL 6
#define DNS_PORT 53
#define WEB_SAVE_BODY_MAX 4096

static const char *TAG = "web_control";

static bool s_netif_ready;
static bool s_wifi_started;
static bool s_dns_running;
static esp_netif_t *s_ap_netif;
static httpd_handle_t s_httpd;
static TaskHandle_t s_dns_task;
static int s_dns_sock = -1;

static const button_key_t WEB_KEYS[] = {
    BUTTON_KEY_UP, BUTTON_KEY_DOWN, BUTTON_KEY_LEFT, BUTTON_KEY_RIGHT,
    BUTTON_KEY_FUNC2, BUTTON_KEY_FUNC3, BUTTON_KEY_FUNC4,
};

static const char * const WEB_KEY_IDS[] = {
    "up", "down", "left", "right", "f2", "f3", "f4",
};

static const char * const WEB_KEY_LABELS[] = {
    "上", "下", "左", "右", "功能二", "功能三", "功能四",
};

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void url_decode_range(char *dst, size_t dst_len, const char *src, size_t src_len)
{
    size_t used = 0;
    size_t pos = 0;
    while (pos < src_len && used + 1 < dst_len) {
        if (src[pos] == '+') {
            dst[used++] = ' ';
            pos++;
        } else if (src[pos] == '%' && pos + 2 < src_len &&
                   isxdigit((unsigned char)src[pos + 1]) &&
                   isxdigit((unsigned char)src[pos + 2])) {
            dst[used++] = (char)((hex_value(src[pos + 1]) << 4) | hex_value(src[pos + 2]));
            pos += 3;
        } else {
            dst[used++] = src[pos++];
        }
    }
    dst[used] = '\0';
}

static bool find_param(const char *body, const char *name, char *value, size_t value_len)
{
    size_t name_len = strlen(name);
    const char *p = body;
    while (p != NULL && *p != '\0') {
        if (strncmp(p, name, name_len) == 0 && p[name_len] == '=') {
            const char *start = p + name_len + 1;
            const char *end = strchr(start, '&');
            size_t len = end != NULL ? (size_t)(end - start) : strlen(start);
            url_decode_range(value, value_len, start, len);
            return true;
        }
        p = strchr(p, '&');
        if (p != NULL) {
            p++;
        }
    }
    value[0] = '\0';
    return false;
}

static void html_attr_escape(char *dst, size_t dst_len, const char *src)
{
    size_t used = 0;
    while (*src != '\0' && used + 1 < dst_len) {
        const char *rep = NULL;
        if (*src == '&') rep = "&amp;";
        else if (*src == '"') rep = "&quot;";
        else if (*src == '<') rep = "&lt;";
        else if (*src == '>') rep = "&gt;";

        if (rep != NULL) {
            size_t rep_len = strlen(rep);
            if (used + rep_len >= dst_len) {
                break;
            }
            memcpy(dst + used, rep, rep_len);
            used += rep_len;
        } else {
            dst[used++] = *src;
        }
        src++;
    }
    dst[used] = '\0';
}

static esp_err_t root_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_sendstr_chunk(req,
        "<!doctype html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no'>"
        "<title>万能遥控器</title><style>"
        "body{margin:0;background:#111;color:#eee;font-family:system-ui,-apple-system,sans-serif}"
        "main{max-width:520px;margin:0 auto;padding:18px}h1{font-size:22px;margin:0 0 14px}"
        ".row{display:flex;align-items:center;gap:10px;margin:10px 0}.row label{width:64px}"
        "input{flex:1;min-width:0;padding:12px;border-radius:8px;border:1px solid #444;background:#1d1d1d;color:#fff;font-size:16px}"
        "button{width:100%;padding:14px;margin-top:16px;border:0;border-radius:8px;background:#2e7dff;color:white;font-size:17px}"
        ".hint{color:#aaa;font-size:13px;line-height:1.5}</style></head><body><main>"
        "<h1>自定义模式</h1><form method='post' action='/save'>");

    char row[800];
    for (size_t i = 0; i < sizeof(WEB_KEYS) / sizeof(WEB_KEYS[0]); i++) {
        char value[CUSTOM_SHORTCUT_MAX_LEN * 5 + 1];
        html_attr_escape(value, sizeof(value), custom_mode_get_shortcut(WEB_KEYS[i]));
        snprintf(row, sizeof(row),
                 "<div class='row'><label>%s</label><input name='%s' maxlength='%d' value='%s' placeholder='ctrl+c'></div>",
                 WEB_KEY_LABELS[i], WEB_KEY_IDS[i], CUSTOM_SHORTCUT_MAX_LEN, value);
        httpd_resp_sendstr_chunk(req, row);
    }

    httpd_resp_sendstr_chunk(req,
        "<button type='submit'>保存</button></form>"
        "<p class='hint'>示例：ctrl+c、ctrl+win、shift+a、enter、left、right、H e l l o。空格请写 space。原始模式保留不可删除，保存后断电不丢失。</p>"
        "</main></body></html>");
    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}

static esp_err_t save_post_handler(httpd_req_t *req)
{
    if (req->content_len <= 0 || req->content_len >= WEB_SAVE_BODY_MAX) {
        httpd_resp_send_err(req, HTTPD_413_CONTENT_TOO_LARGE, "Save data is too large");
        return ESP_FAIL;
    }

    char *body = calloc((size_t)req->content_len + 1, 1);
    if (body == NULL) {
        httpd_resp_send_500(req);
        return ESP_ERR_NO_MEM;
    }

    int remaining = req->content_len;
    int offset = 0;
    while (remaining > 0) {
        int ret = httpd_req_recv(req, body + offset, remaining);
        if (ret <= 0) {
            free(body);
            return ESP_FAIL;
        }
        offset += ret;
        remaining -= ret;
    }
    body[offset] = '\0';

    for (size_t i = 0; i < sizeof(WEB_KEYS) / sizeof(WEB_KEYS[0]); i++) {
        char value[CUSTOM_SHORTCUT_MAX_LEN + 1];
        find_param(body, WEB_KEY_IDS[i], value, sizeof(value));
        custom_mode_set_shortcut(WEB_KEYS[i], value);
    }
    free(body);

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_sendstr(req,
        "<!doctype html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no'>"
        "<meta http-equiv='refresh' content='1;url=/'>"
        "<style>body{font-family:system-ui;background:#111;color:#eee;padding:24px}</style></head>"
        "<body>已保存，正在返回...</body></html>");
    return ESP_OK;
}

static esp_err_t redirect_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static void dns_task(void *arg)
{
    (void)arg;
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        s_dns_task = NULL;
        vTaskDelete(NULL);
        return;
    }
    s_dns_sock = sock;

    struct timeval timeout = {
        .tv_sec = 0,
        .tv_usec = 100000,
    };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    struct sockaddr_in server = {
        .sin_family = AF_INET,
        .sin_port = htons(DNS_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    bind(sock, (struct sockaddr *)&server, sizeof(server));

    uint8_t packet[512];
    while (s_dns_running) {
        struct sockaddr_in from;
        socklen_t from_len = sizeof(from);
        int len = recvfrom(sock, packet, sizeof(packet), 0, (struct sockaddr *)&from, &from_len);
        if (!s_dns_running) {
            break;
        }
        if (len < 12) {
            continue;
        }

        uint8_t response[512];
        memcpy(response, packet, len);
        response[2] = 0x81;
        response[3] = 0x80;
        response[7] = 0x01;
        int pos = len;
        if (pos + 16 <= (int)sizeof(response)) {
            response[pos++] = 0xC0; response[pos++] = 0x0C;
            response[pos++] = 0x00; response[pos++] = 0x01;
            response[pos++] = 0x00; response[pos++] = 0x01;
            response[pos++] = 0x00; response[pos++] = 0x00; response[pos++] = 0x00; response[pos++] = 0x3C;
            response[pos++] = 0x00; response[pos++] = 0x04;
            response[pos++] = 192; response[pos++] = 168; response[pos++] = 4; response[pos++] = 1;
            sendto(sock, response, pos, 0, (struct sockaddr *)&from, from_len);
        }
    }

    closesocket(sock);
    s_dns_sock = -1;
    s_dns_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t web_control_start(void)
{
    if (s_wifi_started) {
        return ESP_OK;
    }

    if (!s_netif_ready) {
        ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif init failed");
        esp_err_t event_ret = esp_event_loop_create_default();
        if (event_ret != ESP_OK && event_ret != ESP_ERR_INVALID_STATE) {
            return event_ret;
        }
        s_ap_netif = esp_netif_create_default_wifi_ap();
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "wifi init failed");
        s_netif_ready = true;
    }

    wifi_config_t wifi_config = {0};
    snprintf((char *)wifi_config.ap.ssid, sizeof(wifi_config.ap.ssid), "%s", WEB_AP_SSID);
    wifi_config.ap.ssid_len = strlen(WEB_AP_SSID);
    wifi_config.ap.channel = WEB_AP_CHANNEL;
    wifi_config.ap.max_connection = 2;
    wifi_config.ap.authmode = WIFI_AUTH_OPEN;

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_AP), TAG, "set ap mode failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &wifi_config), TAG, "ap config failed");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "wifi start failed");
    s_wifi_started = true;

    httpd_config_t http_cfg = HTTPD_DEFAULT_CONFIG();
    http_cfg.uri_match_fn = httpd_uri_match_wildcard;
    ESP_RETURN_ON_ERROR(httpd_start(&s_httpd, &http_cfg), TAG, "http start failed");

    httpd_uri_t root = {.uri = "/", .method = HTTP_GET, .handler = root_get_handler};
    httpd_uri_t save = {.uri = "/save", .method = HTTP_POST, .handler = save_post_handler};
    httpd_uri_t all = {.uri = "/*", .method = HTTP_GET, .handler = redirect_handler};
    httpd_register_uri_handler(s_httpd, &root);
    httpd_register_uri_handler(s_httpd, &save);
    httpd_register_uri_handler(s_httpd, &all);

    s_dns_running = true;
    xTaskCreate(dns_task, "dns_captive", 4096, NULL, 5, &s_dns_task);

    ESP_LOGI(TAG, "web control started: ssid=%s url=http://192.168.4.1", WEB_AP_SSID);
    return ESP_OK;
}

esp_err_t web_control_stop(void)
{
    if (s_httpd != NULL) {
        httpd_stop(s_httpd);
        s_httpd = NULL;
    }

    s_dns_running = false;
    if (s_dns_sock >= 0) {
        shutdown(s_dns_sock, 0);
    }
    vTaskDelay(pdMS_TO_TICKS(100));

    if (s_wifi_started) {
        esp_wifi_stop();
        s_wifi_started = false;
    }

    ESP_LOGI(TAG, "web control stopped");
    return ESP_OK;
}

bool web_control_is_running(void)
{
    return s_wifi_started;
}
