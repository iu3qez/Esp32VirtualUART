#pragma once

#include "port.h"
#include "esp_err.h"

#define ROUTE_MAX_COUNT     16
#define ROUTE_MAX_DEST      4   // Max destinations per route

typedef enum {
    ROUTE_TYPE_BRIDGE = 0,  // Bidirectional 1:1
    ROUTE_TYPE_CLONE,       // Unidirectional 1:N (source -> all destinations)
    ROUTE_TYPE_MERGE,       // Unidirectional N:1 (all sources -> single destination)
} route_type_t;

typedef struct {
    uint8_t from_signal;    // Source signal bit (SIGNAL_DTR, etc.)
    uint8_t to_signal;      // Destination signal bit
} signal_mapping_t;

typedef struct {
    uint8_t             id;
    route_type_t        type;
    bool                active;
    uint8_t             src_port_id;
    uint8_t             dst_port_ids[ROUTE_MAX_DEST];
    uint8_t             dst_count;
    signal_mapping_t    signal_map[8];
    uint8_t             signal_map_count;

    // Runtime state (not persisted)
    TaskHandle_t        task_handles[2];    // Up to 2 tasks (bridge needs 2 directions)
    uint8_t             task_count;
    volatile uint32_t   bytes_fwd_src_to_dst;
    volatile uint32_t   bytes_fwd_dst_to_src;
} route_t;

// Initialize the routing engine
esp_err_t route_engine_init(void);

// Create a route (does not start it). Returns assigned route ID via route_id_out.
esp_err_t route_create(const route_t *config, uint8_t *route_id_out);

// Destroy a route (stops it first if running)
esp_err_t route_destroy(uint8_t route_id);

// Start data forwarding for a route
esp_err_t route_start(uint8_t route_id);

// Stop data forwarding for a route
esp_err_t route_stop(uint8_t route_id);

// Get all routes. Copies up to max_count routes into the array. Returns actual count.
int route_get_all(route_t *routes, int max_count);

// Get a single route by ID. Returns NULL if not found.
route_t *route_get(uint8_t route_id);

// Get count of active routes
int route_active_count(void);

// Reset byte counters for monitoring
void route_reset_counters(uint8_t route_id);
