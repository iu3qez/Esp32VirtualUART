#include "port_uart.h"
#include "port_registry.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "port_uart";

#define UART_RX_BUF_SIZE    1024
#define SIGNAL_POLL_MS      10

typedef struct {
    uart_port_t     uart_num;
    uart_pin_config_t pins;
    TaskHandle_t    signal_task;
    volatile bool   signal_task_running;
} uart_priv_t;

static port_t uart_ports[UART_PORT_COUNT];
static uart_priv_t uart_priv[UART_PORT_COUNT];
static int uart_port_count = 0;

// --- Signal polling task ---
// Polls GPIO-based signal lines (DSR, DCD, RI) and updates port state

static void uart_signal_poll_task(void *arg)
{
    port_t *port = (port_t *)arg;
    uart_priv_t *priv = (uart_priv_t *)port->priv;

    while (priv->signal_task_running) {
        uint32_t new_signals = port->signals;

        // Read CTS from UART hardware (if pin configured)
        if (priv->pins.cts_pin >= 0) {
            if (gpio_get_level(priv->pins.cts_pin)) {
                new_signals |= SIGNAL_CTS;
            } else {
                new_signals &= ~SIGNAL_CTS;
            }
        }

        // Read DSR from GPIO (if pin configured)
        if (priv->pins.dsr_pin >= 0) {
            if (gpio_get_level(priv->pins.dsr_pin)) {
                new_signals |= SIGNAL_DSR;
            } else {
                new_signals &= ~SIGNAL_DSR;
            }
        }

        // Read DCD from GPIO (if pin configured)
        if (priv->pins.dcd_pin >= 0) {
            if (gpio_get_level(priv->pins.dcd_pin)) {
                new_signals |= SIGNAL_DCD;
            } else {
                new_signals &= ~SIGNAL_DCD;
            }
        }

        // Read RI from GPIO (if pin configured)
        if (priv->pins.ri_pin >= 0) {
            if (gpio_get_level(priv->pins.ri_pin)) {
                new_signals |= SIGNAL_RI;
            } else {
                new_signals &= ~SIGNAL_RI;
            }
        }

        port->signals = new_signals;
        vTaskDelay(pdMS_TO_TICKS(SIGNAL_POLL_MS));
    }

    vTaskDelete(NULL);
}

// --- Port ops implementation ---

