#include "route.h"
#include "port_registry.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "route";

#define FORWARD_BUF_SIZE    256
#define FORWARD_STACK_SIZE  4096

static route_t routes[ROUTE_MAX_COUNT];
static SemaphoreHandle_t route_mutex;
static uint8_t next_route_id = 0;

// --- Forwarding task context ---

typedef struct {
    port_t          *src;
    port_t          *dst[ROUTE_MAX_DEST];
    int              dst_count;
    volatile bool   *running;
    volatile uint32_t *bytes_counter;
} forward_ctx_t;

static void forward_task(void *arg)
{
    forward_ctx_t *ctx = (forward_ctx_t *)arg;
    uint8_t buf[FORWARD_BUF_SIZE];

    ESP_LOGI(TAG, "Forwarding %s -> %d dest(s) started", ctx->src->name, ctx->dst_count);

    while (*ctx->running) {
        int n = ctx->src->ops.read(ctx->src, buf, sizeof(buf), pdMS_TO_TICKS(50));
        if (n > 0) {
            for (int i = 0; i < ctx->dst_count; i++) {
                if (ctx->dst[i] && ctx->dst[i]->state >= PORT_STATE_READY) {
                    ctx->dst[i]->ops.write(ctx->dst[i], buf, n, pdMS_TO_TICKS(100));
                }
            }
            *ctx->bytes_counter += n;
        }
    }

    ESP_LOGI(TAG, "Forwarding %s stopped", ctx->src->name);
    free(ctx);
    vTaskDelete(NULL);
}

// --- Public API ---

esp_err_t route_engine_init(void)
{
    route_mutex = xSemaphoreCreateMutex();
    if (!route_mutex) {
        ESP_LOGE(TAG, "Failed to create route mutex");
        return ESP_ERR_NO_MEM;
    }
    memset(routes, 0, sizeof(routes));
    next_route_id = 0;
    ESP_LOGI(TAG, "Route engine initialized (max %d routes)", ROUTE_MAX_COUNT);
    return ESP_OK;
}

esp_err_t route_create(const route_t *config, uint8_t *route_id_out)
{
    xSemaphoreTake(route_mutex, portMAX_DELAY);

    // Find free slot
    int slot = -1;
    for (int i = 0; i < ROUTE_MAX_COUNT; i++) {
        if (!routes[i].active && routes[i].task_count == 0) {
            slot = i;
            break;
        }
    }

    if (slot < 0) {
        xSemaphoreGive(route_mutex);
        ESP_LOGE(TAG, "No free route slots");
        return ESP_ERR_NO_MEM;
    }

    // Validate ports exist
    port_t *src = port_registry_get(config->src_port_id);
    if (!src) {
        xSemaphoreGive(route_mutex);
        ESP_LOGE(TAG, "Source port %d not found", config->src_port_id);
        return ESP_ERR_NOT_FOUND;
    }

    for (int i = 0; i < config->dst_count; i++) {
        port_t *dst = port_registry_get(config->dst_port_ids[i]);
        if (!dst) {
            xSemaphoreGive(route_mutex);
            ESP_LOGE(TAG, "Destination port %d not found", config->dst_port_ids[i]);
            return ESP_ERR_NOT_FOUND;
        }
    }

    // Copy config
    routes[slot] = *config;
    routes[slot].id = next_route_id++;
    routes[slot].active = true;
    routes[slot].task_count = 0;
    routes[slot].bytes_fwd_src_to_dst = 0;
    routes[slot].bytes_fwd_dst_to_src = 0;
    memset(routes[slot].task_handles, 0, sizeof(routes[slot].task_handles));

    if (route_id_out) {
        *route_id_out = routes[slot].id;
    }

    ESP_LOGI(TAG, "Route %d created: type=%d src=%d dst_count=%d",
             routes[slot].id, config->type, config->src_port_id, config->dst_count);

    xSemaphoreGive(route_mutex);
    return ESP_OK;
}

