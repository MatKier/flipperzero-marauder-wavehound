#include "../wavehound_i.h"

/* Query the board for version/identity/health and show the reply. The board
 * answers asynchronously with INFO_BEGIN / BI,... / INFO_END (parsed in
 * wavehound.c), which fires WhEventBoardInfoReady once the block is complete. */

static void wh_scene_board_info_show(WavehoundApp* app, const char* text) {
    widget_reset(app->widget);
    widget_add_text_scroll_element(app->widget, 0, 0, 128, 64, text);
}

void wh_scene_board_info_on_enter(void* context) {
    WavehoundApp* app = context;
    wh_scene_board_info_show(
        app, app->board_alive ? "Querying board..." : "No board.\nIs it connected?");
    view_dispatcher_switch_to_view(app->view_dispatcher, WhViewWidget);
    wh_send(app, "INFO");
}

bool wh_scene_board_info_on_event(void* context, SceneManagerEvent event) {
    WavehoundApp* app = context;
    if(event.type == SceneManagerEventTypeCustom && event.event == WhEventBoardInfoReady) {
        furi_mutex_acquire(app->results_mutex, FuriWaitForever);
        wh_scene_board_info_show(app, furi_string_get_cstr(app->board_info));
        furi_mutex_release(app->results_mutex);
        return true;
    }
    return false;
}

void wh_scene_board_info_on_exit(void* context) {
    WavehoundApp* app = context;
    widget_reset(app->widget);
}
