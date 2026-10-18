#pragma once

#include <stdint.h>

/** Set up PWM for the on-board RGB LED (no-op when disabled in menuconfig). */
void led_init(void);

/** Set LED color, 0..255 per channel. */
void led_set(uint8_t r, uint8_t g, uint8_t b);

/** Map an RSSI (dBm) to a cold-blue .. hot-red color. rssi == 0 means "nothing heard". */
void led_show_heat(int rssi);
