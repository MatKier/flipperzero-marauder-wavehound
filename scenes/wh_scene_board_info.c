#include "../wavehound_i.h"
#include "../wh_marauder.h"

/* Marauder's `info` prints a block of "<label>: <value>" lines (firmware,
 * hardware, partition, IDF, MACs, ...) with no explicit end marker, so we just
 * collect them in app->board_info (in the UART worker) and re-render whenever
 * the text grows. */

static size_t s_shown_len; /* length of board_info last rendered */
static uint16_t s_ticks; /* ticks since the query was sent */
static bool s_no_board; /* "No board" is on screen */
static bool s_replied; /* a reply line has arrived */

/* Wait this long for a first reply before saying the board is missing. */
#define WH_INFO_TIMEOUT_TICKS (3000 / WH_TICK_MS)

static void wh_scene_board_info_show(WavehoundApp* app, const char* text) {
    widget_reset(app->widget);
    widget_add_text_scroll_element(app->widget, 0, 0, 128, 64, text);
}

void wh_scene_board_info_on_enter(void* context) {
    WavehoundApp* app = context;

    furi_mutex_acquire(app->results_mutex, FuriWaitForever);
    furi_string_set(app->board_info, "\e#Board info\n");
    /* Count the title as shown: "grew" then means a reply line arrived. */
    s_shown_len = furi_string_size(app->board_info);
    furi_mutex_release(app->results_mutex);
    s_ticks = 0;
    s_replied = false;
    s_no_board = !app->uart;

    wh_scene_board_info_show(app, app->uart ? "Querying board..." : "USART busy.");
    view_dispatcher_switch_to_view(app->view_dispatcher, WhViewWidget);

    wh_set_board_mode(app, WhBoardInfo);
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
            s_replied = true;
            s_no_board = false;
            wh_scene_board_info_show(app, furi_string_get_cstr(app->board_info));
        }
        furi_mutex_release(app->results_mutex);
        /* Nothing back after a while and the link is silent: say so (and take
         * it back if the board turns up after all). */
        if(s_ticks < UINT16_MAX) s_ticks++;
        if(!s_no_board && !s_replied && s_ticks >= WH_INFO_TIMEOUT_TICKS &&
           !app->board_alive) {
            s_no_board = true;
            wh_scene_board_info_show(app, "No board.\nIs it connected?");
        }
        return grew;
    }
    return false;
}

void wh_scene_board_info_on_exit(void* context) {
    WavehoundApp* app = context;
    wh_set_board_mode(app, WhBoardIdle);
    widget_reset(app->widget);
}
