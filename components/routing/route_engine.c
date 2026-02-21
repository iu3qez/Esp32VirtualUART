#include "route.h"
#include "port_registry.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "route";

#define FORWARD_BUF_SIZE    256
#define FORWARD_STACK_SIZE  4096

// ---------------------------------------------------------------------------
// Private per-route runtime state (not exposed in route.h)
// ---------------------------------------------------------------------------

typedef struct {
    QueueHandle_t       fwd_src_queue;
    QueueHandle_t       rev_src_queue;
    SemaphoreHandle_t   done_sem;       // counting semaphore for task join
} route_runtime_t;

static route_runtime_t route_rt[ROUTE_MAX_COUNT];

// ---------------------------------------------------------------------------
// Fan-out source reader
//
// FreeRTOS stream buffers support exactly ONE blocked reader.  When multiple
// routes share the same source port each forward_task cannot call
// xStreamBufferReceive directly -- only one would block successfully and the
// second would assert (stream_buffer.c:976).
//
// Solution: one pump task per source port reads from port->rx_buf (the sole
// reader) and fans the data out to per-route subscriber queues.  Each
// forward_task reads from its own private queue instead of from the port.
// ---------------------------------------------------------------------------

#define SRC_READER_MAX   8   // max distinct source ports active simultaneously
#define SRC_SUB_MAX      4   // max simultaneous routes sharing one source (6 CDC + 2 UART is fine)
#define SRC_SUB_Q_DEPTH  8   // depth of each per-route subscriber queue
// TODO: consider static chunk pool if heap pressure appears under load (8MB PSRAM available)

// A single heap-allocated chunk carried through subscriber queues.
// route_stop() drains and frees any residual chunks after tasks exit.
typedef struct {
    uint8_t *data;
    uint16_t len;
} fanout_chunk_t;

typedef struct {
    QueueHandle_t queue;
    bool          active;
} src_sub_t;

typedef struct {
    port_t           *src;
    volatile bool     running;
    int               ref_count;
    src_sub_t         subs[SRC_SUB_MAX];
    SemaphoreHandle_t mutex;
    TaskHandle_t      task;
    SemaphoreHandle_t pump_done;    // signaled by pump task before exit
} src_reader_t;

static src_reader_t       src_readers[SRC_READER_MAX];
static SemaphoreHandle_t  src_reader_mutex;

// Pump task: sole reader of port->rx_buf, fans data to all subscriber queues.
static void src_pump_task(void *arg)
{
    src_reader_t *sr = (src_reader_t *)arg;
    uint8_t buf[FORWARD_BUF_SIZE];

    ESP_LOGI(TAG, "Pump %s started", sr->src->name);

    while (sr->running) {
        int n = sr->src->ops.read(sr->src, buf, sizeof(buf), pdMS_TO_TICKS(50));
        if (n <= 0) continue;

        xSemaphoreTake(sr->mutex, portMAX_DELAY);
        for (int i = 0; i < SRC_SUB_MAX; i++) {
            if (!sr->subs[i].active) continue;
            fanout_chunk_t chunk = {
                .data = malloc(n),
                .len  = (uint16_t)n,
            };
            if (!chunk.data) continue;
            memcpy(chunk.data, buf, n);
            if (xQueueSend(sr->subs[i].queue, &chunk, 0) != pdTRUE) {
                free(chunk.data); // subscriber queue full -- drop
                ESP_LOGW(TAG, "Pump %s: sub %d queue full, dropped %d bytes",
                         sr->src->name, i, n);
            }
        }
        xSemaphoreGive(sr->mutex);
    }

    ESP_LOGI(TAG, "Pump %s stopped", sr->src->name);
    xSemaphoreGive(sr->pump_done);
    vTaskDelete(NULL);
}

