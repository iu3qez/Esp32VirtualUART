#pragma once

#include "esp_err.h"
#include "driver/gpio.h"

typedef enum {
    LED_STATE_BOOTING = 0,  // Solid blue
    LED_STATE_NO_USB,       // Slow pulse purple
    LED_STATE_READY,        // Slow pulse green - USB connected, no routes
    LED_STATE_IDLE,         // Solid green - routes active, no data flowing
    LED_STATE_DATA_FLOW,    // Fast blink green - data flowing
    LED_STATE_WIFI_CONNECTING, // Slow pulse orange
    LED_STATE_WIFI_READY,   // Solid cyan
    LED_STATE_DATA_FLOW_NET,// Fast blink white - USB + TCP active
    LED_STATE_ERROR,        // Solid red
} led_state_t;

// Initialize the RGB LED on the given GPIO pin.
// Common pins: GPIO48 (DevKitC), GPIO38 (some boards)
esp_err_t status_led_init(gpio_num_t gpio);

// Set the LED state (affects color and animation pattern)
void status_led_set_state(led_state_t state);

// Signal a data activity burst (triggers a brief blink on top of current state)
void status_led_set_activity(void);

// Get current LED state
led_state_t status_led_get_state(void);
