#include "../wavehound_i.h"
#include "../wh_store.h"

/* Name the current hunt target and store it as a favorite. Reached from the
 * hunt screen (Right key); name_buf is pre-filled with the SSID or MAC. */

static void wh_scene_name_input_done(void* context) {
    WavehoundApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, WhEventNameEntered);
}

void wh_scene_name_input_on_enter(void* context) {
    WavehoundApp* app = context;
    text_input_reset(app->text_input);
    text_input_set_header_text(app->text_input, "Save MAC as");
    text_input_set_minimum_length(app->text_input, 1);
    text_input_set_result_callback(
        app->text_input,
        wh_scene_name_input_done,
        app,
        app->name_buf,
        sizeof(app->name_buf),
        false); /* keep the pre-filled default */
    view_dispatcher_switch_to_view(app->view_dispatcher, WhViewTextInput);
}

bool wh_scene_name_input_on_event(void* context, SceneManagerEvent event) {
    WavehoundApp* app = context;
    if(event.type != SceneManagerEventTypeCustom || event.event != WhEventNameEntered) {
        return false;
    }
    wh_store_add(app, app->name_buf, app->target.mac, app->save_channel);
    scene_manager_previous_scene(app->scene_manager); /* back to the hunt */
    return true;
}

void wh_scene_name_input_on_exit(void* context) {
    WavehoundApp* app = context;
    text_input_reset(app->text_input);
}
