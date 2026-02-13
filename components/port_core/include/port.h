#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/stream_buffer.h"
#include "esp_err.h"

#define PORT_MAX_COUNT      8   // 2 CDC + 2 UART + 4 TCP
#define PORT_NAME_MAX       16
#define PORT_BUF_SIZE       2048

typedef enum {
    PORT_TYPE_CDC = 0,
    PORT_TYPE_UART,
    PORT_TYPE_TCP,
} port_type_t;

typedef enum {
    PORT_STATE_DISABLED = 0,
    PORT_STATE_READY,
    PORT_STATE_ACTIVE,
    PORT_STATE_ERROR,
} port_state_t;

// Signal line bitmask (matches USB CDC serial state + RS-232 signals)
#define SIGNAL_DTR      (1 << 0)
#define SIGNAL_RTS      (1 << 1)
#define SIGNAL_CTS      (1 << 2)
#define SIGNAL_DSR      (1 << 3)
#define SIGNAL_DCD      (1 << 4)
#define SIGNAL_RI       (1 << 5)

typedef struct {
    uint32_t baud_rate;
    uint8_t  data_bits;     // 5, 6, 7, 8
    uint8_t  stop_bits;     // 0=1bit, 1=1.5bits, 2=2bits
    uint8_t  parity;        // 0=none, 1=odd, 2=even, 3=mark, 4=space
    bool     flow_control;  // RTS/CTS hardware flow control
} port_line_coding_t;

typedef struct port port_t;

typedef struct {
    int  (*open)(port_t *port);
    void (*close)(port_t *port);
    int  (*read)(port_t *port, uint8_t *buf, size_t len, TickType_t timeout);
    int  (*write)(port_t *port, const uint8_t *buf, size_t len, TickType_t timeout);
    int  (*get_signals)(port_t *port, uint32_t *signals);
    int  (*set_signals)(port_t *port, uint32_t signals);
    int  (*set_line_coding)(port_t *port, const port_line_coding_t *coding);
    int  (*get_line_coding)(port_t *port, port_line_coding_t *coding);
} port_ops_t;

struct port {
    uint8_t             id;
    char                name[PORT_NAME_MAX];
    port_type_t         type;
    port_state_t        state;
    port_ops_t          ops;
    port_line_coding_t  line_coding;
    uint32_t            signals;            // Current signal state bitmask
    uint32_t            signal_override;    // Which signals are manually overridden
    uint32_t            signal_override_val;// Override values for those signals
    StreamBufferHandle_t rx_buf;            // Incoming data buffer
    void               *priv;              // Type-specific private data
};

// Initialize a port struct
esp_err_t port_init(port_t *port, uint8_t id, const char *name, port_type_t type, const port_ops_t *ops, void *priv);
esp_err_t port_open(port_t *port);
void port_close(port_t *port);

// Get effective signals (hardware signals with overrides applied)
uint32_t port_get_effective_signals(port_t *port);

// Default line coding: 115200 8N1
static inline port_line_coding_t port_line_coding_default(void) {
    return (port_line_coding_t){
        .baud_rate = 115200,
        .data_bits = 8,
        .stop_bits = 0,
        .parity = 0,
        .flow_control = false,
    };
}
