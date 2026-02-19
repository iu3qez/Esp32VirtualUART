#pragma once

#include "port.h"
#include "route.h"
#include "esp_err.h"

#define CONFIG_VERSION      2  // v2: UART defaults changed away from SDIO pins (GPIO 14-19)
#define CONFIG_WIFI_SSID_MAX 33
#define CONFIG_WIFI_PASS_MAX 65

typedef struct {
    char     host[64];
    uint16_t port;
    bool     is_server;
} tcp_persist_config_t;

typedef struct {
    int      uart_num;
    int      tx_pin;
    int      rx_pin;
    int      rts_pin;
    int      cts_pin;
    int      dtr_pin;
    int      dsr_pin;
    int      dcd_pin;
    int      ri_pin;
} uart_persist_config_t;

typedef struct {
    uint8_t                 version;

    // WiFi
    char                    wifi_ssid[CONFIG_WIFI_SSID_MAX];
    char                    wifi_pass[CONFIG_WIFI_PASS_MAX];

    // Per-port line coding
    port_line_coding_t      port_coding[PORT_MAX_COUNT];

    // TCP port configs
    tcp_persist_config_t    tcp_configs[4];

    // UART pin configs
    uart_persist_config_t   uart_configs[2];

    // Routes
    uint8_t                 route_count;
    struct {
        uint8_t             type;
        uint8_t             src_port_id;
        uint8_t             dst_port_ids[ROUTE_MAX_DEST];
        uint8_t             dst_count;
        signal_mapping_t    signal_map[8];
        uint8_t             signal_map_count;
    } routes[ROUTE_MAX_COUNT];
} system_config_t;

// Initialize config store (call after nvs_flash_init)
esp_err_t config_store_init(void);

// Save current system config to NVS
esp_err_t config_store_save(const system_config_t *config);

// Load system config from NVS. Returns default config if none saved or version mismatch.
esp_err_t config_store_load(system_config_t *config);

// Erase stored config and reset to defaults
esp_err_t config_store_reset(void);

// Fill a system_config_t with sensible defaults
void config_store_defaults(system_config_t *config);
