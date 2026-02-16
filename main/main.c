#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "port.h"
#include "port_registry.h"
#include "port_cdc.h"
#include "port_uart.h"
#include "route.h"
#include "signal_router.h"
#include "config_store.h"
#include "wifi_mgr.h"
#include "port_tcp.h"
#include "web_server.h"
#include "dns_server.h"
#include "status_led.h"
#ifdef CONFIG_VUART_ETHERNET_ENABLED
#include "ethernet_mgr.h"
#endif

static const char *TAG = "main";

system_config_t sys_config;

// Restart web server on WiFi mode change (e.g., STA-to-AP fallback)
static void on_wifi_mode_change(wifi_mgr_mode_t new_mode)
{
    ESP_LOGI(TAG, "WiFi mode changed to %d, restarting web server", (int)new_mode);
    web_server_stop();
    esp_err_t ret = web_server_start();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Web server restart failed: %s", esp_err_to_name(ret));
    }

    // Start DNS server in AP mode (captive portal), stop in STA mode
    if (new_mode == WIFI_MGR_MODE_AP) {
        dns_server_start();
    } else {
        dns_server_stop();
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "ESP32-P4 Virtual UART starting...");

    // 1. Init status LED first (visual feedback during boot)
#if CONFIG_VUART_STATUS_LED_GPIO >= 0
    status_led_init(CONFIG_VUART_STATUS_LED_GPIO);
    status_led_set_state(LED_STATE_BOOTING);
#endif

    // 2. Init NVS flash
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS flash needs erase, reformatting...");
        nvs_flash_erase();
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS flash init failed: %s", esp_err_to_name(ret));
#if CONFIG_VUART_STATUS_LED_GPIO >= 0
        status_led_set_state(LED_STATE_ERROR);
#endif
        return;
    }

    // 3. Load config
    config_store_init();
    config_store_load(&sys_config);

    // 4. Init port registry
    ret = port_registry_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Port registry init failed");
#if CONFIG_VUART_STATUS_LED_GPIO >= 0
        status_led_set_state(LED_STATE_ERROR);
#endif
        return;
    }

    // 5. Init CDC ports (USB virtual COM ports) - IDs 0-5
    ret = port_cdc_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "CDC init failed: %s", esp_err_to_name(ret));
#if CONFIG_VUART_STATUS_LED_GPIO >= 0
        status_led_set_state(LED_STATE_ERROR);
#endif
        return;
    }

    // 6. Init UART ports - IDs 6, 7
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
        ret = port_uart_init(6 + i, &pin_cfg);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "UART%d init failed: %s (continuing)", pin_cfg.uart_num, esp_err_to_name(ret));
        }
    }

    // 7. Init WiFi (via ESP32-C6 companion over SDIO)
    // If STA credentials exist: tries STA, falls back to AP after retries
    // If no credentials: starts AP mode immediately ("VirtualUART" open network)
#if CONFIG_VUART_STATUS_LED_GPIO >= 0
    status_led_set_state(LED_STATE_WIFI_CONNECTING);
#endif
    wifi_mgr_init(
        strlen(sys_config.wifi_ssid) > 0 ? sys_config.wifi_ssid : NULL,
        strlen(sys_config.wifi_pass) > 0 ? sys_config.wifi_pass : NULL
    );

    // 8. Init Ethernet (IP101 PHY)
#ifdef CONFIG_VUART_ETHERNET_ENABLED
    ret = ethernet_mgr_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Ethernet init failed: %s (continuing without Ethernet)", esp_err_to_name(ret));
    }
#endif

    // 9. Init TCP ports - IDs 8-11
    for (int i = 0; i < 4; i++) {
        if (sys_config.tcp_configs[i].port > 0) {
            tcp_port_config_t tcp_cfg = {
                .tcp_port = sys_config.tcp_configs[i].port,
                .is_server = sys_config.tcp_configs[i].is_server,
            };
            strncpy(tcp_cfg.host, sys_config.tcp_configs[i].host, sizeof(tcp_cfg.host) - 1);
            port_tcp_init(8 + i, &tcp_cfg);
        }
    }

    // 10. Init routing engine
    ret = route_engine_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Route engine init failed");
#if CONFIG_VUART_STATUS_LED_GPIO >= 0
        status_led_set_state(LED_STATE_ERROR);
#endif
        return;
    }

    // 11. Start signal router
    signal_router_init();

    // 12. Restore saved routes
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

    // 13. Wait for WiFi to be ready before starting web server
    ret = wifi_mgr_wait_ready(30000);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "WiFi not ready, starting web server anyway");
    }

    // 14. Start web server (HTTP + WebSocket + static files)
    ret = web_server_start();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Web server start failed: %s (continuing)", esp_err_to_name(ret));
    }

    // Register callback to restart web server on WiFi mode changes
    wifi_mgr_set_mode_change_cb(on_wifi_mode_change);

    // Start DNS server if already in AP mode (captive portal)
    if (wifi_mgr_get_mode() == WIFI_MGR_MODE_AP) {
        dns_server_start();
    }

    // Boot complete
#if CONFIG_VUART_STATUS_LED_GPIO >= 0
    status_led_set_state(LED_STATE_READY);
#endif
    ESP_LOGI(TAG, "ESP32-P4 Virtual UART ready! %d ports, %d routes",
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
#if CONFIG_VUART_STATUS_LED_GPIO >= 0
                status_led_set_activity();
#endif
                web_server_notify_data_flow(active_routes[i].id,
                    active_routes[i].bytes_fwd_src_to_dst,
                    active_routes[i].bytes_fwd_dst_to_src);
                route_reset_counters(active_routes[i].id);
            }
        }

#if CONFIG_VUART_STATUS_LED_GPIO >= 0
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
#endif

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
