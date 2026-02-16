#include "esp_http_server.h"
#include "esp_log.h"
#include "cJSON.h"
#include "port.h"
#include "port_registry.h"
#include "route.h"
#include "config_store.h"
#include "wifi_mgr.h"
#include "esp_timer.h"
#include <string.h>

static const char *TAG = "api_handler";

// Deferred WiFi switch (allows HTTP response to complete before killing AP)
static char deferred_ssid[33] = "";
static char deferred_pass[65] = "";

static void deferred_wifi_switch_cb(void *arg)
{
    ESP_LOGI(TAG, "Deferred WiFi switch to SSID: %s", deferred_ssid);
    wifi_mgr_set_credentials(deferred_ssid,
        strlen(deferred_pass) > 0 ? deferred_pass : NULL);
}

// Helper: send JSON response
static esp_err_t send_json(httpd_req_t *req, cJSON *json)
{
    char *str = cJSON_PrintUnformatted(json);
    if (!str) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "JSON format error");
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    esp_err_t ret = httpd_resp_sendstr(req, str);
    free(str);
    return ret;
}

// Helper: read request body
static char *read_body(httpd_req_t *req)
{
    int total_len = req->content_len;
    if (total_len <= 0 || total_len > 4096) {
        return NULL;
    }

    char *buf = malloc(total_len + 1);
    if (!buf) return NULL;

    int received = 0;
    while (received < total_len) {
        int ret = httpd_req_recv(req, buf + received, total_len - received);
        if (ret <= 0) {
            free(buf);
            return NULL;
        }
        received += ret;
    }
    buf[total_len] = '\0';
    return buf;
}

// Helper: serialize port to JSON
static cJSON *port_to_json(port_t *port)
{
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddNumberToObject(obj, "id", port->id);
    cJSON_AddStringToObject(obj, "name", port->name);
    cJSON_AddNumberToObject(obj, "type", port->type);
    cJSON_AddNumberToObject(obj, "state", port->state);

    // Line coding
    cJSON *lc = cJSON_CreateObject();
    cJSON_AddNumberToObject(lc, "baudRate", port->line_coding.baud_rate);
    cJSON_AddNumberToObject(lc, "dataBits", port->line_coding.data_bits);
    cJSON_AddNumberToObject(lc, "stopBits", port->line_coding.stop_bits);
    cJSON_AddNumberToObject(lc, "parity", port->line_coding.parity);
    cJSON_AddBoolToObject(lc, "flowControl", port->line_coding.flow_control);
    cJSON_AddItemToObject(obj, "lineCoding", lc);

    // Signals
    uint32_t sigs = port_get_effective_signals(port);
    cJSON *signals = cJSON_CreateObject();
    cJSON_AddBoolToObject(signals, "dtr", (sigs & SIGNAL_DTR) != 0);
    cJSON_AddBoolToObject(signals, "rts", (sigs & SIGNAL_RTS) != 0);
    cJSON_AddBoolToObject(signals, "cts", (sigs & SIGNAL_CTS) != 0);
    cJSON_AddBoolToObject(signals, "dsr", (sigs & SIGNAL_DSR) != 0);
    cJSON_AddBoolToObject(signals, "dcd", (sigs & SIGNAL_DCD) != 0);
    cJSON_AddBoolToObject(signals, "ri",  (sigs & SIGNAL_RI)  != 0);
    cJSON_AddItemToObject(obj, "signals", signals);

    return obj;
}

// Helper: serialize route to JSON
static cJSON *route_to_json(route_t *route)
{
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddNumberToObject(obj, "id", route->id);
    cJSON_AddNumberToObject(obj, "type", route->type);
    cJSON_AddBoolToObject(obj, "active", route->active);
    cJSON_AddNumberToObject(obj, "srcPortId", route->src_port_id);

    cJSON *dsts = cJSON_CreateArray();
    for (int i = 0; i < route->dst_count; i++) {
        cJSON_AddItemToArray(dsts, cJSON_CreateNumber(route->dst_port_ids[i]));
    }
    cJSON_AddItemToObject(obj, "dstPortIds", dsts);

    // Signal mappings
    if (route->signal_map_count > 0) {
        cJSON *maps = cJSON_CreateArray();
        for (int i = 0; i < route->signal_map_count; i++) {
            cJSON *m = cJSON_CreateObject();
            cJSON_AddNumberToObject(m, "fromSignal", route->signal_map[i].from_signal);
            cJSON_AddNumberToObject(m, "toSignal", route->signal_map[i].to_signal);
            cJSON_AddItemToArray(maps, m);
        }
        cJSON_AddItemToObject(obj, "signalMap", maps);
    }

    // Stats
    cJSON_AddNumberToObject(obj, "bytesSrcToDst", route->bytes_fwd_src_to_dst);
    cJSON_AddNumberToObject(obj, "bytesDstToSrc", route->bytes_fwd_dst_to_src);

    return obj;
}

