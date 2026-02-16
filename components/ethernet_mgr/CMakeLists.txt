idf_component_register(
    SRCS "ethernet_mgr.c"
    INCLUDE_DIRS "include"
    REQUIRES freertos log esp_eth esp_event esp_netif driver
)
