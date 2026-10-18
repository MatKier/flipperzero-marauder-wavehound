#include "led.h"

#include "sdkconfig.h"

#if CONFIG_WH_LED_ENABLE

#include "driver/ledc.h"

#define LED_TIMER LEDC_TIMER_0
#define LED_MODE LEDC_LOW_SPEED_MODE

static const int s_led_gpio[3] = {CONFIG_WH_LED_R_GPIO, CONFIG_WH_LED_G_GPIO, CONFIG_WH_LED_B_GPIO};

void led_init(void)
{
    const ledc_timer_config_t timer = {
        .speed_mode = LED_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num = LED_TIMER,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));

    for (int i = 0; i < 3; i++) {
        const ledc_channel_config_t ch = {
            .gpio_num = s_led_gpio[i],
            .speed_mode = LED_MODE,
            .channel = (ledc_channel_t)i,
            .intr_type = LEDC_INTR_DISABLE,
            .timer_sel = LED_TIMER,
            .duty = 0,
            .hpoint = 0,
#if CONFIG_WH_LED_ACTIVE_LOW
            .flags.output_invert = 1,
#endif
        };
        ESP_ERROR_CHECK(ledc_channel_config(&ch));
    }
}

void led_set(uint8_t r, uint8_t g, uint8_t b)
{
    const uint8_t v[3] = {r, g, b};
    for (int i = 0; i < 3; i++) {
        ledc_set_duty(LED_MODE, (ledc_channel_t)i, v[i]);
        ledc_update_duty(LED_MODE, (ledc_channel_t)i);
    }
}

#else

void led_init(void) {}
void led_set(uint8_t r, uint8_t g, uint8_t b)
{
    (void)r;
    (void)g;
    (void)b;
}

#endif

void led_show_heat(int rssi)
{
    if (rssi == 0) {
        led_set(0, 0, 0);
        return;
    }
    /* -90 dBm -> 0 (cold), -35 dBm -> 255 (hot) */
    int heat = (rssi + 90) * 255 / 55;
    if (heat < 0) {
        heat = 0;
    } else if (heat > 255) {
        heat = 255;
    }
    led_set((uint8_t)heat, (uint8_t)(heat > 128 ? (255 - heat) / 4 : heat / 4), (uint8_t)(255 - heat));
}
