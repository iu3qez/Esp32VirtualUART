#include "port_tcp.h"
#include "port_registry.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include <string.h>
#include <errno.h>

static const char *TAG = "port_tcp";

#define TCP_RECONNECT_DELAY_MS  3000
#define TCP_ACCEPT_TIMEOUT_MS   1000

typedef struct {
    tcp_port_config_t   cfg;
    int                 listen_fd;
    int                 client_fd;
    TaskHandle_t        accept_task;
    volatile bool       task_running;
} tcp_priv_t;

static port_t tcp_ports[TCP_PORT_COUNT];
static tcp_priv_t tcp_priv[TCP_PORT_COUNT];
static int tcp_port_count = 0;

// --- Server accept task ---

static void tcp_accept_task(void *arg)
{
    port_t *port = (port_t *)arg;
    tcp_priv_t *priv = (tcp_priv_t *)port->priv;

    ESP_LOGI(TAG, "%s: server listening on port %d", port->name, priv->cfg.tcp_port);

    while (priv->task_running) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);

        // Use select with timeout so we can check task_running
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(priv->listen_fd, &rfds);
        struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };

        int sel = select(priv->listen_fd + 1, &rfds, NULL, NULL, &tv);
        if (sel <= 0) continue;

        int fd = accept(priv->listen_fd, (struct sockaddr *)&client_addr, &addr_len);
        if (fd < 0) {
            ESP_LOGW(TAG, "%s: accept failed: %d", port->name, errno);
            continue;
        }

        // Close previous client if any
        if (priv->client_fd >= 0) {
            close(priv->client_fd);
        }

        priv->client_fd = fd;
        port->state = PORT_STATE_ACTIVE;
        port->signals |= SIGNAL_DCD;  // Connection established

        char addr_str[16];
        inet_ntoa_r(client_addr.sin_addr, addr_str, sizeof(addr_str));
        ESP_LOGI(TAG, "%s: client connected from %s:%d",
                 port->name, addr_str, ntohs(client_addr.sin_port));

        // Wait for disconnect
        while (priv->task_running && priv->client_fd >= 0) {
            // Check if client is still connected
            char probe;
            fd_set check;
            FD_ZERO(&check);
            FD_SET(priv->client_fd, &check);
            struct timeval check_tv = { .tv_sec = 1, .tv_usec = 0 };

            int ready = select(priv->client_fd + 1, &check, NULL, NULL, &check_tv);
            if (ready > 0) {
                int n = recv(priv->client_fd, &probe, 1, MSG_PEEK);
                if (n == 0) {
                    // Client disconnected
                    ESP_LOGI(TAG, "%s: client disconnected", port->name);
                    close(priv->client_fd);
                    priv->client_fd = -1;
                    port->state = PORT_STATE_READY;
                    port->signals &= ~SIGNAL_DCD;
                    break;
                }
            }
        }
    }

    vTaskDelete(NULL);
}

// --- Client connect logic ---

static int tcp_client_connect(port_t *port)
{
    tcp_priv_t *priv = (tcp_priv_t *)port->priv;

    struct sockaddr_in dest_addr = {0};
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(priv->cfg.tcp_port);
    inet_aton(priv->cfg.host, &dest_addr.sin_addr);

    int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) {
        ESP_LOGE(TAG, "%s: socket() failed: %d", port->name, errno);
        return -1;
    }

    int err = connect(fd, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err != 0) {
        ESP_LOGW(TAG, "%s: connect to %s:%d failed: %d",
                 port->name, priv->cfg.host, priv->cfg.tcp_port, errno);
        close(fd);
        return -1;
    }

    priv->client_fd = fd;
    port->state = PORT_STATE_ACTIVE;
    port->signals |= SIGNAL_DCD;

    ESP_LOGI(TAG, "%s: connected to %s:%d", port->name, priv->cfg.host, priv->cfg.tcp_port);
    return 0;
}

// --- Port ops ---