// Subscribe to a source port.  Creates a pump task the first time.
// Returns the subscriber queue to read from, or NULL on error.
static QueueHandle_t src_subscribe(port_t *src)
{
    // Pre-allocate queue outside locks to avoid priority inversion (NB-7).
    QueueHandle_t q = xQueueCreate(SRC_SUB_Q_DEPTH, sizeof(fanout_chunk_t));
    if (!q) {
        ESP_LOGE(TAG, "Failed to create subscriber queue");
        return NULL;
    }

    xSemaphoreTake(src_reader_mutex, portMAX_DELAY);

    // Find existing reader for this source, or allocate a new slot.
    src_reader_t *sr = NULL;
    for (int i = 0; i < SRC_READER_MAX; i++) {
        if (src_readers[i].src == src) { sr = &src_readers[i]; break; }
    }
    if (!sr) {
        for (int i = 0; i < SRC_READER_MAX; i++) {
            if (!src_readers[i].src) { sr = &src_readers[i]; break; }
        }
        if (!sr) {
            xSemaphoreGive(src_reader_mutex);
            vQueueDelete(q);
            ESP_LOGE(TAG, "No free src_reader slots");
            return NULL;
        }
        memset(sr, 0, sizeof(*sr));
        sr->src     = src;
        sr->running = true;
        sr->mutex   = xSemaphoreCreateMutex();
        if (!sr->mutex) {
            sr->src = NULL;
            xSemaphoreGive(src_reader_mutex);
            vQueueDelete(q);
            ESP_LOGE(TAG, "Failed to create src_reader mutex");
            return NULL;
        }
        sr->pump_done = xSemaphoreCreateCounting(1, 0);
        if (!sr->pump_done) {
            vSemaphoreDelete(sr->mutex);
            memset(sr, 0, sizeof(*sr));
            xSemaphoreGive(src_reader_mutex);
            vQueueDelete(q);
            ESP_LOGE(TAG, "Failed to create pump_done semaphore");
            return NULL;
        }
        char name[PORT_NAME_MAX + 6]; // "pump_" + name + NUL
        snprintf(name, sizeof(name), "pump_%s", src->name);
        BaseType_t ret = xTaskCreate(src_pump_task, name, FORWARD_STACK_SIZE, sr, 5, &sr->task);
        if (ret != pdPASS) {
            vSemaphoreDelete(sr->pump_done);
            vSemaphoreDelete(sr->mutex);
            memset(sr, 0, sizeof(*sr));
            xSemaphoreGive(src_reader_mutex);
            vQueueDelete(q);
            ESP_LOGE(TAG, "Failed to create pump task for %s", src->name);
            return NULL;
        }
        ESP_LOGI(TAG, "Created pump for %s", src->name);
    }

    // Find a free subscriber slot and assign the pre-allocated queue.
    xSemaphoreTake(sr->mutex, portMAX_DELAY);
    bool found = false;
    for (int i = 0; i < SRC_SUB_MAX; i++) {
        if (!sr->subs[i].active) {
            sr->subs[i].queue  = q;
            sr->subs[i].active = true;
            sr->ref_count++;
            found = true;
            break;
        }
    }
    xSemaphoreGive(sr->mutex);
    xSemaphoreGive(src_reader_mutex);

    if (!found) {
        vQueueDelete(q);
        ESP_LOGE(TAG, "Too many subscribers on %s", src->name);
        return NULL;
    }
    return q;
}