// GET /api/ports
esp_err_t api_get_ports_handler(httpd_req_t *req)
{
    port_t *ports[PORT_MAX_COUNT];
    int count = port_registry_get_all(ports, PORT_MAX_COUNT);

    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < count; i++) {
        cJSON_AddItemToArray(arr, port_to_json(ports[i]));
    }

    esp_err_t ret = send_json(req, arr);
    cJSON_Delete(arr);
    return ret;
}

// PUT /api/ports/<id>/config - update port line coding
esp_err_t api_put_port_config_handler(httpd_req_t *req)
{
    // Parse port ID from URI: /api/ports/<id>/config
    int port_id = -1;
    sscanf(req->uri, "/api/ports/%d", &port_id);
    if (port_id < 0 || port_id >= PORT_MAX_COUNT) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid port ID");
        return ESP_OK;
    }

    port_t *port = port_registry_get(port_id);
    if (!port) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Port not found");
        return ESP_OK;
    }

    char *body = read_body(req);
    if (!body) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing body");
        return ESP_OK;
    }

    cJSON *json = cJSON_Parse(body);
    free(body);
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_OK;
    }

    // Update line coding
    port_line_coding_t coding = port->line_coding;
    cJSON *lc = cJSON_GetObjectItem(json, "lineCoding");
    if (lc) {
        cJSON *br = cJSON_GetObjectItem(lc, "baudRate");
        if (br) coding.baud_rate = br->valueint;
        cJSON *db = cJSON_GetObjectItem(lc, "dataBits");
        if (db) coding.data_bits = db->valueint;
        cJSON *sb = cJSON_GetObjectItem(lc, "stopBits");
        if (sb) coding.stop_bits = sb->valueint;
        cJSON *par = cJSON_GetObjectItem(lc, "parity");
        if (par) coding.parity = par->valueint;
        cJSON *fc = cJSON_GetObjectItem(lc, "flowControl");
        if (fc) coding.flow_control = cJSON_IsTrue(fc);
    }

    // Update signal overrides
    cJSON *overrides = cJSON_GetObjectItem(json, "signalOverrides");
    if (overrides) {
        cJSON *mask = cJSON_GetObjectItem(overrides, "mask");
        cJSON *val = cJSON_GetObjectItem(overrides, "values");
        if (mask) port->signal_override = mask->valueint;
        if (val)  port->signal_override_val = val->valueint;
    }

    if (port->ops.set_line_coding) {
        port->ops.set_line_coding(port, &coding);
    }
    port->line_coding = coding;

    cJSON_Delete(json);

    // Respond with updated port
    cJSON *resp = port_to_json(port);
    esp_err_t ret = send_json(req, resp);
    cJSON_Delete(resp);
    return ret;
}

// GET /api/routes
esp_err_t api_get_routes_handler(httpd_req_t *req)
{
    route_t routes[ROUTE_MAX_COUNT];
    int count = route_get_all(routes, ROUTE_MAX_COUNT);

    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < count; i++) {
        cJSON_AddItemToArray(arr, route_to_json(&routes[i]));
    }

    esp_err_t ret = send_json(req, arr);
    cJSON_Delete(arr);
    return ret;
}

