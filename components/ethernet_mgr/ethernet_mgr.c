#include "ethernet_mgr.h"
#include "esp_eth.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include <string.h>

static const char *TAG = "ethernet_mgr";

// Guition JC-ESP32P4-M3-Dev board pin configuration
#define ETH_MDC_GPIO        GPIO_NUM_31
#define ETH_MDIO_GPIO       GPIO_NUM_52
#define ETH_PHY_POWER_GPIO  GPIO_NUM_51
#define ETH_CLK_EXT_IN_GPIO GPIO_NUM_50
#define ETH_PHY_ADDR        1

static esp_eth_handle_t eth_handle = NULL;
static bool eth_connected = false;
static char eth_ip_str[16] = "";

static void eth_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    switch (event_id) {
    case ETHERNET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "Ethernet link up");
        break;
    case ETHERNET_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "Ethernet link down");
        eth_connected = false;
        eth_ip_str[0] = '\0';
        break;
    case ETHERNET_EVENT_START:
        ESP_LOGI(TAG, "Ethernet started");
        break;
    case ETHERNET_EVENT_STOP:
        ESP_LOGI(TAG, "Ethernet stopped");
        eth_connected = false;
        eth_ip_str[0] = '\0';
        break;
    }
}

static void got_ip_event_handler(void *arg, esp_event_base_t event_base,
                                  int32_t event_id, void *event_data)
{
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    snprintf(eth_ip_str, sizeof(eth_ip_str), IPSTR, IP2STR(&event->ip_info.ip));
    ESP_LOGI(TAG, "Ethernet got IP: %s", eth_ip_str);
    eth_connected = true;
}

esp_err_t ethernet_mgr_init(void)
{
    ESP_LOGI(TAG, "Initializing Ethernet (IP101 PHY)");

    // Power on the PHY
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << ETH_PHY_POWER_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    gpio_set_level(ETH_PHY_POWER_GPIO, 1);

    // Create default esp_netif for Ethernet
    esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_ETH();
    esp_netif_t *eth_netif = esp_netif_new(&netif_cfg);

    // Configure EMAC
    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    eth_esp32_emac_config_t esp32_emac_config = ETH_ESP32_EMAC_DEFAULT_CONFIG();
    esp32_emac_config.smi_gpio.mdc_num = ETH_MDC_GPIO;
    esp32_emac_config.smi_gpio.mdio_num = ETH_MDIO_GPIO;
    esp32_emac_config.clock_config.rmii.clock_mode = EMAC_CLK_EXT_IN;
    esp32_emac_config.clock_config.rmii.clock_gpio = ETH_CLK_EXT_IN_GPIO;

    esp_eth_mac_t *mac = esp_eth_mac_new_esp32(&esp32_emac_config, &mac_config);
    if (!mac) {
        ESP_LOGE(TAG, "Failed to create EMAC");
        return ESP_FAIL;
    }

    // Configure IP101 PHY
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    phy_config.phy_addr = ETH_PHY_ADDR;
    phy_config.reset_gpio_num = -1;  // No separate reset pin; using power pin

    esp_eth_phy_t *phy = esp_eth_phy_new_ip101(&phy_config);
    if (!phy) {
        ESP_LOGE(TAG, "Failed to create IP101 PHY");
        return ESP_FAIL;
    }

    // Install Ethernet driver
    esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(mac, phy);
    esp_err_t ret = esp_eth_driver_install(&eth_config, &eth_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Ethernet driver install failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Attach Ethernet driver to TCP/IP stack
    esp_netif_attach(eth_netif, esp_eth_new_netif_glue(eth_handle));

    // Register event handlers
    esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, eth_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, got_ip_event_handler, NULL);

    // Start Ethernet
    ret = esp_eth_start(eth_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Ethernet start failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Ethernet initialized (MDC=%d, MDIO=%d, PHY addr=%d)",
             ETH_MDC_GPIO, ETH_MDIO_GPIO, ETH_PHY_ADDR);
    return ESP_OK;
}

bool ethernet_mgr_is_connected(void)
{
    return eth_connected;
}

const char *ethernet_mgr_get_ip(void)
{
    return eth_ip_str;
}
