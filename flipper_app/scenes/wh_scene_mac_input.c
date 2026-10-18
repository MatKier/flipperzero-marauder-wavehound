#include "../wavehound_i.h"

static void wh_scene_mac_input_done(void* context) {
    WavehoundApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, WhEventMacEntered);
}

void wh_scene_mac_input_on_enter(void* context) {
    WavehoundApp* app = context;
    byte_input_set_header_text(app->byte_input, "Target MAC address");
    byte_input_set_result_callback(
        app->byte_input, wh_scene_mac_input_done, NULL, app, app->mac_bytes, sizeof(app->mac_bytes));
    view_dispatcher_switch_to_view(app->view_dispatcher, WhViewByteInput);
}

bool wh_scene_mac_input_on_event(void* context, SceneManagerEvent event) {
    WavehoundApp* app = context;
    if(event.type != SceneManagerEventTypeCustom || event.event != WhEventMacEntered) return false;

    const uint8_t* b = app->mac_bytes;
    memset(&app->target, 0, sizeof(WhTarget));
    snprintf(
        app->target.mac,
        sizeof(app->target.mac),
        "%02x:%02x:%02x:%02x:%02x:%02x",
        b[0],
        b[1],
        b[2],
        b[3],
        b[4],
        b[5]);
    app->target.randomized = b[0] & 0x02;
    app->channel_purpose = WhChannelForMacHunt;
    scene_manager_next_scene(app->scene_manager, WhSceneChannelSelect);
    return true;
}

void wh_scene_mac_input_on_exit(void* context) {
    WavehoundApp* app = context;
    byte_input_set_result_callback(app->byte_input, NULL, NULL, NULL, NULL, 0);
    byte_input_set_header_text(app->byte_input, "");
}