// Unsubscribe a queue from its source.  Drains leftover chunks.
// Stops and destroys the pump task when the last subscriber is removed.
static void src_unsubscribe(port_t *src, QueueHandle_t q)
{
    if (!src || !q) return;

    xSemaphoreTake(src_reader_mutex, portMAX_DELAY);

    for (int i = 0; i < SRC_READER_MAX; i++) {
        src_reader_t *sr = &src_readers[i];
        if (sr->src != src) continue;

        xSemaphoreTake(sr->mutex, portMAX_DELAY);
        for (int j = 0; j < SRC_SUB_MAX; j++) {
            if (!sr->subs[j].active || sr->subs[j].queue != q) continue;
            // Drain residual chunks before deleting.
            fanout_chunk_t chunk;
            while (xQueueReceive(q, &chunk, 0) == pdTRUE) free(chunk.data);
            vQueueDelete(q);
            sr->subs[j].queue  = NULL;
            sr->subs[j].active = false;
            sr->ref_count--;
            break;
        }
        bool stop = (sr->ref_count == 0);
        if (stop) sr->running = false;
        xSemaphoreGive(sr->mutex);

        if (stop) {
            // Release global mutex while waiting for pump task to exit.
            xSemaphoreGive(src_reader_mutex);
            if (xSemaphoreTake(sr->pump_done, pdMS_TO_TICKS(1000)) != pdTRUE) {
                ESP_LOGW(TAG, "Pump %s did not exit in time", src->name);
            }
            xSemaphoreTake(src_reader_mutex, portMAX_DELAY);

            // Re-check: a new subscriber may have raced in while we waited (NB-3).
            xSemaphoreTake(sr->mutex, portMAX_DELAY);
            if (sr->ref_count == 0) {
                xSemaphoreGive(sr->mutex);
                vSemaphoreDelete(sr->mutex);
                vSemaphoreDelete(sr->pump_done);
                sr->src       = NULL;
                sr->task      = NULL;
                sr->pump_done = NULL;
                sr->mutex     = NULL;
                ESP_LOGI(TAG, "Pump for %s destroyed", src->name);
            } else {
                // A new subscriber arrived while pump was stopping -- restart it.
                sr->running = true;
                char name[PORT_NAME_MAX + 6];
                snprintf(name, sizeof(name), "pump_%s", src->name);
                BaseType_t ret = xTaskCreate(src_pump_task, name, FORWARD_STACK_SIZE,
                                             sr, 5, &sr->task);
                if (ret != pdPASS) {
                    ESP_LOGE(TAG, "Failed to restart pump for %s", src->name);
                }
                xSemaphoreGive(sr->mutex);
                ESP_LOGW(TAG, "Pump for %s restarted (subscriber raced in)", src->name);
            }
        }
        break;
    }

    xSemaphoreGive(src_reader_mutex);
}

// ---------------------------------------------------------------------------
// Forwarding task
// ---------------------------------------------------------------------------

typedef struct {
    port_t            *src;
    QueueHandle_t      src_queue;   // private subscriber queue from src_subscribe()
    port_t            *dst[ROUTE_MAX_DEST];
    int                dst_count;
    volatile bool     *running;
    volatile uint32_t *bytes_counter;
    SemaphoreHandle_t  done_sem;    // signaled before task exit
} forward_ctx_t;

