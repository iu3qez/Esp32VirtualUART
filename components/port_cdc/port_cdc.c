#include "port_cdc.h"
#include "port_registry.h"
#include "tinyusb.h"
#include "tusb_cdc_acm.h"
#include "esp_log.h"
#include <string.h>

#ifdef CONFIG_VUART_CDC_DEBUG_PORT
#include "tusb_console.h"
#endif

static const char *TAG = "port_cdc";

// External descriptors from usb_descriptors.c
extern const tusb_desc_device_t cdc_device_descriptor;
extern const uint8_t cdc_config_descriptor[];
extern const char *cdc_string_descriptor[];

// Private data for each CDC port
typedef struct {
    int cdc_index;  // TinyUSB CDC port index (0 or 1)
} cdc_priv_t;

static port_t cdc_ports[CDC_PORT_COUNT];
static cdc_priv_t cdc_priv[CDC_PORT_COUNT];

// --- Port ops implementation ---

static int cdc_open(port_t *port)
{
    ESP_LOGI(TAG, "CDC port %s opened", port->name);
    port->state = PORT_STATE_ACTIVE;
    return 0;
}

static void cdc_close(port_t *port)
{
    ESP_LOGI(TAG, "CDC port %s closed", port->name);
    port->state = PORT_STATE_DISABLED;
}

static int cdc_read(port_t *port, uint8_t *buf, size_t len, TickType_t timeout)
{
    // Read from the stream buffer (data pushed in by RX callback)
    size_t received = xStreamBufferReceive(port->rx_buf, buf, len, timeout);
    return (int)received;
}

static int cdc_write(port_t *port, const uint8_t *buf, size_t len, TickType_t timeout)
{
    cdc_priv_t *priv = (cdc_priv_t *)port->priv;
    (void)timeout;

    // Write to TinyUSB CDC TX
    size_t written = tinyusb_cdcacm_write_queue(priv->cdc_index, buf, len);
    tinyusb_cdcacm_write_flush(priv->cdc_index, pdMS_TO_TICKS(50));
    return (int)written;
}

static int cdc_get_signals(port_t *port, uint32_t *signals)
{
    *signals = port_get_effective_signals(port);
    return 0;
}

static int cdc_set_signals(port_t *port, uint32_t signals)
{
    // For CDC, we can notify the host of DSR/DCD/RI changes
    // via tud_cdc_n_set_serial_state() (TinyUSB low-level API)
    // For now, just store the signal state
    port->signals = (port->signals & (SIGNAL_DTR | SIGNAL_RTS)) | (signals & ~(SIGNAL_DTR | SIGNAL_RTS));
    return 0;
}

static int cdc_set_line_coding(port_t *port, const port_line_coding_t *coding)
{
    port->line_coding = *coding;
    ESP_LOGI(TAG, "%s: line coding set to %lu baud, %d%c%s",
             port->name, (unsigned long)coding->baud_rate, coding->data_bits,
             "NOEMS"[coding->parity], coding->stop_bits == 0 ? "1" : "2");
    return 0;
}

static int cdc_get_line_coding(port_t *port, port_line_coding_t *coding)
{
    *coding = port->line_coding;
    return 0;
}

static const port_ops_t cdc_ops = {
    .open           = cdc_open,
    .close          = cdc_close,
    .read           = cdc_read,
    .write          = cdc_write,
    .get_signals    = cdc_get_signals,
    .set_signals    = cdc_set_signals,
    .set_line_coding = cdc_set_line_coding,
    .get_line_coding = cdc_get_line_coding,
};

// --- TinyUSB Callbacks ---

static void cdc_rx_callback(int itf, cdcacm_event_t *event)
{
    if (itf < 0 || itf >= CDC_PORT_COUNT) return;

    port_t *port = &cdc_ports[itf];
    uint8_t buf[CONFIG_TINYUSB_CDC_RX_BUFSIZE];
    size_t rx_size = 0;

    esp_err_t ret = tinyusb_cdcacm_read(itf, buf, sizeof(buf), &rx_size);
    if (ret == ESP_OK && rx_size > 0) {
        // Push received data into port's stream buffer
        size_t sent = xStreamBufferSend(port->rx_buf, buf, rx_size, 0);
        if (sent < rx_size) {
            ESP_LOGW(TAG, "%s: rx buffer overflow, dropped %d bytes",
                     port->name, (int)(rx_size - sent));
        }
        port->state = PORT_STATE_ACTIVE;
    }
}

static void cdc_line_state_changed_callback(int itf, cdcacm_event_t *event)
{
    if (itf < 0 || itf >= CDC_PORT_COUNT) return;

    port_t *port = &cdc_ports[itf];
    int dtr = event->line_state_changed_data.dtr;
    int rts = event->line_state_changed_data.rts;

    // Update signal state from host
    uint32_t new_signals = port->signals & ~(SIGNAL_DTR | SIGNAL_RTS);
    if (dtr) new_signals |= SIGNAL_DTR;
    if (rts) new_signals |= SIGNAL_RTS;
    port->signals = new_signals;

    ESP_LOGI(TAG, "%s: line state DTR=%d RTS=%d", port->name, dtr, rts);

    if (dtr) {
        port->state = PORT_STATE_ACTIVE;
    } else {
        port->state = PORT_STATE_READY;
    }
}

