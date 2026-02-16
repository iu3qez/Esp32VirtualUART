idf_component_register(
    SRCS "wifi_mgr.c"
    INCLUDE_DIRS "include"
    REQUIRES freertos log esp_wifi esp_event esp_netif esp-extconn
)
