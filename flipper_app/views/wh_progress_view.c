#include "wh_progress_view.h"

#include <furi.h>
#include <gui/elements.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    char title[24];
    char error[32];
    uint32_t duration_ms;
    uint32_t start_tick;
    uint32_t now_tick;
    uint16_t found;
    bool finishing; /* listen cut short or over, waiting for the report */
} WhProgressModel;

struct WhProgressView {
    View* view;
    WhProgressFinishCallback finish_cb;
    void* finish_ctx;
};

/* Concentric arcs that grow outward, one ring per animation phase. */
static void draw_radar(Canvas* canvas, int cx, int cy, uint32_t phase) {
    canvas_draw_disc(canvas, cx, cy, 2);
    for(int r = 1; r <= 3; r++) {
        if((uint32_t)r <= phase % 4) canvas_draw_circle(canvas, cx, cy, 2 + r * 3);
    }
}

static void wh_progress_draw(Canvas* canvas, void* model) {
    WhProgressModel* m = model;
    char buf[32];
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 0, AlignCenter, AlignTop, m->title);

    const uint32_t elapsed_ms = (m->now_tick - m->start_tick) * 1000 / furi_kernel_get_tick_frequency();

    if(m->error[0]) {
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, 64, 24, AlignCenter, AlignTop, m->error);
        canvas_draw_str_aligned(canvas, 64, 36, AlignCenter, AlignTop, "Check board + firmware");
        canvas_draw_str_aligned(canvas, 64, 63, AlignCenter, AlignBottom, "Back to return");
        return;
    }

    draw_radar(canvas, 18, 34, elapsed_ms / 300);

    canvas_set_font(canvas, FontBigNumbers);
    snprintf(buf, sizeof(buf), "%u", m->found);
    canvas_draw_str_aligned(canvas, 82, 20, AlignCenter, AlignTop, buf);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 82, 36, AlignCenter, AlignTop, "found");

    const bool over = m->finishing || elapsed_ms >= m->duration_ms;
    float p = over ? 1.0f : (float)elapsed_ms / (float)m->duration_ms;
    elements_progress_bar(canvas, 0, 48, 128, p);

    if(over) {
        strlcpy(buf, "Collecting results...", sizeof(buf));
    } else {
        uint32_t left_s = (m->duration_ms - elapsed_ms + 999) / 1000;
        if(left_s >= 60) {
            snprintf(
                buf,
                sizeof(buf),
                "%lu:%02lu left  OK: results",
                (unsigned long)(left_s / 60),
                (unsigned long)(left_s % 60));
        } else {
            snprintf(buf, sizeof(buf), "%lus left  OK: results", (unsigned long)left_s);
        }
    }
    canvas_draw_str_aligned(canvas, 64, 64, AlignCenter, AlignBottom, buf);
}

static bool wh_progress_input(InputEvent* event, void* context) {
    WhProgressView* progress = context;
    if(event->type != InputTypeShort || event->key != InputKeyOk) return false;

    bool can_finish = false;
    with_view_model(
        progress->view,
        WhProgressModel * m,
        { can_finish = !m->error[0] && !m->finishing; },
        false);
    if(can_finish && progress->finish_cb) progress->finish_cb(progress->finish_ctx);
    return true;
}

WhProgressView* wh_progress_view_alloc(void) {
    WhProgressView* progress = malloc(sizeof(WhProgressView));
    progress->finish_cb = NULL;
    progress->finish_ctx = NULL;
    progress->view = view_alloc();
    view_allocate_model(progress->view, ViewModelTypeLocking, sizeof(WhProgressModel));
    view_set_context(progress->view, progress);
    view_set_draw_callback(progress->view, wh_progress_draw);
    view_set_input_callback(progress->view, wh_progress_input);
    with_view_model(
        progress->view, WhProgressModel * m, { memset(m, 0, sizeof(WhProgressModel)); }, false);
    return progress;
}

void wh_progress_view_free(WhProgressView* progress) {
    view_free(progress->view);
    free(progress);
}

View* wh_progress_view_get_view(WhProgressView* progress) {
    return progress->view;
}

void wh_progress_view_set_finish_callback(
    WhProgressView* progress,
    WhProgressFinishCallback callback,
    void* context) {
    progress->finish_cb = callback;
    progress->finish_ctx = context;
}

void wh_progress_view_start(WhProgressView* progress, const char* title, uint32_t duration_ms) {
    with_view_model(
        progress->view,
        WhProgressModel * m,
        {
            memset(m, 0, sizeof(WhProgressModel));
            strlcpy(m->title, title, sizeof(m->title));
            m->duration_ms = duration_ms ? duration_ms : 1;
            m->start_tick = furi_get_tick();
            m->now_tick = m->start_tick;
        },
        true);
}

void wh_progress_view_update(WhProgressView* progress, uint16_t found) {
    with_view_model(
        progress->view,
        WhProgressModel * m,
        {
            m->found = found;
            m->now_tick = furi_get_tick();
        },
        true);
}

void wh_progress_view_set_finishing(WhProgressView* progress) {
    with_view_model(progress->view, WhProgressModel * m, { m->finishing = true; }, true);
}

void wh_progress_view_set_error(WhProgressView* progress, const char* message) {
    with_view_model(
        progress->view,
        WhProgressModel * m,
        { strlcpy(m->error, message, sizeof(m->error)); },
        true);
}
