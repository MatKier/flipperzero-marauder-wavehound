#include "../wavehound_i.h"

/* Saved MACs (favorites). Each entry opens a Hunt / Edit / Delete menu
 * (WhSceneSavedEntry). */

static const char* const empty_text =
    "\e#No saved MACs\n"
    "Hunt a target, then press\n"
    "Right on the hunt screen\n"
    "to save it here.";

static void wh_scene_saved_callback(void* context, uint32_t index) {
    WavehoundApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void wh_scene_saved_on_enter(void* context) {
    WavehoundApp* app = context;
    if(app->saved_count == 0) {
        widget_reset(app->widget);
        widget_add_text_scroll_element(app->widget, 0, 0, 128, 64, empty_text);
        view_dispatcher_switch_to_view(app->view_dispatcher, WhViewWidget);
        return;
    }

    Submenu* submenu = app->submenu;
    submenu_reset(submenu);
    submenu_set_header(submenu, "Saved MACs");
    char label[40];
    for(size_t i = 0; i < app->saved_count; i++) {
        const WhSavedMac* s = &app->saved[i];
        if(s->channel) {
            snprintf(label, sizeof(label), "%s (ch%u)", s->name, s->channel);
        } else {
            snprintf(label, sizeof(label), "%s", s->name);
        }
        submenu_add_item(submenu, label, (uint32_t)i, wh_scene_saved_callback, app);
    }
    /* Keep the previously chosen row selected when coming back from an entry. */
    size_t selected = app->edit_index < app->saved_count ? app->edit_index : 0;
    submenu_set_selected_item(submenu, selected);
    view_dispatcher_switch_to_view(app->view_dispatcher, WhViewSubmenu);
}

bool wh_scene_saved_on_event(void* context, SceneManagerEvent event) {
    WavehoundApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;

    const size_t index = (size_t)event.event;
    if(index >= app->saved_count) return false;

    /* Open the per-entry Hunt / Edit / Delete menu. */
    app->edit_index = index;
    scene_manager_next_scene(app->scene_manager, WhSceneSavedEntry);
    return true;
}

void wh_scene_saved_on_exit(void* context) {
    WavehoundApp* app = context;
    submenu_reset(app->submenu);
    widget_reset(app->widget);
}
