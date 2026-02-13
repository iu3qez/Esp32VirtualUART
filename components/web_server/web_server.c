#include "web_server.h"
#include "esp_http_server.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include <string.h>
#include <sys/stat.h>

static const char *TAG = "web_server";

static httpd_handle_t server = NULL;

// Forward declarations from api_handler.c
esp_err_t api_get_ports_handler(httpd_req_t *req);
esp_err_t api_put_port_config_handler(httpd_req_t *req);
esp_err_t api_get_routes_handler(httpd_req_t *req);
esp_err_t api_put_routes_handler(httpd_req_t *req);
esp_err_t api_delete_route_handler(httpd_req_t *req);
esp_err_t api_get_config_handler(httpd_req_t *req);
esp_err_t api_put_config_handler(httpd_req_t *req);
esp_err_t api_post_config_reset_handler(httpd_req_t *req);
esp_err_t api_get_system_handler(httpd_req_t *req);

// Forward declarations from ws_handler.c
esp_err_t ws_signals_handler(httpd_req_t *req);
esp_err_t ws_monitor_handler(httpd_req_t *req);
void ws_init(httpd_handle_t server_handle);
void ws_cleanup(void);

// Content-type lookup
static const char *get_content_type(const char *path)
{
    const char *ext = strrchr(path, '.');
    if (!ext) return "application/octet-stream";

    if (strcmp(ext, ".html") == 0) return "text/html";
    if (strcmp(ext, ".js") == 0)   return "application/javascript";
    if (strcmp(ext, ".css") == 0)  return "text/css";
    if (strcmp(ext, ".json") == 0) return "application/json";
    if (strcmp(ext, ".svg") == 0)  return "image/svg+xml";
    if (strcmp(ext, ".png") == 0)  return "image/png";
    if (strcmp(ext, ".ico") == 0)  return "image/x-icon";
    if (strcmp(ext, ".woff") == 0) return "font/woff";
    if (strcmp(ext, ".woff2") == 0) return "font/woff2";
    return "application/octet-stream";
}

// Serve static files from LittleFS
static esp_err_t static_file_handler(httpd_req_t *req)
{
    char filepath[128];
    const char *uri = req->uri;

    // Strip query string
    const char *query = strchr(uri, '?');
    size_t uri_len = query ? (size_t)(query - uri) : strlen(uri);

    // Build file path
    if (uri_len == 1 && uri[0] == '/') {
        snprintf(filepath, sizeof(filepath), "/littlefs/www/index.html");
    } else {
        snprintf(filepath, sizeof(filepath), "/littlefs/www%.*s", (int)uri_len, uri);
    }

    // Check if gzipped version exists
    char gz_path[136];
    snprintf(gz_path, sizeof(gz_path), "%s.gz", filepath);

    struct stat st;
    bool use_gzip = false;
    if (stat(gz_path, &st) == 0) {
        use_gzip = true;
        strncpy(filepath, gz_path, sizeof(filepath) - 1);
    } else if (stat(filepath, &st) != 0) {
        // File not found - SPA fallback: serve index.html
        snprintf(filepath, sizeof(filepath), "/littlefs/www/index.html");
        if (stat(filepath, &st) != 0) {
            httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found");
            return ESP_OK;
        }
    }

    FILE *f = fopen(filepath, "r");
    if (!f) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to open file");
        return ESP_OK;
    }

    // Set content type (use original path, not .gz)
    const char *content_type = get_content_type(use_gzip ? filepath + strlen(filepath) - 3 - 1 : filepath);
    // For gz files, derive content type from the original extension
    if (use_gzip) {
        char orig_path[128];
        size_t gz_len = strlen(filepath);
        strncpy(orig_path, filepath, gz_len - 3); // strip ".gz"
        orig_path[gz_len - 3] = '\0';
        content_type = get_content_type(orig_path);
        httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    }
    httpd_resp_set_type(req, content_type);

    // Cache static assets (not index.html)
    if (strstr(filepath, "index.html") == NULL) {
        httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=86400");
    }

    // Stream file content
    char buf[512];
    size_t read_bytes;
    while ((read_bytes = fread(buf, 1, sizeof(buf), f)) > 0) {
        if (httpd_resp_send_chunk(req, buf, read_bytes) != ESP_OK) {
            fclose(f);
            httpd_resp_send_chunk(req, NULL, 0);
            return ESP_FAIL;
        }
    }
    fclose(f);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t init_littlefs(void)
{
    esp_vfs_littlefs_conf_t conf = {
        .base_path = "/littlefs",
        .partition_label = "storage",
        .format_if_mount_failed = true,
        .dont_mount = false,
    };

    esp_err_t ret = esp_vfs_littlefs_register(&conf);
    if (ret != ESP_OK) {
        if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGW(TAG, "LittleFS partition not found, web UI will not be available");
        } else {
            ESP_LOGE(TAG, "Failed to mount LittleFS: %s", esp_err_to_name(ret));
        }
        return ret;
    }

    size_t total = 0, used = 0;
    esp_littlefs_info("storage", &total, &used);
    ESP_LOGI(TAG, "LittleFS: total=%d, used=%d", (int)total, (int)used);
    return ESP_OK;
}

