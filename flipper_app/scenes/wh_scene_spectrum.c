#include "../wavehound_i.h"

void wh_scene_spectrum_on_enter(void* context) {
    WavehoundApp* app = context;
    wh_spectrum_view_reset(app->spectrum);
    wh_send(app, "SPECTRUM");
    view_dispatcher_switch_to_view(app->view_dispatcher, WhViewSpectrum);
}

bool wh_scene_spectrum_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void wh_scene_spectrum_on_exit(void* context) {
    WavehoundApp* app = context;
    wh_send(app, "STOP");
}
