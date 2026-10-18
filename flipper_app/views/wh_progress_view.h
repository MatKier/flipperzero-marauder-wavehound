#pragma once

#include <gui/view.h>
#include <stdbool.h>
#include <stdint.h>

/** Timed search screen: progress bar, found counter, early finish on OK, error state. */
typedef struct WhProgressView WhProgressView;

/** Called (GUI thread) when the user presses OK to end the search early. */
typedef void (*WhProgressFinishCallback)(void* context);

WhProgressView* wh_progress_view_alloc(void);
void wh_progress_view_free(WhProgressView* progress);
View* wh_progress_view_get_view(WhProgressView* progress);

void wh_progress_view_set_finish_callback(
    WhProgressView* progress,
    WhProgressFinishCallback callback,
    void* context);

/** Start a new run of `duration_ms`. */
void wh_progress_view_start(WhProgressView* progress, const char* title, uint32_t duration_ms);

/** Refresh the bar and found count (call from the scene tick). */
void wh_progress_view_update(WhProgressView* progress, uint16_t found);

/** Show "Collecting results..." while waiting for the board's report. */
void wh_progress_view_set_finishing(WhProgressView* progress);

/** Switch to an error message, e.g. when the board does not answer. */
void wh_progress_view_set_error(WhProgressView* progress, const char* message);
