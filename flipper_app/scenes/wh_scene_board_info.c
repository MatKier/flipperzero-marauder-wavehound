#include "../wavehound_i.h"
#include "../wh_marauder.h"

/* Marauder's `info` prints a block of "<label>: <value>" lines (firmware,
 * hardware, partition, IDF, MACs, ...) with no explicit end marker, so we just
 * collect them in app->board_info (in the UART worker) and re-render whenever
 * the text grows. */

static size_t s_shown_len; /* length of board_info last rendered */

static void wh_scene_board_info_show(WavehoundApp* app, const char* text) {
    widget_reset(app->widget);
    widget_add_text_scroll_element(app->widget, 0, 0, 128, 64, text);
}

void wh_scene_board_info_on_enter(void* context) {
    WavehoundApp* app = context;

    furi_mutex_acquire(app->results_mutex, FuriWaitForever);
    furi_string_set(app->board_info, "\e#Board info\n");
    furi_mutex_release(app->results_mutex);
    s_shown_len = 0;

    wh_scene_board_info_show(
        app, app->board_alive ? "Querying board..." : "No board.\nIs it connected?");
    view_dispatcher_switch_to_view(app->view_dispatcher, WhViewWidget);

    app->board_mode = WhBoardInfo;
    wh_marauder_info(app);
}

bool wh_scene_board_info_on_event(void* context, SceneManagerEvent event) {
    WavehoundApp* app = context;
    if(event.type == SceneManagerEventTypeTick) {
        /* Re-render only when new lines have arrived, so the scroll position is
         * not reset on every tick. */
        furi_mutex_acquire(app->results_mutex, FuriWaitForever);
        size_t len = furi_string_size(app->board_info);
        bool grew = len != s_shown_len;
        if(grew) {
            s_shown_len = len;
            wh_scene_board_info_show(app, furi_string_get_cstr(app->board_info));
        }
        furi_mutex_release(app->results_mutex);
        return grew;
    }
    return false;
}

void wh_scene_board_info_on_exit(void* context) {
    WavehoundApp* app = context;
    app->board_mode = WhBoardIdle;
    widget_reset(app->widget);
}
