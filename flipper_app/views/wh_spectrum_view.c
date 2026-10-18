#include "wh_spectrum_view.h"

#include <furi.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define CHANNELS 13
#define BAR_W 8
#define BAR_PITCH 9
#define BARS_TOP 12
#define BARS_BOTTOM 54
#define PEAK_DECAY 0.08f /* per sweep */
/* log2(frames + 1) of this value fills a bar: 2^8 frames per 120 ms dwell. */
#define FRAMES_FULL_SCALE 8.0f

typedef enum {
    MetricFrames,
    MetricRssi,
    MetricAirtime,
    MetricCount,
} Metric;

static const char* const metric_names[MetricCount] = {"pkts", "dBm", "bytes"};

typedef struct {
    uint32_t frames[CHANNELS];
    uint32_t bytes[CHANNELS];
    int8_t rssi[CHANNELS];
    float peak[CHANNELS];
    uint8_t current; /* channel of the newest sample, 0 = none yet */
    uint16_t unique;
    bool unique_valid;
    Metric metric;
} WhSpectrumModel;

struct WhSpectrumView {
    View* view;
};

static float level(const WhSpectrumModel* m, int i) {
    float v = 0;
    switch(m->metric) {
    case MetricFrames:
        v = log2f((float)m->frames[i] + 1.0f) / FRAMES_FULL_SCALE;
        break;
    case MetricRssi:
        v = m->rssi[i] ? ((float)m->rssi[i] + 100.0f) / 70.0f : 0;
        break;
    case MetricAirtime:
        /* bytes per 120 ms; ~64 KB means a saturated channel */
        v = log2f((float)m->bytes[i] + 1.0f) / 16.0f;
        break;
    default:
        break;
    }
    if(v < 0) v = 0;
    if(v > 1) v = 1;
    return v;
}

static void wh_spectrum_draw(Canvas* canvas, void* model) {
    WhSpectrumModel* m = model;
    char buf[32];
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    canvas_set_font(canvas, FontSecondary);

    if(m->unique_valid) {
        snprintf(buf, sizeof(buf), "%u devices", m->unique);
    } else {
        snprintf(buf, sizeof(buf), "sweeping...");
    }
    canvas_draw_str(canvas, 0, 8, buf);

    int busiest = -1;
    for(int i = 0; i < CHANNELS; i++) {
        if(m->frames[i] && (busiest < 0 || m->frames[i] > m->frames[busiest])) busiest = i;
    }
    if(busiest >= 0) {
        snprintf(buf, sizeof(buf), "top ch%d", busiest + 1);
        canvas_draw_str_aligned(canvas, 92, 8, AlignRight, AlignBottom, buf);
    }
    canvas_draw_str_aligned(canvas, 128, 8, AlignRight, AlignBottom, metric_names[m->metric]);

    const int x0 = (128 - CHANNELS * BAR_PITCH) / 2;
    const int full_h = BARS_BOTTOM - BARS_TOP;
    canvas_draw_line(canvas, 0, BARS_BOTTOM + 1, 127, BARS_BOTTOM + 1);

    for(int i = 0; i < CHANNELS; i++) {
        int x = x0 + i * BAR_PITCH;
        int h = (int)(level(m, i) * full_h);
        if(h > 0) canvas_draw_box(canvas, x, BARS_BOTTOM - h + 1, BAR_W, h);
        int ph = (int)(m->peak[i] * full_h);
        if(ph > 0) canvas_draw_line(canvas, x, BARS_BOTTOM - ph, x + BAR_W - 1, BARS_BOTTOM - ph);
        if(m->current == i + 1) {
            /* sweep cursor */
            canvas_draw_line(canvas, x + 2, BARS_BOTTOM + 3, x + BAR_W - 3, BARS_BOTTOM + 3);
        }
    }

    static const uint8_t labels[] = {1, 6, 11, 13};
    for(size_t i = 0; i < COUNT_OF(labels); i++) {
        int x = x0 + (labels[i] - 1) * BAR_PITCH + BAR_W / 2;
        snprintf(buf, sizeof(buf), "%u", labels[i]);
        canvas_draw_str_aligned(canvas, x, 64, AlignCenter, AlignBottom, buf);
    }
}

static bool wh_spectrum_input(InputEvent* event, void* context) {
    WhSpectrumView* spectrum = context;
    if(event->type != InputTypeShort) return false;
    if(event->key == InputKeyOk || event->key == InputKeyRight || event->key == InputKeyLeft) {
        int step = event->key == InputKeyLeft ? MetricCount - 1 : 1;
        with_view_model(
            spectrum->view,
            WhSpectrumModel * m,
            {
                m->metric = (Metric)((m->metric + step) % MetricCount);
                for(int i = 0; i < CHANNELS; i++)
                    m->peak[i] = level(m, i);
            },
            true);
        return true;
    }
    return false;
}

WhSpectrumView* wh_spectrum_view_alloc(void) {
    WhSpectrumView* spectrum = malloc(sizeof(WhSpectrumView));
    memset(spectrum, 0, sizeof(WhSpectrumView));
    spectrum->view = view_alloc();
    view_allocate_model(spectrum->view, ViewModelTypeLocking, sizeof(WhSpectrumModel));
    view_set_context(spectrum->view, spectrum);
    view_set_draw_callback(spectrum->view, wh_spectrum_draw);
    view_set_input_callback(spectrum->view, wh_spectrum_input);
    wh_spectrum_view_reset(spectrum);
    return spectrum;
}

void wh_spectrum_view_free(WhSpectrumView* spectrum) {
    view_free(spectrum->view);
    free(spectrum);
}

View* wh_spectrum_view_get_view(WhSpectrumView* spectrum) {
    return spectrum->view;
}

void wh_spectrum_view_reset(WhSpectrumView* spectrum) {
    with_view_model(
        spectrum->view,
        WhSpectrumModel * m,
        {
            Metric metric = m->metric;
            memset(m, 0, sizeof(WhSpectrumModel));
            m->metric = metric;
        },
        true);
}

void wh_spectrum_view_push_channel(
    WhSpectrumView* spectrum,
    uint8_t channel,
    uint32_t frames,
    uint32_t bytes,
    int8_t max_rssi) {
    if(channel < 1 || channel > CHANNELS) return;
    int i = channel - 1;
    with_view_model(
        spectrum->view,
        WhSpectrumModel * m,
        {
            m->frames[i] = frames;
            m->bytes[i] = bytes;
            m->rssi[i] = max_rssi;
            m->current = channel;
            float l = level(m, i);
            float decayed = m->peak[i] - PEAK_DECAY;
            m->peak[i] = l > decayed ? l : decayed;
        },
        true);
}

void wh_spectrum_view_push_unique(WhSpectrumView* spectrum, uint16_t unique) {
    with_view_model(
        spectrum->view,
        WhSpectrumModel * m,
        {
            m->unique = unique;
            m->unique_valid = true;
        },
        true);
}
