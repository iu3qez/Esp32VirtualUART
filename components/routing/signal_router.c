#include "signal_router.h"
#include "port_registry.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "sig_router";

#define SIGNAL_POLL_INTERVAL_MS  10

static TaskHandle_t signal_task_handle = NULL;
static volatile bool signal_task_running = false;

static void apply_signal_mappings(route_t *r)
{
    if (r->signal_map_count == 0) return;

    port_t *src = port_registry_get(r->src_port_id);
    if (!src) return;

    uint32_t src_signals = 0;
    if (src->ops.get_signals) {
        src->ops.get_signals(src, &src_signals);
    }

    // For each destination, compute mapped signals and apply
    for (int d = 0; d < r->dst_count; d++) {
        port_t *dst = port_registry_get(r->dst_port_ids[d]);
        if (!dst || !dst->ops.set_signals) continue;

        uint32_t dst_signals = 0;
        // Read current destination signals to preserve unmapped ones
        if (dst->ops.get_signals) {
            dst->ops.get_signals(dst, &dst_signals);
        }

        for (int m = 0; m < r->signal_map_count; m++) {
            uint8_t from = r->signal_map[m].from_signal;
            uint8_t to = r->signal_map[m].to_signal;

            if (src_signals & from) {
                dst_signals |= to;
            } else {
                dst_signals &= ~to;
            }
        }

        dst->ops.set_signals(dst, dst_signals);
    }

    // For bridge routes: also map in reverse direction
    if (r->type == ROUTE_TYPE_BRIDGE && r->dst_count > 0) {
        port_t *dst0 = port_registry_get(r->dst_port_ids[0]);
        if (!dst0) return;

        uint32_t dst_signals = 0;
        if (dst0->ops.get_signals) {
            dst0->ops.get_signals(dst0, &dst_signals);
        }

        uint32_t rev_signals = 0;
        if (src->ops.get_signals) {
            src->ops.get_signals(src, &rev_signals);
        }

        for (int m = 0; m < r->signal_map_count; m++) {
            // Reverse the mapping for bridge: to -> from
            uint8_t from = r->signal_map[m].to_signal;
            uint8_t to = r->signal_map[m].from_signal;

            if (dst_signals & from) {
                rev_signals |= to;
            } else {
                rev_signals &= ~to;
            }
        }

        src->ops.set_signals(src, rev_signals);
    }
}

static void signal_router_task(void *arg)
{
    ESP_LOGI(TAG, "Signal router started (poll every %d ms)", SIGNAL_POLL_INTERVAL_MS);

    while (signal_task_running) {
        // Get all active routes
        route_t all_routes[ROUTE_MAX_COUNT];
        int count = route_get_all(all_routes, ROUTE_MAX_COUNT);

        for (int i = 0; i < count; i++) {
            if (all_routes[i].active && all_routes[i].signal_map_count > 0) {
                apply_signal_mappings(&all_routes[i]);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(SIGNAL_POLL_INTERVAL_MS));
    }

    ESP_LOGI(TAG, "Signal router stopped");
    vTaskDelete(NULL);
}

esp_err_t signal_router_init(void)
{
    if (signal_task_handle) {
        ESP_LOGW(TAG, "Signal router already running");
        return ESP_OK;
    }

    signal_task_running = true;
    BaseType_t ret = xTaskCreate(signal_router_task, "sig_router", 3072, NULL, 4, &signal_task_handle);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create signal router task");
        signal_task_running = false;
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

void signal_router_stop(void)
{
    if (!signal_task_handle) return;

    signal_task_running = false;
    vTaskDelay(pdMS_TO_TICKS(SIGNAL_POLL_INTERVAL_MS * 3));
    signal_task_handle = NULL;
    ESP_LOGI(TAG, "Signal router stopped");
}