static int tcp_open(port_t *port)
{
    tcp_priv_t *priv = (tcp_priv_t *)port->priv;

    if (priv->cfg.is_server) {
        // Create listening socket
        priv->listen_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (priv->listen_fd < 0) {
            ESP_LOGE(TAG, "%s: socket() failed", port->name);
            return -1;
        }

        int opt = 1;
        setsockopt(priv->listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        struct sockaddr_in bind_addr = {0};
        bind_addr.sin_family = AF_INET;
        bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        bind_addr.sin_port = htons(priv->cfg.tcp_port);

        if (bind(priv->listen_fd, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) != 0) {
            ESP_LOGE(TAG, "%s: bind failed: %d", port->name, errno);
            close(priv->listen_fd);
            priv->listen_fd = -1;
            return -1;
        }

        if (listen(priv->listen_fd, 1) != 0) {
            ESP_LOGE(TAG, "%s: listen failed: %d", port->name, errno);
            close(priv->listen_fd);
            priv->listen_fd = -1;
            return -1;
        }

        // Start accept task
        priv->task_running = true;
        char name[16];
        snprintf(name, sizeof(name), "tcp_s%.4s", port->name + 3);
        xTaskCreate(tcp_accept_task, name, 4096, port, 4, &priv->accept_task);

        port->state = PORT_STATE_READY;
    } else {
        // Client mode: attempt connection
        if (tcp_client_connect(port) != 0) {
            port->state = PORT_STATE_READY;  // Will retry on read/write
        }
    }

    return 0;
}

static void tcp_close(port_t *port)
{
    tcp_priv_t *priv = (tcp_priv_t *)port->priv;

    priv->task_running = false;
    if (priv->accept_task) {
        vTaskDelay(pdMS_TO_TICKS(1500));  // Let accept task exit
        priv->accept_task = NULL;
    }

    if (priv->client_fd >= 0) {
        close(priv->client_fd);
        priv->client_fd = -1;
    }
    if (priv->listen_fd >= 0) {
        close(priv->listen_fd);
        priv->listen_fd = -1;
    }

    port->state = PORT_STATE_DISABLED;
    port->signals &= ~SIGNAL_DCD;
    ESP_LOGI(TAG, "%s closed", port->name);
}

static int tcp_read(port_t *port, uint8_t *buf, size_t len, TickType_t timeout)
{
    tcp_priv_t *priv = (tcp_priv_t *)port->priv;

    if (priv->client_fd < 0) {
        // Client mode: try reconnect
        if (!priv->cfg.is_server) {
            tcp_client_connect(port);
        }
        if (priv->client_fd < 0) return 0;
    }

    // Use select with timeout
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(priv->client_fd, &rfds);

    int timeout_ms = (timeout * portTICK_PERIOD_MS);
    struct timeval tv = {
        .tv_sec = timeout_ms / 1000,
        .tv_usec = (timeout_ms % 1000) * 1000,
    };

    int sel = select(priv->client_fd + 1, &rfds, NULL, NULL, &tv);
    if (sel <= 0) return 0;

    int n = recv(priv->client_fd, buf, len, 0);
    if (n <= 0) {
        // Connection closed or error
        ESP_LOGI(TAG, "%s: connection lost", port->name);
        close(priv->client_fd);
        priv->client_fd = -1;
        port->state = PORT_STATE_READY;
        port->signals &= ~SIGNAL_DCD;
        return 0;
    }

    return n;
}

static int tcp_write(port_t *port, const uint8_t *buf, size_t len, TickType_t timeout)
{
    tcp_priv_t *priv = (tcp_priv_t *)port->priv;
    (void)timeout;

    if (priv->client_fd < 0) {
        if (!priv->cfg.is_server) {
            tcp_client_connect(port);
        }
        if (priv->client_fd < 0) return 0;
    }

    int n = send(priv->client_fd, buf, len, 0);
    if (n < 0) {
        ESP_LOGW(TAG, "%s: send failed: %d", port->name, errno);
        close(priv->client_fd);
        priv->client_fd = -1;
        port->state = PORT_STATE_READY;
        port->signals &= ~SIGNAL_DCD;
        return 0;
    }

    return n;
}

static int tcp_get_signals(port_t *port, uint32_t *signals)
{
    *signals = port_get_effective_signals(port);
    return 0;
}

static int tcp_set_signals(port_t *port, uint32_t signals)
{
    // TCP ports only support virtual signal state
    port->signals = (port->signals & SIGNAL_DCD) | (signals & ~SIGNAL_DCD);
    return 0;
}

static int tcp_set_line_coding(port_t *port, const port_line_coding_t *coding)
{
    // TCP doesn't have physical line coding, but store it for display
    port->line_coding = *coding;
    return 0;
}

static int tcp_get_line_coding(port_t *port, port_line_coding_t *coding)
{
    *coding = port->line_coding;
    return 0;
}

static const port_ops_t tcp_ops = {
    .open           = tcp_open,
    .close          = tcp_close,
    .read           = tcp_read,
    .write          = tcp_write,
    .get_signals    = tcp_get_signals,
    .set_signals    = tcp_set_signals,
    .set_line_coding = tcp_set_line_coding,
    .get_line_coding = tcp_get_line_coding,
};

// --- Public API ---

esp_err_t port_tcp_init(uint8_t port_id, const tcp_port_config_t *cfg)
{
    if (tcp_port_count >= TCP_PORT_COUNT) {
        ESP_LOGE(TAG, "Maximum TCP ports (%d) reached", TCP_PORT_COUNT);
        return ESP_ERR_NO_MEM;
    }

    if (cfg->tcp_port == 0) {
        ESP_LOGD(TAG, "TCP port slot %d not configured, skipping", tcp_port_count);
        return ESP_OK;
    }

    int idx = tcp_port_count;
    tcp_priv_t *priv = &tcp_priv[idx];
    priv->cfg = *cfg;
    priv->listen_fd = -1;
    priv->client_fd = -1;
    priv->accept_task = NULL;
    priv->task_running = false;

    port_t *port = &tcp_ports[idx];
    memset(port, 0, sizeof(port_t));
    port->id = port_id;
    snprintf(port->name, PORT_NAME_MAX, "TCP%d", idx);
    port->type = PORT_TYPE_TCP;
    port->state = PORT_STATE_DISABLED;
    port->ops = tcp_ops;
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

    tcp_port_count++;
    ESP_LOGI(TAG, "%s registered (%s mode, %s:%d)",
             port->name, cfg->is_server ? "server" : "client",
             cfg->host, cfg->tcp_port);
    return ESP_OK;
}

port_t *port_tcp_get(int tcp_index)
{
    if (tcp_index < 0 || tcp_index >= tcp_port_count) {
        return NULL;
    }
    return &tcp_ports[tcp_index];
}
