#include "esp_http_server.h"
#include "esp_log.h"
#include "cJSON.h"
#include <string.h>

static const char *TAG = "ws_handler";

#define WS_MAX_CLIENTS 4

typedef struct {
    int fd;
    bool active;
} ws_client_t;

static httpd_handle_t ws_server = NULL;
static ws_client_t signal_clients[WS_MAX_CLIENTS];
static ws_client_t monitor_clients[WS_MAX_CLIENTS];

void ws_init(httpd_handle_t server_handle)
{
    ws_server = server_handle;
    memset(signal_clients, 0, sizeof(signal_clients));
    memset(monitor_clients, 0, sizeof(monitor_clients));
}

void ws_cleanup(void)
{
    ws_server = NULL;
    memset(signal_clients, 0, sizeof(signal_clients));
    memset(monitor_clients, 0, sizeof(monitor_clients));
}

static void add_client(ws_client_t *list, int fd)
{
    for (int i = 0; i < WS_MAX_CLIENTS; i++) {
        if (!list[i].active) {
            list[i].fd = fd;
            list[i].active = true;
            ESP_LOGI(TAG, "WS client connected: fd=%d (slot %d)", fd, i);
            return;
        }
    }
    ESP_LOGW(TAG, "WS client rejected: no free slots");
}

static void remove_client(ws_client_t *list, int fd)
{
    for (int i = 0; i < WS_MAX_CLIENTS; i++) {
        if (list[i].active && list[i].fd == fd) {
            list[i].active = false;
            ESP_LOGI(TAG, "WS client disconnected: fd=%d (slot %d)", fd, i);
            return;
        }
    }
}

static void broadcast(ws_client_t *list, const char *data, size_t len)
{
    if (!ws_server) return;

    httpd_ws_frame_t frame = {
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)data,
        .len = len,
    };

    for (int i = 0; i < WS_MAX_CLIENTS; i++) {
        if (list[i].active) {
            esp_err_t ret = httpd_ws_send_frame_async(ws_server, list[i].fd, &frame);
            if (ret != ESP_OK) {
                ESP_LOGD(TAG, "WS send failed fd=%d: %s", list[i].fd, esp_err_to_name(ret));
                list[i].active = false;
            }
        }
    }
}

// WebSocket handler for /ws/signals
esp_err_t ws_signals_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        // WebSocket handshake
        ESP_LOGI(TAG, "WS /ws/signals handshake, fd=%d", httpd_req_to_sockfd(req));
        return ESP_OK;
    }

    // Receive frame (client might send ping/pong or close)
    httpd_ws_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.type = HTTPD_WS_TYPE_TEXT;

    esp_err_t ret = httpd_ws_recv_frame(req, &frame, 0);
    if (ret != ESP_OK) {
        return ret;
    }

    if (frame.type == HTTPD_WS_TYPE_CLOSE) {
        remove_client(signal_clients, httpd_req_to_sockfd(req));
        return ESP_OK;
    }

    // If it's the first real frame, register the client
    int fd = httpd_req_to_sockfd(req);
    bool found = false;
    for (int i = 0; i < WS_MAX_CLIENTS; i++) {
        if (signal_clients[i].active && signal_clients[i].fd == fd) {
            found = true;
            break;
        }
    }
    if (!found) {
        add_client(signal_clients, fd);
    }

    // Allocate buffer if frame has payload
    if (frame.len > 0) {
        uint8_t *buf = malloc(frame.len + 1);
        if (buf) {
            frame.payload = buf;
            httpd_ws_recv_frame(req, &frame, frame.len);
            free(buf);
        }
    }

    return ESP_OK;
}

// WebSocket handler for /ws/monitor
esp_err_t ws_monitor_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "WS /ws/monitor handshake, fd=%d", httpd_req_to_sockfd(req));
        return ESP_OK;
    }

    httpd_ws_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.type = HTTPD_WS_TYPE_TEXT;

    esp_err_t ret = httpd_ws_recv_frame(req, &frame, 0);
    if (ret != ESP_OK) {
        return ret;
    }

    if (frame.type == HTTPD_WS_TYPE_CLOSE) {
        remove_client(monitor_clients, httpd_req_to_sockfd(req));
        return ESP_OK;
    }

    int fd = httpd_req_to_sockfd(req);
    bool found = false;
    for (int i = 0; i < WS_MAX_CLIENTS; i++) {
        if (monitor_clients[i].active && monitor_clients[i].fd == fd) {
            found = true;
            break;
        }
    }
    if (!found) {
        add_client(monitor_clients, fd);
    }

    if (frame.len > 0) {
        uint8_t *buf = malloc(frame.len + 1);
        if (buf) {
            frame.payload = buf;
            httpd_ws_recv_frame(req, &frame, frame.len);
            free(buf);
        }
    }

    return ESP_OK;
}

// Broadcast signal change to /ws/signals clients
void ws_broadcast_signal(uint8_t port_id, uint32_t signals)
{
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "type", "signal");
    cJSON_AddNumberToObject(obj, "portId", port_id);

    cJSON *sigs = cJSON_CreateObject();
    cJSON_AddBoolToObject(sigs, "dtr", (signals & (1 << 0)) != 0);
    cJSON_AddBoolToObject(sigs, "rts", (signals & (1 << 1)) != 0);
    cJSON_AddBoolToObject(sigs, "cts", (signals & (1 << 2)) != 0);
    cJSON_AddBoolToObject(sigs, "dsr", (signals & (1 << 3)) != 0);
    cJSON_AddBoolToObject(sigs, "dcd", (signals & (1 << 4)) != 0);
    cJSON_AddBoolToObject(sigs, "ri",  (signals & (1 << 5)) != 0);
    cJSON_AddItemToObject(obj, "signals", sigs);

    char *str = cJSON_PrintUnformatted(obj);
    if (str) {
        broadcast(signal_clients, str, strlen(str));
        free(str);
    }
    cJSON_Delete(obj);
}

// Broadcast data flow stats to /ws/monitor clients
void ws_broadcast_data_flow(uint8_t route_id, uint32_t bytes_src_to_dst, uint32_t bytes_dst_to_src)
{
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "type", "dataFlow");
    cJSON_AddNumberToObject(obj, "routeId", route_id);
    cJSON_AddNumberToObject(obj, "bytesSrcToDst", bytes_src_to_dst);
    cJSON_AddNumberToObject(obj, "bytesDstToSrc", bytes_dst_to_src);

    char *str = cJSON_PrintUnformatted(obj);
    if (str) {
        broadcast(monitor_clients, str, strlen(str));
        free(str);
    }
    cJSON_Delete(obj);
}
