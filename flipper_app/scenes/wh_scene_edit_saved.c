#include "../wavehound_i.h"
#include "../wh_store.h"

/* Edit one saved MAC. The working copy lives in app->name_buf (name),
 * app->mac_bytes (MAC) and app->edit_channel (channel); Name/MAC reuse the
 * existing input scenes (with app->editing set), and the channel is picked from
 * an inline list. Save writes back to saved[edit_index]; Delete removes it. */

#define EDIT_NAME 200
#define EDIT_MAC 201
#define EDIT_CH 202
#define EDIT_SAVE 203

static void wh_scene_edit_saved_callback(void* context, uint32_t index) {
    WavehoundApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

static void mac_to_str(const uint8_t* b, char* out, size_t n) {
    snprintf(out, n, "%02x:%02x:%02x:%02x:%02x:%02x", b[0], b[1], b[2], b[3], b[4], b[5]);
}

static void wh_scene_edit_saved_build_fields(WavehoundApp* app) {
    Submenu* submenu = app->submenu;
    submenu_reset(submenu);
    submenu_set_header(submenu, "Edit saved MAC");

    char line[48];
    char mac[18];
    snprintf(line, sizeof(line), "Name: %s", app->name_buf);
    submenu_add_item(submenu, line, EDIT_NAME, wh_scene_edit_saved_callback, app);
    mac_to_str(app->mac_bytes, mac, sizeof(mac));
    snprintf(line, sizeof(line), "MAC: %s", mac);
    submenu_add_item(submenu, line, EDIT_MAC, wh_scene_edit_saved_callback, app);
    if(app->edit_channel) {
        snprintf(line, sizeof(line), "Channel: %u", app->edit_channel);
    } else {
        snprintf(line, sizeof(line), "Channel: Auto");
    }
    submenu_add_item(submenu, line, EDIT_CH, wh_scene_edit_saved_callback, app);
    submenu_add_item(submenu, "Save changes", EDIT_SAVE, wh_scene_edit_saved_callback, app);
    view_dispatcher_switch_to_view(app->view_dispatcher, WhViewSubmenu);
}

static void wh_scene_edit_saved_build_channel(WavehoundApp* app) {
    Submenu* submenu = app->submenu;
    submenu_reset(submenu);
    submenu_set_header(submenu, "Channel");
    submenu_add_item(submenu, "Auto (hop)", 0, wh_scene_edit_saved_callback, app);
    char label[16];
    for(uint32_t ch = 1; ch <= WH_MAX_CHANNEL; ch++) {
        snprintf(label, sizeof(label), "Channel %lu", (unsigned long)ch);
        submenu_add_item(submenu, label, ch, wh_scene_edit_saved_callback, app);
    }
    submenu_set_selected_item(submenu, app->edit_channel);
    view_dispatcher_switch_to_view(app->view_dispatcher, WhViewSubmenu);
}

void wh_scene_edit_saved_on_enter(void* context) {
    WavehoundApp* app = context;
    app->edit_channel_mode = false;
    wh_scene_edit_saved_build_fields(app);
}

bool wh_scene_edit_saved_on_event(void* context, SceneManagerEvent event) {
    WavehoundApp* app = context;

    if(event.type == SceneManagerEventTypeBack) {
        if(app->edit_channel_mode) {
            app->edit_channel_mode = false;
            wh_scene_edit_saved_build_fields(app);
            return true;
        }
        app->editing = false; /* discard: leave without saving */
        return false;
    }

    if(event.type != SceneManagerEventTypeCustom) return false;

    if(app->edit_channel_mode) {
        if(event.event > WH_MAX_CHANNEL) return false;
        app->edit_channel = (uint8_t)event.event;
        app->edit_channel_mode = false;
        wh_scene_edit_saved_build_fields(app);
        return true;
    }

    switch(event.event) {
    case EDIT_NAME:
        app->editing = true;
        scene_manager_next_scene(app->scene_manager, WhSceneNameInput);
        return true;
    case EDIT_MAC:
        app->editing = true;
        scene_manager_next_scene(app->scene_manager, WhSceneMacInput);
        return true;
    case EDIT_CH:
        app->edit_channel_mode = true;
        wh_scene_edit_saved_build_channel(app);
        return true;
    case EDIT_SAVE: {
        char mac[18];
        mac_to_str(app->mac_bytes, mac, sizeof(mac));
        wh_store_update(app, app->edit_index, app->name_buf, mac, app->edit_channel);
        app->editing = false;
        scene_manager_search_and_switch_to_previous_scene(app->scene_manager, WhSceneSaved);
        return true;
    }
    default:
        return false;
    }
}

void wh_scene_edit_saved_on_exit(void* context) {
    WavehoundApp* app = context;
    submenu_reset(app->submenu);
}
