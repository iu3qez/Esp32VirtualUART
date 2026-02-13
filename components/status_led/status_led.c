#include "status_led.h"
#include "led_strip.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>

static const char *TAG = "status_led";

static led_strip_handle_t led_strip;
static led_state_t current_state = LED_STATE_BOOTING;
static volatile bool activity_flag = false;
static TaskHandle_t led_task_handle = NULL;

// Color definitions (R, G, B)
typedef struct { uint8_t r, g, b; } rgb_t;

static const rgb_t COLOR_BLUE   = {0, 0, 40};
static const rgb_t COLOR_GREEN  = {0, 40, 0};
static const rgb_t COLOR_RED    = {40, 0, 0};
static const rgb_t COLOR_ORANGE = {40, 20, 0};
static const rgb_t COLOR_CYAN   = {0, 30, 30};
static const rgb_t COLOR_PURPLE = {20, 0, 30};
static const rgb_t COLOR_WHITE  = {30, 30, 30};


static void led_set_color(const rgb_t *color)
{
    led_strip_set_pixel(led_strip, 0, color->r, color->g, color->b);
    led_strip_refresh(led_strip);
}

static void led_off(void)
{
    led_strip_clear(led_strip);
    led_strip_refresh(led_strip);
}

// Pulse effect: smoothly ramp brightness up and down
static void led_pulse(const rgb_t *color, int period_ms, int step_ms)
{
    int steps = period_ms / step_ms;
    for (int i = 0; i < steps; i++) {
        float t = (float)i / (float)steps;
        float brightness = (sinf(t * 2.0f * M_PI - M_PI / 2.0f) + 1.0f) / 2.0f;
        rgb_t c = {
            .r = (uint8_t)(color->r * brightness),
            .g = (uint8_t)(color->g * brightness),
            .b = (uint8_t)(color->b * brightness),
        };
        led_set_color(&c);
        vTaskDelay(pdMS_TO_TICKS(step_ms));

        // Check for activity interrupt or state change
        if (activity_flag) {
            activity_flag = false;
            led_set_color(&COLOR_WHITE);
            vTaskDelay(pdMS_TO_TICKS(30));
            led_off();
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }
}

// Blink effect: on/off at given rate
static void led_blink(const rgb_t *color, int on_ms, int off_ms, int count)
{
    for (int i = 0; i < count; i++) {
        led_set_color(color);
        vTaskDelay(pdMS_TO_TICKS(on_ms));
        led_off();
        vTaskDelay(pdMS_TO_TICKS(off_ms));
    }
}

static void led_animation_task(void *arg)
{
    while (1) {
        switch (current_state) {
        case LED_STATE_BOOTING:
            led_set_color(&COLOR_BLUE);
            vTaskDelay(pdMS_TO_TICKS(500));
            break;

        case LED_STATE_NO_USB:
            led_pulse(&COLOR_PURPLE, 2000, 20);
            break;

        case LED_STATE_READY:
            led_pulse(&COLOR_GREEN, 2000, 20);
            break;

        case LED_STATE_IDLE:
            led_set_color(&COLOR_GREEN);
            vTaskDelay(pdMS_TO_TICKS(200));
            if (activity_flag) {
                activity_flag = false;
                led_set_color(&COLOR_WHITE);
                vTaskDelay(pdMS_TO_TICKS(30));
            }
            break;

        case LED_STATE_DATA_FLOW:
            led_blink(&COLOR_GREEN, 50, 50, 5);
            break;

        case LED_STATE_WIFI_CONNECTING:
            led_pulse(&COLOR_ORANGE, 1500, 20);
            break;

        case LED_STATE_WIFI_READY:
            led_set_color(&COLOR_CYAN);
            vTaskDelay(pdMS_TO_TICKS(200));
            break;

        case LED_STATE_DATA_FLOW_NET:
            led_blink(&COLOR_WHITE, 50, 50, 5);
            break;

        case LED_STATE_ERROR:
            led_set_color(&COLOR_RED);
            vTaskDelay(pdMS_TO_TICKS(500));
            break;
        }
    }
}

esp_err_t status_led_init(gpio_num_t gpio)
{
    ESP_LOGI(TAG, "Initializing RGB LED on GPIO%d", gpio);

    led_strip_config_t strip_config = {
        .strip_gpio_num = gpio,
        .max_leds = 1,
        .led_model = LED_MODEL_WS2812,
        .flags.invert_out = false,
    };

    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000, // 10 MHz
        .flags.with_dma = false,
    };

    esp_err_t ret = led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LED strip init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    led_strip_clear(led_strip);
    led_set_color(&COLOR_BLUE);  // Show blue immediately during boot

    // Start animation task at low priority
    BaseType_t task_ret = xTaskCreate(led_animation_task, "status_led", 2048, NULL, 2, &led_task_handle);
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create LED task");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Status LED initialized");
    return ESP_OK;
}

void status_led_set_state(led_state_t state)
{
    if (state != current_state) {
        ESP_LOGD(TAG, "LED state: %d -> %d", current_state, state);
        current_state = state;
    }
}

void status_led_set_activity(void)
{
    activity_flag = true;
}

led_state_t status_led_get_state(void)
{
    return current_state;
}
