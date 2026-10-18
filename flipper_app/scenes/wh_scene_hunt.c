#include "../wavehound_i.h"

void wh_scene_hunt_on_enter(void* context) {
    WavehoundApp* app = context;
    const WhTarget* t = &app->target;

    wh_hunt_view_set_target(app->hunt, t->name[0] ? t->name : t->mac, t->channel);

    char cmd[48];
    snprintf(
        cmd,
        sizeof(cmd),
        "HUNT,%s,%u%s",
        t->mac,
        t->channel,
        t->pin_channel && t->channel ? ",PIN" : "");
    wh_send(app, cmd);
    view_dispatcher_switch_to_view(app->view_dispatcher, WhViewHunt);
}

bool wh_scene_hunt_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void wh_scene_hunt_on_exit(void* context) {
    WavehoundApp* app = context;
    wh_send(app, "STOP");
}
