#pragma once

#include "esp_err.h"
#include <stdbool.h>

typedef enum {
    WIFI_MGR_MODE_NONE = 0, // WiFi not initialized
    WIFI_MGR_MODE_STA,      // Connected to an AP
    WIFI_MGR_MODE_AP,       // Running as AP (captive portal / config mode)
} wifi_mgr_mode_t;

// Initialize WiFi.
// - If ssid is provided and non-empty: tries STA mode first.
//   If STA fails after retries, falls back to AP mode.
// - If ssid is NULL/empty: starts in AP mode immediately.
// AP mode creates network "VirtualUART" (open) on 192.168.4.1
esp_err_t wifi_mgr_init(const char *ssid, const char *password);

// Disconnect and deinit WiFi
void wifi_mgr_stop(void);

// Check if WiFi is connected (STA) or active (AP)
bool wifi_mgr_is_connected(void);

// Get the IP address as string
// STA mode: assigned IP (e.g., "192.168.1.42")
// AP mode: always "192.168.4.1"
const char *wifi_mgr_get_ip(void);

// Get current WiFi mode
wifi_mgr_mode_t wifi_mgr_get_mode(void);

// Update STA credentials and switch from AP to STA mode
esp_err_t wifi_mgr_set_credentials(const char *ssid, const char *password);

// Force AP mode (e.g., from web GUI button)
esp_err_t wifi_mgr_start_ap(void);

// Block until WiFi is ready (STA connected or AP started).
// Returns ESP_OK if ready, ESP_ERR_TIMEOUT if timeout_ms elapsed.
esp_err_t wifi_mgr_wait_ready(uint32_t timeout_ms);

// Callback type for WiFi mode changes (called on AP start, STA got IP, STA-to-AP fallback)
typedef void (*wifi_mgr_mode_change_cb_t)(wifi_mgr_mode_t new_mode);

// Register a callback for WiFi mode changes
void wifi_mgr_set_mode_change_cb(wifi_mgr_mode_change_cb_t cb);