esp_err_t route_start(uint8_t route_id)
{
    xSemaphoreTake(route_mutex, portMAX_DELAY);

    route_t *r = NULL;
    for (int i = 0; i < ROUTE_MAX_COUNT; i++) {
        if (routes[i].active && routes[i].id == route_id) {
            r = &routes[i];
            break;
        }
    }

    if (!r) {
        xSemaphoreGive(route_mutex);
        return ESP_ERR_NOT_FOUND;
    }

    if (r->task_count > 0) {
        xSemaphoreGive(route_mutex);
        ESP_LOGW(TAG, "Route %d already running", route_id);
        return ESP_OK;
    }

    port_t *src = port_registry_get(r->src_port_id);
    if (!src) {
        xSemaphoreGive(route_mutex);
        return ESP_ERR_INVALID_STATE;
    }

    // Open source port if not already
    if (src->state == PORT_STATE_DISABLED && src->ops.open) {
        src->ops.open(src);
    }

    // Forward task: src -> destinations
    {
        forward_ctx_t *ctx = calloc(1, sizeof(forward_ctx_t));
        if (!ctx) {
            xSemaphoreGive(route_mutex);
            return ESP_ERR_NO_MEM;
        }
        ctx->src = src;
        ctx->dst_count = r->dst_count;
        for (int i = 0; i < r->dst_count; i++) {
            ctx->dst[i] = port_registry_get(r->dst_port_ids[i]);
            // Open destination port if needed
            if (ctx->dst[i] && ctx->dst[i]->state == PORT_STATE_DISABLED && ctx->dst[i]->ops.open) {
                ctx->dst[i]->ops.open(ctx->dst[i]);
            }
        }
        ctx->running = &r->active;
        ctx->bytes_counter = &r->bytes_fwd_src_to_dst;

        char name[16];
        snprintf(name, sizeof(name), "fwd_%d_ab", route_id);
        xTaskCreate(forward_task, name, FORWARD_STACK_SIZE, ctx, 5, &r->task_handles[0]);
        r->task_count++;
    }

    // For bridge: also create reverse direction (dst[0] -> src)
    if (r->type == ROUTE_TYPE_BRIDGE && r->dst_count > 0) {
        port_t *dst0 = port_registry_get(r->dst_port_ids[0]);
        if (dst0) {
            forward_ctx_t *ctx = calloc(1, sizeof(forward_ctx_t));
            if (!ctx) {
                xSemaphoreGive(route_mutex);
                return ESP_ERR_NO_MEM;
            }
            ctx->src = dst0;
            ctx->dst[0] = src;
            ctx->dst_count = 1;
            ctx->running = &r->active;
            ctx->bytes_counter = &r->bytes_fwd_dst_to_src;

            char name[16];
            snprintf(name, sizeof(name), "fwd_%d_ba", route_id);
            xTaskCreate(forward_task, name, FORWARD_STACK_SIZE, ctx, 5, &r->task_handles[1]);
            r->task_count++;
        }
    }

    ESP_LOGI(TAG, "Route %d started (%d task(s))", route_id, r->task_count);
    xSemaphoreGive(route_mutex);
    return ESP_OK;
}

esp_err_t route_stop(uint8_t route_id)
{
    xSemaphoreTake(route_mutex, portMAX_DELAY);

    route_t *r = NULL;
    for (int i = 0; i < ROUTE_MAX_COUNT; i++) {
        if (routes[i].id == route_id && routes[i].task_count > 0) {
            r = &routes[i];
            break;
        }
    }

    if (!r) {
        xSemaphoreGive(route_mutex);
        return ESP_ERR_NOT_FOUND;
    }

    // Signal tasks to stop (they check r->active)
    r->active = false;

    xSemaphoreGive(route_mutex);

    // Wait for tasks to finish (they self-delete)
    vTaskDelay(pdMS_TO_TICKS(200));

    xSemaphoreTake(route_mutex, portMAX_DELAY);
    r->task_count = 0;
    memset(r->task_handles, 0, sizeof(r->task_handles));
    ESP_LOGI(TAG, "Route %d stopped", route_id);
    xSemaphoreGive(route_mutex);

    return ESP_OK;
}

esp_err_t route_destroy(uint8_t route_id)
{
    // Stop first if running
    route_stop(route_id);

    xSemaphoreTake(route_mutex, portMAX_DELAY);

    for (int i = 0; i < ROUTE_MAX_COUNT; i++) {
        if (routes[i].id == route_id) {
            ESP_LOGI(TAG, "Route %d destroyed", route_id);
            memset(&routes[i], 0, sizeof(route_t));
            xSemaphoreGive(route_mutex);
            return ESP_OK;
        }
    }

    xSemaphoreGive(route_mutex);
    return ESP_ERR_NOT_FOUND;
}

int route_get_all(route_t *out, int max_count)
{
    xSemaphoreTake(route_mutex, portMAX_DELAY);

    int count = 0;
    for (int i = 0; i < ROUTE_MAX_COUNT && count < max_count; i++) {
        if (routes[i].active || routes[i].task_count > 0) {
            out[count++] = routes[i];
        }
    }

    xSemaphoreGive(route_mutex);
    return count;
}

route_t *route_get(uint8_t route_id)
{
    xSemaphoreTake(route_mutex, portMAX_DELAY);

    for (int i = 0; i < ROUTE_MAX_COUNT; i++) {
        if (routes[i].active && routes[i].id == route_id) {
            xSemaphoreGive(route_mutex);
            return &routes[i];
        }
    }

    xSemaphoreGive(route_mutex);
    return NULL;
}

int route_active_count(void)
{
    xSemaphoreTake(route_mutex, portMAX_DELAY);
    int count = 0;
    for (int i = 0; i < ROUTE_MAX_COUNT; i++) {
        if (routes[i].active && routes[i].task_count > 0) {
            count++;
        }
    }
    xSemaphoreGive(route_mutex);
    return count;
}

void route_reset_counters(uint8_t route_id)
{
    route_t *r = route_get(route_id);
    if (r) {
        r->bytes_fwd_src_to_dst = 0;
        r->bytes_fwd_dst_to_src = 0;
    }
}
