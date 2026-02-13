#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "port.h"
#include "port_registry.h"
#include "port_cdc.h"
#include "port_uart.h"
#include "route.h"
#include "signal_router.h"
#include "config_store.h"
#include "wifi_mgr.h"
#include "port_tcp.h"
#include "status_led.h"

static const char *TAG = "main";

// RGB LED GPIO - adjust for your board
#define STATUS_LED_GPIO     GPIO_NUM_48

static system_config_t sys_config;

void app_main(void)
{
    ESP_LOGI(TAG, "ESP32 Virtual UART starting...");

    // 1. Init status LED first (visual feedback during boot)
    status_led_init(STATUS_LED_GPIO);
    status_led_set_state(LED_STATE_BOOTING);

    // 2. Init NVS flash
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS flash needs erase, reformatting...");
        nvs_flash_erase();
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS flash init failed: %s", esp_err_to_name(ret));
        status_led_set_state(LED_STATE_ERROR);
        return;
    }

    // 3. Load config
    config_store_init();
    config_store_load(&sys_config);

    // 4. Init port registry
    ret = port_registry_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Port registry init failed");
        status_led_set_state(LED_STATE_ERROR);
        return;
    }

    // 5. Init CDC ports (USB virtual COM ports) - IDs 0, 1
    ret = port_cdc_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "CDC init failed: %s", esp_err_to_name(ret));
        status_led_set_state(LED_STATE_ERROR);
        return;
    }

    // 6. Init UART ports - IDs 2, 3
    for (int i = 0; i < 2; i++) {
        uart_pin_config_t pin_cfg = {
            .uart_num = sys_config.uart_configs[i].uart_num,
            .tx_pin   = sys_config.uart_configs[i].tx_pin,
            .rx_pin   = sys_config.uart_configs[i].rx_pin,
            .rts_pin  = sys_config.uart_configs[i].rts_pin,
            .cts_pin  = sys_config.uart_configs[i].cts_pin,
            .dtr_pin  = sys_config.uart_configs[i].dtr_pin,
            .dsr_pin  = sys_config.uart_configs[i].dsr_pin,
            .dcd_pin  = sys_config.uart_configs[i].dcd_pin,
            .ri_pin   = sys_config.uart_configs[i].ri_pin,
        };
        ret = port_uart_init(2 + i, &pin_cfg);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "UART%d init failed: %s (continuing)", pin_cfg.uart_num, esp_err_to_name(ret));
        }
    }

    // 7. Init WiFi
    // If STA credentials exist: tries STA, falls back to AP after retries
    // If no credentials: starts AP mode immediately ("VirtualUART" open network)
    status_led_set_state(LED_STATE_WIFI_CONNECTING);
    wifi_mgr_init(
        strlen(sys_config.wifi_ssid) > 0 ? sys_config.wifi_ssid : NULL,
        strlen(sys_config.wifi_pass) > 0 ? sys_config.wifi_pass : NULL
    );

    // 8. Init TCP ports - IDs 4-7
    for (int i = 0; i < 4; i++) {
        if (sys_config.tcp_configs[i].port > 0) {
            tcp_port_config_t tcp_cfg = {
                .tcp_port = sys_config.tcp_configs[i].port,
                .is_server = sys_config.tcp_configs[i].is_server,
            };
            strncpy(tcp_cfg.host, sys_config.tcp_configs[i].host, sizeof(tcp_cfg.host) - 1);
            port_tcp_init(4 + i, &tcp_cfg);
        }
    }

    // 9. Init routing engine
    ret = route_engine_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Route engine init failed");
        status_led_set_state(LED_STATE_ERROR);
        return;
    }

    // 10. Start signal router
    signal_router_init();

    // 11. Restore saved routes
    for (int i = 0; i < sys_config.route_count && i < ROUTE_MAX_COUNT; i++) {
        route_t r = {0};
        r.type = sys_config.routes[i].type;
        r.src_port_id = sys_config.routes[i].src_port_id;
        r.dst_count = sys_config.routes[i].dst_count;
        memcpy(r.dst_port_ids, sys_config.routes[i].dst_port_ids, sizeof(r.dst_port_ids));
        r.signal_map_count = sys_config.routes[i].signal_map_count;
        memcpy(r.signal_map, sys_config.routes[i].signal_map, sizeof(r.signal_map));

        uint8_t route_id;
        if (route_create(&r, &route_id) == ESP_OK) {
            route_start(route_id);
        }
    }

    // Boot complete
    status_led_set_state(LED_STATE_READY);
    ESP_LOGI(TAG, "ESP32 Virtual UART ready! %d ports, %d routes",
             port_registry_count(), route_active_count());

    // Log registered ports
    port_t *all_ports[PORT_MAX_COUNT];
    int count = port_registry_get_all(all_ports, PORT_MAX_COUNT);
    for (int i = 0; i < count; i++) {
        ESP_LOGI(TAG, "  Port: %s (id=%d, type=%d)",
                 all_ports[i]->name, all_ports[i]->id, all_ports[i]->type);
    }

    // Main loop: monitor state and update LED
    while (1) {
        bool any_cdc_active = false;
        bool any_data_flowing = false;

        for (int i = 0; i < count; i++) {
            if (all_ports[i]->type == PORT_TYPE_CDC && (all_ports[i]->signals & SIGNAL_DTR)) {
                any_cdc_active = true;
            }
        }

        // Check if any routes have data flowing
        route_t active_routes[ROUTE_MAX_COUNT];
        int rcount = route_get_all(active_routes, ROUTE_MAX_COUNT);
        for (int i = 0; i < rcount; i++) {
            if (active_routes[i].bytes_fwd_src_to_dst > 0 || active_routes[i].bytes_fwd_dst_to_src > 0) {
                any_data_flowing = true;
                status_led_set_activity();
                route_reset_counters(active_routes[i].id);
            }
        }

        led_state_t current = status_led_get_state();
        bool wifi_connected = wifi_mgr_is_connected();

        if (current != LED_STATE_ERROR) {
            if (any_data_flowing && wifi_connected) {
                status_led_set_state(LED_STATE_DATA_FLOW_NET);
            } else if (any_data_flowing) {
                status_led_set_state(LED_STATE_DATA_FLOW);
            } else if (wifi_connected && any_cdc_active) {
                status_led_set_state(LED_STATE_WIFI_READY);
            } else if (any_cdc_active) {
                status_led_set_state(LED_STATE_IDLE);
            } else if (strlen(sys_config.wifi_ssid) > 0 && !wifi_connected) {
                status_led_set_state(LED_STATE_WIFI_CONNECTING);
            } else {
                status_led_set_state(LED_STATE_READY);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