// PUT /api/routes - create a new route
esp_err_t api_put_routes_handler(httpd_req_t *req)
{
    char *body = read_body(req);
    if (!body) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing body");
        return ESP_OK;
    }

    cJSON *json = cJSON_Parse(body);
    free(body);
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_OK;
    }

    route_t r = {0};

    cJSON *type = cJSON_GetObjectItem(json, "type");
    if (type) r.type = type->valueint;

    cJSON *src = cJSON_GetObjectItem(json, "srcPortId");
    if (src) r.src_port_id = src->valueint;

    cJSON *dsts = cJSON_GetObjectItem(json, "dstPortIds");
    if (dsts && cJSON_IsArray(dsts)) {
        r.dst_count = cJSON_GetArraySize(dsts);
        if (r.dst_count > ROUTE_MAX_DEST) r.dst_count = ROUTE_MAX_DEST;
        for (int i = 0; i < r.dst_count; i++) {
            r.dst_port_ids[i] = cJSON_GetArrayItem(dsts, i)->valueint;
        }
    }

    cJSON *maps = cJSON_GetObjectItem(json, "signalMap");
    if (maps && cJSON_IsArray(maps)) {
        r.signal_map_count = cJSON_GetArraySize(maps);
        if (r.signal_map_count > 8) r.signal_map_count = 8;
        for (int i = 0; i < r.signal_map_count; i++) {
            cJSON *m = cJSON_GetArrayItem(maps, i);
            r.signal_map[i].from_signal = cJSON_GetObjectItem(m, "fromSignal")->valueint;
            r.signal_map[i].to_signal = cJSON_GetObjectItem(m, "toSignal")->valueint;
        }
    }

    uint8_t route_id;
    esp_err_t ret = route_create(&r, &route_id);
    if (ret != ESP_OK) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to create route");
        return ESP_OK;
    }

    // Auto-start the route
    route_start(route_id);

    cJSON_Delete(json);

    // Respond with created route
    route_t *created = route_get(route_id);
    if (created) {
        cJSON *resp = route_to_json(created);
        ret = send_json(req, resp);
        cJSON_Delete(resp);
    } else {
        httpd_resp_sendstr(req, "{\"id\":" );
    }
    return ret;
}

