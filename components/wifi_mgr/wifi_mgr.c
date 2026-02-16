#include "wifi_mgr.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "esp_extconn.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include <string.h>

static const char *TAG = "wifi_mgr";

#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_AP_STARTED_BIT BIT1
#define WIFI_STA_RETRY_MAX  5

#define AP_SSID             "VirtualUART"
#define AP_CHANNEL          1
#define AP_MAX_CONN         2
#define AP_IP               "192.168.4.1"

static EventGroupHandle_t wifi_event_group;
static esp_netif_t *sta_netif = NULL;
static esp_netif_t *ap_netif = NULL;
static wifi_mgr_mode_t current_mode = WIFI_MGR_MODE_NONE;
static bool wifi_initialized = false;
static int sta_retry_count = 0;
static char ip_str[16] = "";

// Saved STA credentials for retry/fallback
static char saved_ssid[33] = "";
static char saved_pass[65] = "";
static wifi_mgr_mode_change_cb_t mode_change_cb = NULL;

static void start_ap_mode(void);

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
        case WIFI_EVENT_STA_START:
            ESP_LOGI(TAG, "STA started, connecting...");
            esp_wifi_connect();
            break;

        case WIFI_EVENT_STA_DISCONNECTED:
            xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
            ip_str[0] = '\0';
            sta_retry_count++;

            if (sta_retry_count <= WIFI_STA_RETRY_MAX) {
                ESP_LOGI(TAG, "STA disconnected, retry %d/%d", sta_retry_count, WIFI_STA_RETRY_MAX);
                vTaskDelay(pdMS_TO_TICKS(1000 * sta_retry_count));
                esp_wifi_connect();
            } else {
                ESP_LOGW(TAG, "STA failed after %d retries, switching to AP mode", WIFI_STA_RETRY_MAX);
                start_ap_mode();
            }
            break;

        case WIFI_EVENT_STA_CONNECTED:
            ESP_LOGI(TAG, "STA connected to AP");
            sta_retry_count = 0;
            break;

        case WIFI_EVENT_AP_START:
            ESP_LOGI(TAG, "AP started: SSID=%s", AP_SSID);
            xEventGroupSetBits(wifi_event_group, WIFI_AP_STARTED_BIT);
            current_mode = WIFI_MGR_MODE_AP;
            strncpy(ip_str, AP_IP, sizeof(ip_str));
            if (mode_change_cb) mode_change_cb(WIFI_MGR_MODE_AP);
            break;

        case WIFI_EVENT_AP_STACONNECTED: {
            wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
            ESP_LOGI(TAG, "AP: client connected (AID=%d)", event->aid);
            break;
        }

        case WIFI_EVENT_AP_STADISCONNECTED: {
            wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
            ESP_LOGI(TAG, "AP: client disconnected (AID=%d)", event->aid);
            break;
        }
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "STA got IP: %s", ip_str);
        current_mode = WIFI_MGR_MODE_STA;
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
        if (mode_change_cb) mode_change_cb(WIFI_MGR_MODE_STA);
    }
}

static void start_ap_mode(void)
{
    ESP_LOGI(TAG, "Starting AP mode: SSID=%s (open)", AP_SSID);

    // Stop STA if running
    esp_wifi_disconnect();
    esp_wifi_stop();

    // Create AP netif if needed
    if (!ap_netif) {
        ap_netif = esp_netif_create_default_wifi_ap();
    }

    wifi_config_t ap_config = {
        .ap = {
            .ssid = AP_SSID,
            .ssid_len = strlen(AP_SSID),
            .channel = AP_CHANNEL,
            .authmode = WIFI_AUTH_OPEN,
            .max_connection = AP_MAX_CONN,
        },
    };

    esp_wifi_set_mode(WIFI_MODE_AP);
    esp_wifi_set_config(WIFI_IF_AP, &ap_config);
    esp_wifi_start();
}

static esp_err_t init_wifi_common(void)
{
    if (wifi_initialized) return ESP_OK;

    wifi_event_group = xEventGroupCreate();
    if (!wifi_event_group) return ESP_ERR_NO_MEM;

    esp_err_t ret = esp_netif_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) return ret;

    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) return ret;

    // Initialize external connection to ESP32-C6 companion chip (SDIO)
    // This must be called before esp_wifi_init() on ESP32-P4
    esp_extconn_config_t extconn_cfg = ESP_EXTCONN_CONFIG_DEFAULT();
    ret = esp_extconn_init(&extconn_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_extconn_init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL);

    wifi_initialized = true;
    return ESP_OK;
}