static void cdc_line_coding_changed_callback(int itf, cdcacm_event_t *event)
{
    if (itf < 0 || itf >= CDC_PORT_COUNT) return;

    port_t *port = &cdc_ports[itf];
    const cdc_line_coding_t *coding = event->line_coding_changed_data.p_line_coding;

    port->line_coding.baud_rate = coding->bit_rate;
    port->line_coding.data_bits = coding->data_bits;
    port->line_coding.stop_bits = coding->stop_bits;
    port->line_coding.parity = coding->parity;

    ESP_LOGI(TAG, "%s: host set line coding %lu baud %d%c%s",
             port->name, (unsigned long)coding->bit_rate, coding->data_bits,
             "NOEMS"[coding->parity], coding->stop_bits == 0 ? "1" : "2");
}

// --- Public API ---

esp_err_t port_cdc_init(void)
{
    ESP_LOGI(TAG, "Initializing TinyUSB CDC with %d ports", CDC_PORT_COUNT);

    // Install TinyUSB driver with custom descriptors
    const tinyusb_config_t tusb_cfg = {
        .device_descriptor = &cdc_device_descriptor,
        .configuration_descriptor = cdc_config_descriptor,
        .string_descriptor = cdc_string_descriptor,
        .string_descriptor_count = 6,
        .external_phy = false,
        .self_powered = false,
        .vbus_monitor_io = -1,
    };

    esp_err_t ret = tinyusb_driver_install(&tusb_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "TinyUSB driver install failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Initialize each CDC-ACM port
    for (int i = 0; i < CDC_PORT_COUNT; i++) {
#ifdef CONFIG_VUART_CDC_DEBUG_PORT
        // Debug port: init as CDC-ACM but redirect console, don't register for routing
        if (i == CDC_DEBUG_INDEX) {
            tinyusb_config_cdcacm_t acm_cfg = {
                .usb_dev = TINYUSB_USBDEV_0,
                .cdc_port = i,
                .rx_unread_buf_sz = 256,
                .callback_rx = NULL,
                .callback_rx_wanted_char = NULL,
                .callback_line_state_changed = NULL,
                .callback_line_coding_changed = NULL,
            };

            ret = tusb_cdc_acm_init(&acm_cfg);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "CDC-ACM %d (debug) init failed: %s", i, esp_err_to_name(ret));
                return ret;
            }

            ret = esp_tusb_init_console(i);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Console redirect to CDC%d failed: %s", i, esp_err_to_name(ret));
                return ret;
            }

            ESP_LOGI(TAG, "CDC%d initialized as debug console", i);
            continue;
        }
#endif

        cdc_priv[i].cdc_index = i;

        char name[PORT_NAME_MAX];
        snprintf(name, sizeof(name), "CDC%d", i);

        // Initialize port struct
        port_t *port = &cdc_ports[i];
        memset(port, 0, sizeof(port_t));
        port->id = i;  // CDC ports get IDs 0, 1
        strncpy(port->name, name, PORT_NAME_MAX - 1);
        port->type = PORT_TYPE_CDC;
        port->state = PORT_STATE_READY;
        port->ops = cdc_ops;
        port->line_coding = port_line_coding_default();
        port->priv = &cdc_priv[i];

        port->rx_buf = xStreamBufferCreate(PORT_BUF_SIZE, 1);
        if (!port->rx_buf) {
            ESP_LOGE(TAG, "Failed to create rx buffer for %s", name);
            return ESP_ERR_NO_MEM;
        }

        // Configure TinyUSB CDC-ACM
        tinyusb_config_cdcacm_t acm_cfg = {
            .usb_dev = TINYUSB_USBDEV_0,
            .cdc_port = i,
            .rx_unread_buf_sz = 256,
            .callback_rx = cdc_rx_callback,
            .callback_rx_wanted_char = NULL,
            .callback_line_state_changed = cdc_line_state_changed_callback,
            .callback_line_coding_changed = cdc_line_coding_changed_callback,
        };

        ret = tusb_cdc_acm_init(&acm_cfg);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "CDC-ACM %d init failed: %s", i, esp_err_to_name(ret));
            return ret;
        }

        // Register in port registry
        ret = port_registry_add(port);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to register %s in port registry", name);
            return ret;
        }

        ESP_LOGI(TAG, "CDC port %s initialized and registered", name);
    }

    return ESP_OK;
}

port_t *port_cdc_get(int cdc_index)
{
    if (cdc_index < 0 || cdc_index >= CDC_PORT_COUNT) {
        return NULL;
    }
#ifdef CONFIG_VUART_CDC_DEBUG_PORT
    if (cdc_index == CDC_DEBUG_INDEX) {
        return NULL;  // Debug port is not available for routing
    }
#endif
    return &cdc_ports[cdc_index];
}