static int uart_open(port_t *port)
{
    uart_priv_t *priv = (uart_priv_t *)port->priv;

    uart_config_t uart_config = {
        .baud_rate  = port->line_coding.baud_rate,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = port->line_coding.flow_control ? UART_HW_FLOWCTRL_CTS_RTS : UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 122,
        .source_clk = UART_SCLK_DEFAULT,
    };

    // Map line coding to UART config
    switch (port->line_coding.data_bits) {
    case 5: uart_config.data_bits = UART_DATA_5_BITS; break;
    case 6: uart_config.data_bits = UART_DATA_6_BITS; break;
    case 7: uart_config.data_bits = UART_DATA_7_BITS; break;
    default: uart_config.data_bits = UART_DATA_8_BITS; break;
    }

    switch (port->line_coding.parity) {
    case 1: uart_config.parity = UART_PARITY_ODD; break;
    case 2: uart_config.parity = UART_PARITY_EVEN; break;
    default: uart_config.parity = UART_PARITY_DISABLE; break;
    }

    switch (port->line_coding.stop_bits) {
    case 1: uart_config.stop_bits = UART_STOP_BITS_1_5; break;
    case 2: uart_config.stop_bits = UART_STOP_BITS_2; break;
    default: uart_config.stop_bits = UART_STOP_BITS_1; break;
    }

    esp_err_t ret = uart_param_config(priv->uart_num, &uart_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "%s: uart_param_config failed: %s", port->name, esp_err_to_name(ret));
        return -1;
    }

    ret = uart_set_pin(priv->uart_num, priv->pins.tx_pin, priv->pins.rx_pin,
                       priv->pins.rts_pin, priv->pins.cts_pin);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "%s: uart_set_pin failed: %s", port->name, esp_err_to_name(ret));
        return -1;
    }

    ret = uart_driver_install(priv->uart_num, UART_RX_BUF_SIZE * 2, UART_RX_BUF_SIZE, 0, NULL, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "%s: uart_driver_install failed: %s", port->name, esp_err_to_name(ret));
        return -1;
    }

    // Configure DTR as GPIO output if pin assigned
    if (priv->pins.dtr_pin >= 0) {
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << priv->pins.dtr_pin),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&io_conf);
        gpio_set_level(priv->pins.dtr_pin, 0);
    }

    // Configure DSR, DCD, RI as GPIO inputs if pins assigned
    uint64_t input_mask = 0;
    if (priv->pins.dsr_pin >= 0) input_mask |= (1ULL << priv->pins.dsr_pin);
    if (priv->pins.dcd_pin >= 0) input_mask |= (1ULL << priv->pins.dcd_pin);
    if (priv->pins.ri_pin >= 0)  input_mask |= (1ULL << priv->pins.ri_pin);

    if (input_mask) {
        gpio_config_t io_conf = {
            .pin_bit_mask = input_mask,
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&io_conf);
    }

    // Start signal polling task
    priv->signal_task_running = true;
    char task_name[24];
    snprintf(task_name, sizeof(task_name), "sig_%.8s", port->name);
    xTaskCreate(uart_signal_poll_task, task_name, 2048, port, 4, &priv->signal_task);

    port->state = PORT_STATE_ACTIVE;
    ESP_LOGI(TAG, "%s opened: %lu baud on TX=%d RX=%d",
             port->name, (unsigned long)port->line_coding.baud_rate,
             priv->pins.tx_pin, priv->pins.rx_pin);
    return 0;
}

static void uart_close(port_t *port)
{
    uart_priv_t *priv = (uart_priv_t *)port->priv;

    priv->signal_task_running = false;
    if (priv->signal_task) {
        vTaskDelay(pdMS_TO_TICKS(SIGNAL_POLL_MS * 2));  // Let task exit
        priv->signal_task = NULL;
    }

    uart_driver_delete(priv->uart_num);
    port->state = PORT_STATE_DISABLED;
    ESP_LOGI(TAG, "%s closed", port->name);
}

static int uart_read(port_t *port, uint8_t *buf, size_t len, TickType_t timeout)
{
    uart_priv_t *priv = (uart_priv_t *)port->priv;
    int received = uart_read_bytes(priv->uart_num, buf, len, timeout);
    return received > 0 ? received : 0;
}

static int uart_write(port_t *port, const uint8_t *buf, size_t len, TickType_t timeout)
{
    uart_priv_t *priv = (uart_priv_t *)port->priv;
    (void)timeout;
    int written = uart_write_bytes(priv->uart_num, buf, len);
    return written > 0 ? written : 0;
}

static int uart_get_signals(port_t *port, uint32_t *signals)
{
    *signals = port_get_effective_signals(port);
    return 0;
}

static int uart_set_signals(port_t *port, uint32_t signals)
{
    uart_priv_t *priv = (uart_priv_t *)port->priv;

    // Set RTS via UART hardware
    if (signals & SIGNAL_RTS) {
        uart_set_rts(priv->uart_num, 0);  // Active low
    } else {
        uart_set_rts(priv->uart_num, 1);
    }

    // Set DTR via GPIO
    if (priv->pins.dtr_pin >= 0) {
        gpio_set_level(priv->pins.dtr_pin, (signals & SIGNAL_DTR) ? 1 : 0);
    }

    // Store output signals
    port->signals = (port->signals & (SIGNAL_CTS | SIGNAL_DSR | SIGNAL_DCD | SIGNAL_RI)) |
                    (signals & (SIGNAL_DTR | SIGNAL_RTS));
    return 0;
}

