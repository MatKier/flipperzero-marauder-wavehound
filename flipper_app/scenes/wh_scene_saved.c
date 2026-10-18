#include "../wavehound_i.h"
#include "../wh_store.h"

/* Saved MACs (favorites). Short-press hunts the entry (via the channel picker,
 * with the saved channel pre-selected); "Delete saved..." switches the list
 * into a mode where a pick removes the entry instead. */

#define ITEM_DELETE 0xFFFF

static const char* const empty_text =
    "\e#No saved MACs\n"
    "Hunt a target, then press\n"
    "Right on the hunt screen\n"
    "to save it here.";

static void wh_scene_saved_callback(void* context, uint32_t index) {
    WavehoundApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

static void wh_scene_saved_show_empty(WavehoundApp* app) {
    app->saved_delete_mode = false;
    widget_reset(app->widget);
    widget_add_text_scroll_element(app->widget, 0, 0, 128, 64, empty_text);
    view_dispatcher_switch_to_view(app->view_dispatcher, WhViewWidget);
}

static void wh_scene_saved_build(WavehoundApp* app, size_t selected) {
    Submenu* submenu = app->submenu;
    submenu_reset(submenu);
    submenu_set_header(submenu, app->saved_delete_mode ? "Delete which?" : "Saved MACs");

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
    if(!app->saved_delete_mode) {
        submenu_add_item(submenu, "Delete saved...", ITEM_DELETE, wh_scene_saved_callback, app);
    }
    submenu_set_selected_item(submenu, selected);
    view_dispatcher_switch_to_view(app->view_dispatcher, WhViewSubmenu);
}

void wh_scene_saved_on_enter(void* context) {
    WavehoundApp* app = context;
    app->saved_delete_mode = false;
    if(app->saved_count == 0) {
        wh_scene_saved_show_empty(app);
        return;
    }
    wh_scene_saved_build(app, 0);
}

bool wh_scene_saved_on_event(void* context, SceneManagerEvent event) {
    WavehoundApp* app = context;

    if(event.type == SceneManagerEventTypeBack) {
        if(app->saved_delete_mode) {
            /* First Back leaves delete mode rather than the scene. */
            app->saved_delete_mode = false;
            wh_scene_saved_build(app, 0);
            return true;
        }
        return false; /* default: back to the start menu */
    }

    if(event.type != SceneManagerEventTypeCustom) return false;

    if(event.event == ITEM_DELETE) {
        app->saved_delete_mode = true;
        wh_scene_saved_build(app, 0);
        return true;
    }

    const size_t index = (size_t)event.event;
    if(index >= app->saved_count) return false;

    if(app->saved_delete_mode) {
        wh_store_delete(app, index);
        if(app->saved_count == 0) {
            wh_scene_saved_show_empty(app);
        } else {
            size_t sel = index < app->saved_count ? index : app->saved_count - 1;
            wh_scene_saved_build(app, sel);
        }
        return true;
    }

    /* Hunt the picked entry: seed the target and open the channel picker, which
     * pre-selects app->target.channel (the saved channel). */
    const WhSavedMac* s = &app->saved[index];
    memset(&app->target, 0, sizeof(WhTarget));
    strlcpy(app->target.mac, s->mac, sizeof(app->target.mac));
    strlcpy(app->target.name, s->name, sizeof(app->target.name));
    app->target.channel = s->channel;
    app->target.pin_channel = s->channel != 0;
    wh_parse_mac(app->target.mac, app->mac_bytes);
    app->target.randomized = app->mac_bytes[0] & 0x02;
    app->channel_purpose = WhChannelForMacHunt;
    scene_manager_next_scene(app->scene_manager, WhSceneChannelSelect);
    return true;
}

void wh_scene_saved_on_exit(void* context) {
    WavehoundApp* app = context;
    submenu_reset(app->submenu);
    widget_reset(app->widget);
}