// DELETE /api/routes/<id>
esp_err_t api_delete_route_handler(httpd_req_t *req)
{
    int route_id = -1;
    sscanf(req->uri, "/api/routes/%d", &route_id);
    if (route_id < 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid route ID");
        return ESP_OK;
    }

    esp_err_t ret = route_destroy(route_id);
    if (ret != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Route not found");
        return ESP_OK;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

// GET /api/config
esp_err_t api_get_config_handler(httpd_req_t *req)
{
    extern system_config_t sys_config;

    cJSON *obj = cJSON_CreateObject();

    // WiFi (don't expose password)
    cJSON *wifi = cJSON_CreateObject();
    cJSON_AddStringToObject(wifi, "ssid", sys_config.wifi_ssid);
    cJSON_AddStringToObject(wifi, "mode",
        wifi_mgr_get_mode() == WIFI_MGR_MODE_STA ? "sta" :
        wifi_mgr_get_mode() == WIFI_MGR_MODE_AP ? "ap" : "none");
    cJSON_AddStringToObject(wifi, "ip", wifi_mgr_get_ip());
    cJSON_AddBoolToObject(wifi, "connected", wifi_mgr_is_connected());
    cJSON_AddItemToObject(obj, "wifi", wifi);

    // TCP configs
    cJSON *tcp = cJSON_CreateArray();
    for (int i = 0; i < 4; i++) {
        cJSON *tc = cJSON_CreateObject();
        cJSON_AddStringToObject(tc, "host", sys_config.tcp_configs[i].host);
        cJSON_AddNumberToObject(tc, "port", sys_config.tcp_configs[i].port);
        cJSON_AddBoolToObject(tc, "isServer", sys_config.tcp_configs[i].is_server);
        cJSON_AddItemToArray(tcp, tc);
    }
    cJSON_AddItemToObject(obj, "tcpConfigs", tcp);

    esp_err_t ret = send_json(req, obj);
    cJSON_Delete(obj);
    return ret;
}

// PUT /api/config - update WiFi credentials and/or TCP configs
esp_err_t api_put_config_handler(httpd_req_t *req)
{
    extern system_config_t sys_config;

    char *body = read_body(req);
    if (!body) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing body");
        return ESP_OK;
    }

    cJSON *json = cJSON_Parse(body);
    free(body);
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_OK;
    }

    bool wifi_changed = false;

    // Update WiFi credentials
    cJSON *wifi = cJSON_GetObjectItem(json, "wifi");
    if (wifi) {
        cJSON *ssid = cJSON_GetObjectItem(wifi, "ssid");
        cJSON *pass = cJSON_GetObjectItem(wifi, "password");
        if (ssid && cJSON_IsString(ssid)) {
            strncpy(sys_config.wifi_ssid, ssid->valuestring, sizeof(sys_config.wifi_ssid) - 1);
            wifi_changed = true;
        }
        if (pass && cJSON_IsString(pass)) {
            strncpy(sys_config.wifi_pass, pass->valuestring, sizeof(sys_config.wifi_pass) - 1);
            wifi_changed = true;
        }
    }

    // Update TCP configs
    cJSON *tcp = cJSON_GetObjectItem(json, "tcpConfigs");
    if (tcp && cJSON_IsArray(tcp)) {
        int count = cJSON_GetArraySize(tcp);
        if (count > 4) count = 4;
        for (int i = 0; i < count; i++) {
            cJSON *tc = cJSON_GetArrayItem(tcp, i);
            cJSON *host = cJSON_GetObjectItem(tc, "host");
            cJSON *port = cJSON_GetObjectItem(tc, "port");
            cJSON *is_server = cJSON_GetObjectItem(tc, "isServer");

            if (host && cJSON_IsString(host))
                strncpy(sys_config.tcp_configs[i].host, host->valuestring, sizeof(sys_config.tcp_configs[i].host) - 1);
            if (port) sys_config.tcp_configs[i].port = port->valueint;
            if (is_server) sys_config.tcp_configs[i].is_server = cJSON_IsTrue(is_server);
        }
    }

    // Save config
    config_store_save(&sys_config);

    cJSON_Delete(json);

    // Send response BEFORE switching WiFi (switching kills the AP connection)
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_sendstr(req, wifi_changed ? "{\"ok\":true,\"wifiChanging\":true}" : "{\"ok\":true}");

    // Defer WiFi switch so the HTTP response has time to reach the client
    if (wifi_changed && strlen(sys_config.wifi_ssid) > 0) {
        strncpy(deferred_ssid, sys_config.wifi_ssid, sizeof(deferred_ssid) - 1);
        deferred_ssid[sizeof(deferred_ssid) - 1] = '\0';
        strncpy(deferred_pass, sys_config.wifi_pass, sizeof(deferred_pass) - 1);
        deferred_pass[sizeof(deferred_pass) - 1] = '\0';

        const esp_timer_create_args_t timer_args = {
            .callback = deferred_wifi_switch_cb,
            .name = "wifi_switch",
        };
        esp_timer_handle_t timer;
        if (esp_timer_create(&timer_args, &timer) == ESP_OK) {
            // 500ms delay gives HTTP response time to flush
            esp_timer_start_once(timer, 500 * 1000);
            ESP_LOGI(TAG, "WiFi switch deferred by 500ms");
        } else {
            // Fallback: switch immediately if timer fails
            ESP_LOGW(TAG, "Timer create failed, switching WiFi immediately");
            wifi_mgr_set_credentials(deferred_ssid,
                strlen(deferred_pass) > 0 ? deferred_pass : NULL);
        }
    }

    return ESP_OK;
}

// POST /api/config/reset
esp_err_t api_post_config_reset_handler(httpd_req_t *req)
{
    config_store_reset();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_sendstr(req, "{\"ok\":true,\"message\":\"Config reset. Reboot to apply.\"}");
    return ESP_OK;
}

// GET /api/system - system info
esp_err_t api_get_system_handler(httpd_req_t *req)
{
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "firmware", "ESP32 Virtual UART");
    cJSON_AddStringToObject(obj, "version", "0.1.0");
    cJSON_AddNumberToObject(obj, "portCount", port_registry_count());
    cJSON_AddNumberToObject(obj, "activeRoutes", route_active_count());
    cJSON_AddNumberToObject(obj, "freeHeap", (int)esp_get_free_heap_size());
    cJSON_AddNumberToObject(obj, "uptime", (int)(xTaskGetTickCount() / configTICK_RATE_HZ));

    esp_err_t ret = send_json(req, obj);
    cJSON_Delete(obj);
    return ret;
}