esp_err_t web_server_start(void)
{
    if (server) {
        ESP_LOGW(TAG, "Server already running");
        return ESP_OK;
    }

    // Try to mount LittleFS (not fatal if it fails - API still works)
    init_littlefs();

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 16;
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.stack_size = 8192;

    esp_err_t ret = httpd_start(&server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(ret));
        return ret;
    }

    // Initialize WebSocket handler
    ws_init(server);

    // Register API endpoints (higher priority = matched first)

    // System info
    httpd_uri_t system_uri = {
        .uri = "/api/system",
        .method = HTTP_GET,
        .handler = api_get_system_handler,
    };
    httpd_register_uri_handler(server, &system_uri);

    // Ports
    httpd_uri_t ports_get_uri = {
        .uri = "/api/ports",
        .method = HTTP_GET,
        .handler = api_get_ports_handler,
    };
    httpd_register_uri_handler(server, &ports_get_uri);

    httpd_uri_t port_config_uri = {
        .uri = "/api/ports/*",
        .method = HTTP_PUT,
        .handler = api_put_port_config_handler,
    };
    httpd_register_uri_handler(server, &port_config_uri);

    // Routes
    httpd_uri_t routes_get_uri = {
        .uri = "/api/routes",
        .method = HTTP_GET,
        .handler = api_get_routes_handler,
    };
    httpd_register_uri_handler(server, &routes_get_uri);

    httpd_uri_t routes_put_uri = {
        .uri = "/api/routes",
        .method = HTTP_PUT,
        .handler = api_put_routes_handler,
    };
    httpd_register_uri_handler(server, &routes_put_uri);

    httpd_uri_t route_delete_uri = {
        .uri = "/api/routes/*",
        .method = HTTP_DELETE,
        .handler = api_delete_route_handler,
    };
    httpd_register_uri_handler(server, &route_delete_uri);

    // Config
    httpd_uri_t config_get_uri = {
        .uri = "/api/config",
        .method = HTTP_GET,
        .handler = api_get_config_handler,
    };
    httpd_register_uri_handler(server, &config_get_uri);

    httpd_uri_t config_put_uri = {
        .uri = "/api/config",
        .method = HTTP_PUT,
        .handler = api_put_config_handler,
    };
    httpd_register_uri_handler(server, &config_put_uri);

    httpd_uri_t config_reset_uri = {
        .uri = "/api/config/reset",
        .method = HTTP_POST,
        .handler = api_post_config_reset_handler,
    };
    httpd_register_uri_handler(server, &config_reset_uri);

    // WebSocket endpoints
    httpd_uri_t ws_signals_uri = {
        .uri = "/ws/signals",
        .method = HTTP_GET,
        .handler = ws_signals_handler,
        .is_websocket = true,
    };
    httpd_register_uri_handler(server, &ws_signals_uri);

    httpd_uri_t ws_monitor_uri = {
        .uri = "/ws/monitor",
        .method = HTTP_GET,
        .handler = ws_monitor_handler,
        .is_websocket = true,
    };
    httpd_register_uri_handler(server, &ws_monitor_uri);

    // Static file serving (wildcard, lowest priority - registered last)
    httpd_uri_t static_uri = {
        .uri = "/*",
        .method = HTTP_GET,
        .handler = static_file_handler,
    };
    httpd_register_uri_handler(server, &static_uri);

    ESP_LOGI(TAG, "Web server started on port %d", config.server_port);
    return ESP_OK;
}

void web_server_stop(void)
{
    if (server) {
        ws_cleanup();
        httpd_stop(server);
        server = NULL;
        ESP_LOGI(TAG, "Web server stopped");
    }
    esp_vfs_littlefs_unregister("storage");
}

void web_server_notify_signal_change(uint8_t port_id, uint32_t signals)
{
    // Delegate to ws_handler
    extern void ws_broadcast_signal(uint8_t port_id, uint32_t signals);
    ws_broadcast_signal(port_id, signals);
}

void web_server_notify_data_flow(uint8_t route_id, uint32_t bytes_src_to_dst, uint32_t bytes_dst_to_src)
{
    extern void ws_broadcast_data_flow(uint8_t route_id, uint32_t bytes_src_to_dst, uint32_t bytes_dst_to_src);
    ws_broadcast_data_flow(route_id, bytes_src_to_dst, bytes_dst_to_src);
}
