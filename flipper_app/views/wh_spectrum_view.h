#pragma once

#include <gui/view.h>
#include <stdint.h>

typedef struct WhSpectrumView WhSpectrumView;

WhSpectrumView* wh_spectrum_view_alloc(void);
void wh_spectrum_view_free(WhSpectrumView* spectrum);
View* wh_spectrum_view_get_view(WhSpectrumView* spectrum);

/** Clear all bars (call before starting a new sweep session). */
void wh_spectrum_view_reset(WhSpectrumView* spectrum);

/** Feed one `S` line. Safe to call from any thread. */
void wh_spectrum_view_push_channel(
    WhSpectrumView* spectrum,
    uint8_t channel,
    uint32_t frames,
    uint32_t bytes,
    int8_t max_rssi);

/** Feed one `U` line. */
void wh_spectrum_view_push_unique(WhSpectrumView* spectrum, uint16_t unique);
