#pragma once

#include "route.h"

// Start the signal routing task (polls signal states and applies mappings)
esp_err_t signal_router_init(void);

// Stop the signal routing task
void signal_router_stop(void);