static void forward_task(void *arg)
{
    forward_ctx_t *ctx = (forward_ctx_t *)arg;
    fanout_chunk_t chunk;

    ESP_LOGI(TAG, "Forwarding %s -> %d dest(s) started", ctx->src->name, ctx->dst_count);

    while (*ctx->running) {
        if (xQueueReceive(ctx->src_queue, &chunk, pdMS_TO_TICKS(50)) != pdTRUE) continue;
        for (int i = 0; i < ctx->dst_count; i++) {
            if (ctx->dst[i] && ctx->dst[i]->state >= PORT_STATE_READY) {
                ctx->dst[i]->ops.write(ctx->dst[i], chunk.data, chunk.len,
                                       pdMS_TO_TICKS(100));
            }
        }
        *ctx->bytes_counter += chunk.len;
        free(chunk.data);
    }

    // Drain any chunks the pump pushed after we stopped.
    while (xQueueReceive(ctx->src_queue, &chunk, 0) == pdTRUE) free(chunk.data);

    ESP_LOGI(TAG, "Forwarding %s stopped", ctx->src->name);
    SemaphoreHandle_t done = ctx->done_sem;
    free(ctx);
    xSemaphoreGive(done);
    vTaskDelete(NULL);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

static route_t           routes[ROUTE_MAX_COUNT];
static SemaphoreHandle_t route_mutex;
static uint8_t           next_route_id = 0;

esp_err_t route_engine_init(void)
{
    route_mutex      = xSemaphoreCreateMutex();
    src_reader_mutex = xSemaphoreCreateMutex();
    if (!route_mutex || !src_reader_mutex) {
        ESP_LOGE(TAG, "Failed to create mutexes");
        return ESP_ERR_NO_MEM;
    }
    memset(routes,      0, sizeof(routes));
    memset(route_rt,    0, sizeof(route_rt));
    memset(src_readers, 0, sizeof(src_readers));
    next_route_id = 0;
    ESP_LOGI(TAG, "Route engine initialized (max %d routes)", ROUTE_MAX_COUNT);
    return ESP_OK;
}

esp_err_t route_create(const route_t *config, uint8_t *route_id_out)
{
    xSemaphoreTake(route_mutex, portMAX_DELAY);

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

    port_t *src = port_registry_get(config->src_port_id);
    if (!src) {
        xSemaphoreGive(route_mutex);
        ESP_LOGE(TAG, "Source port %d not found", config->src_port_id);
        return ESP_ERR_NOT_FOUND;
    }
    for (int i = 0; i < config->dst_count; i++) {
        if (!port_registry_get(config->dst_port_ids[i])) {
            xSemaphoreGive(route_mutex);
            ESP_LOGE(TAG, "Destination port %d not found", config->dst_port_ids[i]);
            return ESP_ERR_NOT_FOUND;
        }
    }

    routes[slot] = *config;
    routes[slot].id                  = next_route_id++;
    routes[slot].active              = true;
    routes[slot].task_count          = 0;
    routes[slot].bytes_fwd_src_to_dst = 0;
    routes[slot].bytes_fwd_dst_to_src = 0;
    memset(routes[slot].task_handles, 0, sizeof(routes[slot].task_handles));
    memset(&route_rt[slot], 0, sizeof(route_rt[slot]));

    // Collision avoidance: skip IDs already in use (uint8 wraps at 256).
    for (int tries = 0; tries < 256; tries++) {
        bool collision = false;
        for (int j = 0; j < ROUTE_MAX_COUNT; j++) {
            if (j != slot && routes[j].active && routes[j].id == next_route_id) {
                collision = true;
                break;
            }
        }
        if (!collision) break;
        next_route_id++;
    }

    if (route_id_out) *route_id_out = routes[slot].id;

    ESP_LOGI(TAG, "Route %d created: type=%d src=%d dst_count=%d",
             routes[slot].id, config->type, config->src_port_id, config->dst_count);

    xSemaphoreGive(route_mutex);
    return ESP_OK;
}

esp_err_t route_start(uint8_t route_id)
{
    xSemaphoreTake(route_mutex, portMAX_DELAY);

    route_t *r = NULL;
    int slot = -1;
    for (int i = 0; i < ROUTE_MAX_COUNT; i++) {
        if (routes[i].active && routes[i].id == route_id) {
            r = &routes[i];
            slot = i;
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

    if (src->state == PORT_STATE_DISABLED && src->ops.open) src->ops.open(src);

    // Counting semaphore for task join (max 2 tasks for bridge).
    route_rt[slot].done_sem = xSemaphoreCreateCounting(2, 0);
    if (!route_rt[slot].done_sem) {
        xSemaphoreGive(route_mutex);
        return ESP_ERR_NO_MEM;
    }

    // Forward task: src -> destinations
    {
        forward_ctx_t *ctx = calloc(1, sizeof(forward_ctx_t));
        if (!ctx) {
            vSemaphoreDelete(route_rt[slot].done_sem);
            route_rt[slot].done_sem = NULL;
            xSemaphoreGive(route_mutex);
            return ESP_ERR_NO_MEM;
        }

        ctx->src       = src;
        ctx->dst_count = r->dst_count;
        for (int i = 0; i < r->dst_count; i++) {
            ctx->dst[i] = port_registry_get(r->dst_port_ids[i]);
            if (ctx->dst[i] && ctx->dst[i]->state == PORT_STATE_DISABLED
                    && ctx->dst[i]->ops.open) {
                ctx->dst[i]->ops.open(ctx->dst[i]);
            }
        }
        ctx->running       = &r->active;
        ctx->bytes_counter = &r->bytes_fwd_src_to_dst;
        ctx->done_sem      = route_rt[slot].done_sem;

        // Subscribe to source fan-out (safe for multiple routes on same port).
        xSemaphoreGive(route_mutex);
        QueueHandle_t q = src_subscribe(src);
        xSemaphoreTake(route_mutex, portMAX_DELAY);
        if (!q) {
            free(ctx);
            vSemaphoreDelete(route_rt[slot].done_sem);
            route_rt[slot].done_sem = NULL;
            xSemaphoreGive(route_mutex);
            return ESP_ERR_NO_MEM;
        }

        ctx->src_queue              = q;
        route_rt[slot].fwd_src_queue = q;

        char name[16];
        snprintf(name, sizeof(name), "fwd_%d_ab", route_id);
        BaseType_t ret = xTaskCreate(forward_task, name, FORWARD_STACK_SIZE,
                                     ctx, 5, &r->task_handles[0]);
        if (ret != pdPASS) {
            route_rt[slot].fwd_src_queue = NULL;
            xSemaphoreGive(route_mutex);
            src_unsubscribe(src, q);
            free(ctx);
            vSemaphoreDelete(route_rt[slot].done_sem);
            route_rt[slot].done_sem = NULL;
            return ESP_ERR_NO_MEM;
        }
        r->task_count++;
    }

    // For bridge: reverse direction (dst[0] -> src)
    if (r->type == ROUTE_TYPE_BRIDGE && r->dst_count > 0) {
        port_t *dst0 = port_registry_get(r->dst_port_ids[0]);
        if (dst0) {
            forward_ctx_t *ctx = calloc(1, sizeof(forward_ctx_t));
            if (!ctx) goto rollback_fwd;

            ctx->src       = dst0;
            ctx->dst[0]    = src;
            ctx->dst_count = 1;
            ctx->running       = &r->active;
            ctx->bytes_counter = &r->bytes_fwd_dst_to_src;
            ctx->done_sem      = route_rt[slot].done_sem;

            xSemaphoreGive(route_mutex);
            QueueHandle_t q = src_subscribe(dst0);
            xSemaphoreTake(route_mutex, portMAX_DELAY);
            if (!q) { free(ctx); goto rollback_fwd; }

            ctx->src_queue              = q;
            route_rt[slot].rev_src_queue = q;

            char name[16];
            snprintf(name, sizeof(name), "fwd_%d_ba", route_id);
            BaseType_t ret = xTaskCreate(forward_task, name, FORWARD_STACK_SIZE,
                                         ctx, 5, &r->task_handles[1]);
            if (ret != pdPASS) {
                route_rt[slot].rev_src_queue = NULL;
                xSemaphoreGive(route_mutex);
                src_unsubscribe(dst0, q);
                xSemaphoreTake(route_mutex, portMAX_DELAY);
                free(ctx);
                goto rollback_fwd;
            }
            r->task_count++;
        }
    }

    ESP_LOGI(TAG, "Route %d started: type=%d, %d task(s)", route_id, r->type, r->task_count);
    xSemaphoreGive(route_mutex);
    return ESP_OK;

rollback_fwd:
    // Roll back the already-running forward task.
    r->active = false;
    {
        QueueHandle_t fwd_q = route_rt[slot].fwd_src_queue;
        route_rt[slot].fwd_src_queue = NULL;
        xSemaphoreGive(route_mutex);
        xSemaphoreTake(route_rt[slot].done_sem, pdMS_TO_TICKS(1000));
        src_unsubscribe(src, fwd_q);
        xSemaphoreTake(route_mutex, portMAX_DELAY);
    }
    r->task_count = 0;
    memset(r->task_handles, 0, sizeof(r->task_handles));
    vSemaphoreDelete(route_rt[slot].done_sem);
    route_rt[slot].done_sem = NULL;
    xSemaphoreGive(route_mutex);
    return ESP_ERR_NO_MEM;
}

esp_err_t route_stop(uint8_t route_id)
{
    xSemaphoreTake(route_mutex, portMAX_DELAY);

    route_t *r = NULL;
    int slot = -1;
    for (int i = 0; i < ROUTE_MAX_COUNT; i++) {
        if (routes[i].id == route_id && routes[i].task_count > 0) {
            r = &routes[i];
            slot = i;
            break;
        }
    }
    if (!r) {
        xSemaphoreGive(route_mutex);
        return ESP_ERR_NOT_FOUND;
    }

    // Signal tasks to stop (they check r->active in their loop).
    r->active = false;

    // Collect info needed for cleanup while holding mutex.
    int tc = r->task_count;
    port_t *src  = port_registry_get(r->src_port_id);
    port_t *dst0 = (r->type == ROUTE_TYPE_BRIDGE && r->dst_count > 0)
                   ? port_registry_get(r->dst_port_ids[0]) : NULL;
    QueueHandle_t fwd_q = route_rt[slot].fwd_src_queue;
    QueueHandle_t rev_q = route_rt[slot].rev_src_queue;
    SemaphoreHandle_t done = route_rt[slot].done_sem;
    route_rt[slot].fwd_src_queue = NULL;
    route_rt[slot].rev_src_queue = NULL;

    xSemaphoreGive(route_mutex);

    // Wait for all tasks to confirm exit via done_sem.
    for (int i = 0; i < tc; i++) {
        if (xSemaphoreTake(done, pdMS_TO_TICKS(1000)) != pdTRUE) {
            ESP_LOGW(TAG, "Route %d: task %d/%d did not exit in time", route_id, i + 1, tc);
        }
    }

    // Unsubscribe queues (may stop pump tasks if last subscriber).
    src_unsubscribe(src,  fwd_q);
    src_unsubscribe(dst0, rev_q);

    // Now safe to clear task state -- slot cannot be reused until task_count = 0.
    xSemaphoreTake(route_mutex, portMAX_DELAY);
    r->task_count = 0;
    memset(r->task_handles, 0, sizeof(r->task_handles));
    xSemaphoreGive(route_mutex);

    vSemaphoreDelete(done);
    route_rt[slot].done_sem = NULL;

    ESP_LOGI(TAG, "Route %d stopped (type=%d, %d task(s))", route_id, r->type, tc);
    return ESP_OK;
}

esp_err_t route_destroy(uint8_t route_id)
{
    route_stop(route_id);

    xSemaphoreTake(route_mutex, portMAX_DELAY);
    for (int i = 0; i < ROUTE_MAX_COUNT; i++) {
        if (routes[i].id == route_id) {
            ESP_LOGI(TAG, "Route %d destroyed", route_id);
            memset(&routes[i], 0, sizeof(route_t));
            memset(&route_rt[i], 0, sizeof(route_runtime_t));
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
        if (routes[i].active || routes[i].task_count > 0) out[count++] = routes[i];
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
        if (routes[i].active && routes[i].task_count > 0) count++;
    }
    xSemaphoreGive(route_mutex);
    return count;
}

void route_reset_counters(uint8_t route_id)
{
    xSemaphoreTake(route_mutex, portMAX_DELAY);
    for (int i = 0; i < ROUTE_MAX_COUNT; i++) {
        if (routes[i].active && routes[i].id == route_id) {
            routes[i].bytes_fwd_src_to_dst = 0;
            routes[i].bytes_fwd_dst_to_src = 0;
            break;
        }
    }
    xSemaphoreGive(route_mutex);
}