static int uart_set_line_coding(port_t *port, const port_line_coding_t *coding)
{
    uart_priv_t *priv = (uart_priv_t *)port->priv;
    port->line_coding = *coding;

    // Reconfigure UART at runtime
    uart_config_t uart_config = {
        .baud_rate = coding->baud_rate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = coding->flow_control ? UART_HW_FLOWCTRL_CTS_RTS : UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 122,
        .source_clk = UART_SCLK_DEFAULT,
    };

    switch (coding->data_bits) {
    case 5: uart_config.data_bits = UART_DATA_5_BITS; break;
    case 6: uart_config.data_bits = UART_DATA_6_BITS; break;
    case 7: uart_config.data_bits = UART_DATA_7_BITS; break;
    default: uart_config.data_bits = UART_DATA_8_BITS; break;
    }

    switch (coding->parity) {
    case 1: uart_config.parity = UART_PARITY_ODD; break;
    case 2: uart_config.parity = UART_PARITY_EVEN; break;
    default: uart_config.parity = UART_PARITY_DISABLE; break;
    }

    switch (coding->stop_bits) {
    case 1: uart_config.stop_bits = UART_STOP_BITS_1_5; break;
    case 2: uart_config.stop_bits = UART_STOP_BITS_2; break;
    default: uart_config.stop_bits = UART_STOP_BITS_1; break;
    }

    uart_param_config(priv->uart_num, &uart_config);

    ESP_LOGI(TAG, "%s: line coding set to %lu baud %d%c%s",
             port->name, (unsigned long)coding->baud_rate, coding->data_bits,
             "NOEMS"[coding->parity], coding->stop_bits == 0 ? "1" : "2");
    return 0;
}

static int uart_get_line_coding(port_t *port, port_line_coding_t *coding)
{
    *coding = port->line_coding;
    return 0;
}

static const port_ops_t uart_ops = {
    .open           = uart_open,
    .close          = uart_close,
    .read           = uart_read,
    .write          = uart_write,
    .get_signals    = uart_get_signals,
    .set_signals    = uart_set_signals,
    .set_line_coding = uart_set_line_coding,
    .get_line_coding = uart_get_line_coding,
};

// --- Public API ---

esp_err_t port_uart_init(uint8_t port_id, const uart_pin_config_t *pin_cfg)
{
    if (uart_port_count >= UART_PORT_COUNT) {
        ESP_LOGE(TAG, "Maximum UART ports (%d) reached", UART_PORT_COUNT);
        return ESP_ERR_NO_MEM;
    }

    int idx = uart_port_count;
    uart_priv_t *priv = &uart_priv[idx];
    priv->uart_num = pin_cfg->uart_num;
    priv->pins = *pin_cfg;
    priv->signal_task = NULL;
    priv->signal_task_running = false;

    port_t *port = &uart_ports[idx];
    memset(port, 0, sizeof(port_t));
    port->id = port_id;
    snprintf(port->name, PORT_NAME_MAX, "UART%d", pin_cfg->uart_num);
    port->type = PORT_TYPE_UART;
    port->state = PORT_STATE_DISABLED;
    port->ops = uart_ops;
    port->line_coding = port_line_coding_default();
    port->priv = priv;

    port->rx_buf = xStreamBufferCreate(PORT_BUF_SIZE, 1);
    if (!port->rx_buf) {
        ESP_LOGE(TAG, "Failed to create rx buffer for %s", port->name);
        return ESP_ERR_NO_MEM;
    }

    esp_err_t ret = port_registry_add(port);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register %s", port->name);
        return ret;
    }

    uart_port_count++;
    ESP_LOGI(TAG, "%s registered (TX=%d RX=%d RTS=%d CTS=%d)",
             port->name, pin_cfg->tx_pin, pin_cfg->rx_pin,
             pin_cfg->rts_pin, pin_cfg->cts_pin);
    return ESP_OK;
}

port_t *port_uart_get(int uart_index)
{
    if (uart_index < 0 || uart_index >= uart_port_count) {
        return NULL;
    }
    return &uart_ports[uart_index];
}
