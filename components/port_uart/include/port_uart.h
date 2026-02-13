#pragma once

#include "port.h"
#include "driver/uart.h"
#include "driver/gpio.h"

#define UART_PORT_COUNT 2

typedef struct {
    uart_port_t uart_num;   // UART_NUM_1 or UART_NUM_2
    gpio_num_t  tx_pin;
    gpio_num_t  rx_pin;
    gpio_num_t  rts_pin;    // -1 if unused
    gpio_num_t  cts_pin;    // -1 if unused
    gpio_num_t  dtr_pin;    // GPIO output for DTR, -1 if unused
    gpio_num_t  dsr_pin;    // GPIO input for DSR, -1 if unused
    gpio_num_t  dcd_pin;    // GPIO input for DCD, -1 if unused
    gpio_num_t  ri_pin;     // GPIO input for RI, -1 if unused
} uart_pin_config_t;

// Initialize a hardware UART port and register it in the port registry.
// port_id: unique port ID for the registry (e.g., 2 for UART1, 3 for UART2)
esp_err_t port_uart_init(uint8_t port_id, const uart_pin_config_t *pin_cfg);

// Get the port_t for a UART port by index (0 or 1)
port_t *port_uart_get(int uart_index);
