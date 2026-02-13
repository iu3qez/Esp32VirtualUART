#include "port_registry.h"
#include "esp_log.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "port_reg";

static port_t *ports[PORT_MAX_COUNT];
static int port_count = 0;
static SemaphoreHandle_t registry_mutex;

esp_err_t port_registry_init(void)
{
    registry_mutex = xSemaphoreCreateMutex();
    if (!registry_mutex) {
        ESP_LOGE(TAG, "Failed to create registry mutex");
        return ESP_ERR_NO_MEM;
    }
    memset(ports, 0, sizeof(ports));
    port_count = 0;
    ESP_LOGI(TAG, "Port registry initialized (max %d ports)", PORT_MAX_COUNT);
    return ESP_OK;
}

esp_err_t port_registry_add(port_t *port)
{
    if (!port) return ESP_ERR_INVALID_ARG;

    xSemaphoreTake(registry_mutex, portMAX_DELAY);

    if (port_count >= PORT_MAX_COUNT) {
        xSemaphoreGive(registry_mutex);
        ESP_LOGE(TAG, "Registry full, cannot add port %s", port->name);
        return ESP_ERR_NO_MEM;
    }

    // Check for duplicate ID
    for (int i = 0; i < PORT_MAX_COUNT; i++) {
        if (ports[i] && ports[i]->id == port->id) {
            xSemaphoreGive(registry_mutex);
            ESP_LOGE(TAG, "Port id %d already registered", port->id);
            return ESP_ERR_INVALID_STATE;
        }
    }

    // Find empty slot
    for (int i = 0; i < PORT_MAX_COUNT; i++) {
        if (!ports[i]) {
            ports[i] = port;
            port_count++;
            xSemaphoreGive(registry_mutex);
            ESP_LOGI(TAG, "Registered port %s (id=%d) in slot %d", port->name, port->id, i);
            return ESP_OK;
        }
    }

    xSemaphoreGive(registry_mutex);
    return ESP_ERR_NO_MEM;
}

esp_err_t port_registry_remove(uint8_t port_id)
{
    xSemaphoreTake(registry_mutex, portMAX_DELAY);

    for (int i = 0; i < PORT_MAX_COUNT; i++) {
        if (ports[i] && ports[i]->id == port_id) {
            ESP_LOGI(TAG, "Removed port %s (id=%d)", ports[i]->name, port_id);
            ports[i] = NULL;
            port_count--;
            xSemaphoreGive(registry_mutex);
            return ESP_OK;
        }
    }

    xSemaphoreGive(registry_mutex);
    return ESP_ERR_NOT_FOUND;
}

port_t *port_registry_get(uint8_t port_id)
{
    xSemaphoreTake(registry_mutex, portMAX_DELAY);

    for (int i = 0; i < PORT_MAX_COUNT; i++) {
        if (ports[i] && ports[i]->id == port_id) {
            xSemaphoreGive(registry_mutex);
            return ports[i];
        }
    }

    xSemaphoreGive(registry_mutex);
    return NULL;
}

port_t *port_registry_get_by_name(const char *name)
{
    if (!name) return NULL;

    xSemaphoreTake(registry_mutex, portMAX_DELAY);

    for (int i = 0; i < PORT_MAX_COUNT; i++) {
        if (ports[i] && strcmp(ports[i]->name, name) == 0) {
            xSemaphoreGive(registry_mutex);
            return ports[i];
        }
    }

    xSemaphoreGive(registry_mutex);
    return NULL;
}

int port_registry_get_all(port_t **out, int max_count)
{
    xSemaphoreTake(registry_mutex, portMAX_DELAY);

    int count = 0;
    for (int i = 0; i < PORT_MAX_COUNT && count < max_count; i++) {
        if (ports[i]) {
            out[count++] = ports[i];
        }
    }

    xSemaphoreGive(registry_mutex);
    return count;
}

int port_registry_count(void)
{
    return port_count;
}