esp_err_t wifi_mgr_init(const char *ssid, const char *password)
{
    esp_err_t ret = init_wifi_common();
    if (ret != ESP_OK) return ret;

    if (!ssid || strlen(ssid) == 0) {
        // No STA credentials: start in AP mode immediately
        ESP_LOGI(TAG, "No WiFi credentials, starting AP mode");
        start_ap_mode();
        return ESP_OK;
    }

    // Save credentials for fallback
    strncpy(saved_ssid, ssid, sizeof(saved_ssid) - 1);
    if (password) {
        strncpy(saved_pass, password, sizeof(saved_pass) - 1);
    }

    ESP_LOGI(TAG, "Trying STA mode, SSID: %s", ssid);

    sta_netif = esp_netif_create_default_wifi_sta();

    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    if (password) {
        strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
    }
    wifi_config.sta.threshold.authmode = (password && strlen(password) > 0) ?
                                         WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();

    sta_retry_count = 0;
    return ESP_OK;
}

void wifi_mgr_stop(void)
{
    if (!wifi_initialized) return;

    esp_wifi_disconnect();
    esp_wifi_stop();
    esp_wifi_deinit();

    if (sta_netif) {
        esp_netif_destroy_default_wifi(sta_netif);
        sta_netif = NULL;
    }
    if (ap_netif) {
        esp_netif_destroy_default_wifi(ap_netif);
        ap_netif = NULL;
    }

    wifi_initialized = false;
    current_mode = WIFI_MGR_MODE_NONE;
    ip_str[0] = '\0';
    ESP_LOGI(TAG, "WiFi stopped");
}

bool wifi_mgr_is_connected(void)
{
    if (!wifi_event_group) return false;
    EventBits_t bits = xEventGroupGetBits(wifi_event_group);
    return (bits & (WIFI_CONNECTED_BIT | WIFI_AP_STARTED_BIT)) != 0;
}

const char *wifi_mgr_get_ip(void)
{
    return ip_str;
}

wifi_mgr_mode_t wifi_mgr_get_mode(void)
{
    return current_mode;
}

esp_err_t wifi_mgr_set_credentials(const char *ssid, const char *password)
{
    if (!ssid || strlen(ssid) == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Switching to STA mode, SSID: %s", ssid);

    strncpy(saved_ssid, ssid, sizeof(saved_ssid) - 1);
    saved_pass[0] = '\0';
    if (password) {
        strncpy(saved_pass, password, sizeof(saved_pass) - 1);
    }

    // Stop current mode
    esp_wifi_disconnect();
    esp_wifi_stop();
    sta_retry_count = 0;

    if (!sta_netif) {
        sta_netif = esp_netif_create_default_wifi_sta();
    }

    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    if (password) {
        strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
    }
    wifi_config.sta.threshold.authmode = (password && strlen(password) > 0) ?
                                         WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    return esp_wifi_start();
}

esp_err_t wifi_mgr_start_ap(void)
{
    esp_err_t ret = init_wifi_common();
    if (ret != ESP_OK) return ret;

    start_ap_mode();
    return ESP_OK;
}

esp_err_t wifi_mgr_wait_ready(uint32_t timeout_ms)
{
    if (!wifi_event_group) return ESP_ERR_INVALID_STATE;

    EventBits_t bits = xEventGroupWaitBits(wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_AP_STARTED_BIT,
        pdFALSE, pdFALSE, pdMS_TO_TICKS(timeout_ms));

    if (bits & (WIFI_CONNECTED_BIT | WIFI_AP_STARTED_BIT)) {
        ESP_LOGI(TAG, "WiFi ready (mode=%s)", (bits & WIFI_CONNECTED_BIT) ? "STA" : "AP");
        return ESP_OK;
    }

    ESP_LOGW(TAG, "WiFi not ready after %lu ms", (unsigned long)timeout_ms);
    return ESP_ERR_TIMEOUT;
}

void wifi_mgr_set_mode_change_cb(wifi_mgr_mode_change_cb_t cb)
{
    mode_change_cb = cb;
}
