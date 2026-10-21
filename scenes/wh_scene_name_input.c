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
    if(app->editing) {
        /* Editing an existing entry: name_buf is updated; return to the editor. */
        scene_manager_previous_scene(app->scene_manager);
        return true;
    }
    const WhStoreResult res = wh_store_add(app, app->name_buf, app->target.mac, app->save_channel);
    if(res == WhStoreOk) {
        scene_manager_previous_scene(app->scene_manager); /* back to the hunt */
        return true;
    }
    /* Say why nothing was saved; Back returns to where we came from. */
    widget_reset(app->widget);
    widget_add_string_multiline_element(
        app->widget,
        64,
        32,
        AlignCenter,
        AlignCenter,
        FontSecondary,
        res == WhStoreFull ? "Not saved:\nthe saved list is full.\nDelete one first." :
                             "Not saved:\ncouldn't write to the\nSD card.");
    view_dispatcher_switch_to_view(app->view_dispatcher, WhViewWidget);
    return true;
}

void wh_scene_name_input_on_exit(void* context) {
    WavehoundApp* app = context;
    text_input_reset(app->text_input);
    widget_reset(app->widget);
}
