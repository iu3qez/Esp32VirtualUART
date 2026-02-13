#include "port.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "port";

esp_err_t port_init(port_t *port, uint8_t id, const char *name, port_type_t type, const port_ops_t *ops, void *priv)
{
    if (!port || !name || !ops) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(port, 0, sizeof(port_t));
    port->id = id;
    strncpy(port->name, name, PORT_NAME_MAX - 1);
    port->type = type;
    port->state = PORT_STATE_DISABLED;
    port->ops = *ops;
    port->line_coding = port_line_coding_default();
    port->signals = 0;
    port->signal_override = 0;
    port->signal_override_val = 0;
    port->priv = priv;

    port->rx_buf = xStreamBufferCreate(PORT_BUF_SIZE, 1);
    if (!port->rx_buf) {
        ESP_LOGE(TAG, "Failed to create stream buffer for port %s", name);
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Port %s (id=%d, type=%d) initialized", name, id, type);
    return ESP_OK;
}

esp_err_t port_open(port_t *port)
{
    if (!port || !port->ops.open) {
        return ESP_ERR_INVALID_ARG;
    }
    int ret = port->ops.open(port);
    if (ret == 0) {
        port->state = PORT_STATE_READY;
    }
    return ret == 0 ? ESP_OK : ESP_FAIL;
}

void port_close(port_t *port)
{
    if (port && port->ops.close) {
        port->ops.close(port);
        port->state = PORT_STATE_DISABLED;
    }
}

uint32_t port_get_effective_signals(port_t *port)
{
    if (!port) return 0;

    uint32_t hw_signals = port->signals;
    // Apply overrides: for each bit set in signal_override, use the value from signal_override_val
    uint32_t result = (hw_signals & ~port->signal_override) | (port->signal_override_val & port->signal_override);
    return result;
}
