#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "port.h"
#include "port_registry.h"
#include "port_cdc.h"
#include "status_led.h"

static const char *TAG = "main";

// RGB LED GPIO - adjust for your board
// ESP32-S3-DevKitC: GPIO48
// Some boards: GPIO38
#define STATUS_LED_GPIO     GPIO_NUM_48

void app_main(void)
{
    ESP_LOGI(TAG, "ESP32 Virtual UART starting...");

    // 1. Init status LED first (visual feedback during boot)
    status_led_init(STATUS_LED_GPIO);
    status_led_set_state(LED_STATE_BOOTING);

    // 2. Init NVS flash (needed for config persistence later)
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

    // 3. Init port registry
    ret = port_registry_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Port registry init failed: %s", esp_err_to_name(ret));
        status_led_set_state(LED_STATE_ERROR);
        return;
    }

    // 4. Init CDC ports (USB virtual COM ports)
    ret = port_cdc_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "CDC init failed: %s", esp_err_to_name(ret));
        status_led_set_state(LED_STATE_ERROR);
        return;
    }

    // Boot complete - show ready state
    status_led_set_state(LED_STATE_READY);
    ESP_LOGI(TAG, "ESP32 Virtual UART ready! %d ports registered.",
             port_registry_count());

    // Log registered ports
    port_t *all_ports[PORT_MAX_COUNT];
    int count = port_registry_get_all(all_ports, PORT_MAX_COUNT);
    for (int i = 0; i < count; i++) {
        ESP_LOGI(TAG, "  Port: %s (id=%d, type=%d, state=%d)",
                 all_ports[i]->name, all_ports[i]->id,
                 all_ports[i]->type, all_ports[i]->state);
    }

    // Main loop: monitor port states and update LED
    while (1) {
        // Check if any CDC port has DTR set (host connected and opened port)
        bool any_active = false;
        for (int i = 0; i < count; i++) {
            if (all_ports[i]->type == PORT_TYPE_CDC && (all_ports[i]->signals & SIGNAL_DTR)) {
                any_active = true;
                break;
            }
        }

        led_state_t current = status_led_get_state();
        if (any_active && current == LED_STATE_READY) {
            status_led_set_state(LED_STATE_IDLE);
        } else if (!any_active && current == LED_STATE_IDLE) {
            status_led_set_state(LED_STATE_READY);
        }

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
