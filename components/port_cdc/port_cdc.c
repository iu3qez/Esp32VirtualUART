#include "port_cdc.h"
#include "port_registry.h"
#include "tusb.h"
#include "tusb_cdc_acm.h"
#include "esp_private/usb_phy.h"
#include "soc/lp_system_struct.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "port_cdc";

// Private data for each CDC port
typedef struct {
    int cdc_index;  // TinyUSB CDC port index (0-4)
} cdc_priv_t;

static port_t cdc_ports[CDC_PORT_COUNT];
static cdc_priv_t cdc_priv[CDC_PORT_COUNT];

// PHY handles for both USB controllers
static usb_phy_handle_t fs_phy_hdl;
static usb_phy_handle_t hs_phy_hdl;

// TinyUSB device task handle
static TaskHandle_t tusb_task_hdl;

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
    // Use heap buffer — this callback runs on the TinyUSB task stack which is
    // small (~4KB). A 4096-byte stack buffer would overflow it immediately.
    uint8_t *buf = malloc(512);
    if (!buf) return;
    size_t rx_size = 0;

    esp_err_t ret = tinyusb_cdcacm_read(itf, buf, 512, &rx_size);
    if (ret == ESP_OK && rx_size > 0) {
        size_t sent = xStreamBufferSend(port->rx_buf, buf, rx_size, 0);
        if (sent < rx_size) {
            ESP_LOGW(TAG, "%s: rx buffer overflow, dropped %d bytes",
                     port->name, (int)(rx_size - sent));
        }
        port->state = PORT_STATE_ACTIVE;
    }
    free(buf);
}

static void cdc_line_state_changed_callback(int itf, cdcacm_event_t *event)
{
    if (itf < 0 || itf >= CDC_PORT_COUNT) return;

    port_t *port = &cdc_ports[itf];
    int dtr = event->line_state_changed_data.dtr;
    int rts = event->line_state_changed_data.rts;

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

// --- TinyUSB device task ---

static void tusb_device_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "TinyUSB device task started (dual USB)");
    while (1) {
        tud_task();
    }
}

// --- Public API ---

esp_err_t port_cdc_init(void)
{
    esp_err_t ret;

    ESP_LOGI(TAG, "Initializing dual USB: %d CDC on FS (rhport 0) + %d CDC on HS (rhport 1) = %d total",
             CDC_PORT_COUNT_FS, CDC_PORT_COUNT_HS, CDC_PORT_COUNT);

    // ---- Step 0: Swap USB FS PHY mux so OTG FS uses PHY 0 (instead of USJ) ----
    // ESP32-P4 has 2 FS PHYs. Default: PHY0=USB-SERIAL-JTAG, PHY1=OTG_FS.
    // Swap so OTG FS gets PHY 0 (the one wired to the USB connector on most boards).
    LP_SYS.usb_ctrl.sw_hw_usb_phy_sel = 1;  // enable software PHY mapping
    LP_SYS.usb_ctrl.sw_usb_phy_sel = 1;     // swap: OTG_FS→PHY0, USJ→PHY1
    ESP_LOGI(TAG, "USB FS PHY mux swapped: OTG_FS→PHY0, USJ→PHY1");

    // ---- Step 1: Initialize both USB PHYs ----

    // FS PHY (OTG1.1, internal FSLS PHY) → rhport 0
    usb_phy_config_t fs_phy_conf = {
        .controller = USB_PHY_CTRL_OTG,
        .target     = USB_PHY_TARGET_INT,
        .otg_mode   = USB_OTG_MODE_DEVICE,
        .otg_speed  = USB_PHY_SPEED_FULL,
    };
    ret = usb_new_phy(&fs_phy_conf, &fs_phy_hdl);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "FS PHY init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "FS PHY (OTG1.1) initialized");

    // HS PHY (OTG2.0, internal UTMI PHY) → rhport 1
    usb_phy_config_t hs_phy_conf = {
        .controller = USB_PHY_CTRL_OTG,
        .target     = USB_PHY_TARGET_UTMI,
        .otg_mode   = USB_OTG_MODE_DEVICE,
        .otg_speed  = USB_PHY_SPEED_HIGH,
    };
    ret = usb_new_phy(&hs_phy_conf, &hs_phy_hdl);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "HS PHY init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "HS PHY (OTG2.0) initialized");

    // ---- Step 2: Initialize TinyUSB device stack on both rhports ----
    // Init FS first so CDC 0-1 get assigned to FS, then HS gets CDC 2-4

    tusb_rhport_init_t fs_rh_init = {
        .role  = TUSB_ROLE_DEVICE,
        .speed = TUSB_SPEED_FULL,
    };
    if (!tud_rhport_init(0, &fs_rh_init)) {
        ESP_LOGE(TAG, "TinyUSB device init failed on rhport 0 (FS)");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "TinyUSB device stack initialized on rhport 0 (FS)");

    tusb_rhport_init_t hs_rh_init = {
        .role  = TUSB_ROLE_DEVICE,
        .speed = TUSB_SPEED_HIGH,
    };
    if (!tud_rhport_init(1, &hs_rh_init)) {
        ESP_LOGE(TAG, "TinyUSB device init failed on rhport 1 (HS)");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "TinyUSB device stack initialized on rhport 1 (HS)");

    // ---- Step 3: Start TinyUSB device task ----
    // tud_task() internally iterates all initialized rhport instances

    BaseType_t xret = xTaskCreatePinnedToCore(
        tusb_device_task, "TinyUSB",
        CONFIG_TINYUSB_TASK_STACK_SIZE,
        NULL,
        CONFIG_TINYUSB_TASK_PRIORITY,
        &tusb_task_hdl,
        CONFIG_TINYUSB_TASK_AFFINITY);
    if (xret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create TinyUSB device task");
        return ESP_FAIL;
    }

    // ---- Step 4: Initialize CDC-ACM ports ----
    // CDC instance numbering follows init order in TinyUSB class drivers:
    //   CDC 0-1 on FS (rhport 0), CDC 2-4 on HS (rhport 1)

    for (int i = 0; i < CDC_PORT_COUNT; i++) {
        cdc_priv[i].cdc_index = i;

        port_t *port = &cdc_ports[i];
        memset(port, 0, sizeof(port_t));
        port->id = i;  // CDC ports get IDs 0-4

        // Label with bus type: CDC0(FS), CDC1(FS), CDC2(HS), CDC3(HS), CDC4(HS)
        const char *bus = (i < CDC_PORT_COUNT_FS) ? "FS" : "HS";
        snprintf(port->name, PORT_NAME_MAX, "CDC%d(%s)", i, bus);

        port->type = PORT_TYPE_CDC;
        port->state = PORT_STATE_READY;
        port->ops = cdc_ops;
        port->line_coding = port_line_coding_default();
        port->priv = &cdc_priv[i];

        port->rx_buf = xStreamBufferCreate(PORT_BUF_SIZE, 1);
        if (!port->rx_buf) {
            ESP_LOGE(TAG, "Failed to create rx buffer for %s", port->name);
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
            ESP_LOGE(TAG, "Failed to register %s in port registry", port->name);
            return ret;
        }

        ESP_LOGI(TAG, "CDC port %s initialized and registered", port->name);
    }

    return ESP_OK;
}

port_t *port_cdc_get(int cdc_index)
{
    if (cdc_index < 0 || cdc_index >= CDC_PORT_COUNT) {
        return NULL;
    }
    return &cdc_ports[cdc_index];
}
