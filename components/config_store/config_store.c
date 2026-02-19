#include "config_store.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "config_store";

#define NVS_NAMESPACE   "vuart_cfg"
#define NVS_KEY_CONFIG  "config"

void config_store_defaults(system_config_t *config)
{
    memset(config, 0, sizeof(system_config_t));
    config->version = CONFIG_VERSION;

    // Default line coding for all ports: 115200 8N1
    for (int i = 0; i < PORT_MAX_COUNT; i++) {
        config->port_coding[i] = port_line_coding_default();
    }

    // Default UART1 pins - unassigned (-1 = UART_PIN_NO_CHANGE).
    // IMPORTANT: GPIO 14-19 are used by the ESP-Hosted SDIO link to the C6.
    // Assigning UART TX/RX to any of those GPIOs will break WiFi.
    // Configure actual board pins via the web UI after first boot.
    config->uart_configs[0].uart_num = 1;
    config->uart_configs[0].tx_pin = -1;
    config->uart_configs[0].rx_pin = -1;
    config->uart_configs[0].rts_pin = -1;
    config->uart_configs[0].cts_pin = -1;
    config->uart_configs[0].dtr_pin = -1;
    config->uart_configs[0].dsr_pin = -1;
    config->uart_configs[0].dcd_pin = -1;
    config->uart_configs[0].ri_pin = -1;

    // Default UART2 pins - unassigned. Same reasoning as UART1.
    config->uart_configs[1].uart_num = 2;
    config->uart_configs[1].tx_pin = -1;
    config->uart_configs[1].rx_pin = -1;
    config->uart_configs[1].rts_pin = -1;
    config->uart_configs[1].cts_pin = -1;
    config->uart_configs[1].dtr_pin = -1;
    config->uart_configs[1].dsr_pin = -1;
    config->uart_configs[1].dcd_pin = -1;
    config->uart_configs[1].ri_pin = -1;
}

esp_err_t config_store_init(void)
{
    ESP_LOGI(TAG, "Config store initialized");
    return ESP_OK;
}

esp_err_t config_store_save(const system_config_t *config)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = nvs_set_blob(handle, NVS_KEY_CONFIG, config, sizeof(system_config_t));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nvs_set_blob failed: %s", esp_err_to_name(ret));
        nvs_close(handle);
        return ret;
    }

    ret = nvs_commit(handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nvs_commit failed: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Config saved (%d bytes, %d routes)",
                 (int)sizeof(system_config_t), config->route_count);
    }

    nvs_close(handle);
    return ret;
}

esp_err_t config_store_load(system_config_t *config)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "No saved config, using defaults");
        config_store_defaults(config);
        return ESP_OK;
    }
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open failed: %s, using defaults", esp_err_to_name(ret));
        config_store_defaults(config);
        return ESP_OK;
    }

    size_t size = sizeof(system_config_t);
    ret = nvs_get_blob(handle, NVS_KEY_CONFIG, config, &size);
    nvs_close(handle);

    if (ret != ESP_OK || size != sizeof(system_config_t)) {
        ESP_LOGW(TAG, "Config blob read failed or size mismatch, using defaults");
        config_store_defaults(config);
        return ESP_OK;
    }

    if (config->version != CONFIG_VERSION) {
        ESP_LOGW(TAG, "Config version mismatch (stored=%d, expected=%d), using defaults",
                 config->version, CONFIG_VERSION);
        config_store_defaults(config);
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Config loaded (%d routes)", config->route_count);
    return ESP_OK;
}

esp_err_t config_store_reset(void)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = nvs_erase_all(handle);
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
        ESP_LOGI(TAG, "Config reset to defaults");
    }

    nvs_close(handle);
    return ret;
}
