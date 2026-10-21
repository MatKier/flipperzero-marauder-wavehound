#include "../wavehound_i.h"
#include "../wh_marauder.h"
#include "../wh_oui.h"

/* Shown after picking a scan result, before hunting: identity + vendor (from
 * the OUI), with Hunt and Save actions. app->target and app->mac_bytes were set
 * by the target list. For an access point we also ask Marauder `info -a <idx>`
 * for its security and redraw when the reply arrives (scanall alone doesn't
 * report it). */

static void wh_scene_details_button_cb(GuiButtonType result, InputType type, void* context) {
    WavehoundApp* app = context;
    if(type != InputTypeShort) return;
    if(result == GuiButtonTypeLeft) {
        view_dispatcher_send_custom_event(app->view_dispatcher, WhEventDetailsSave);
    } else if(result == GuiButtonTypeRight) {
        view_dispatcher_send_custom_event(app->view_dispatcher, WhEventDetailsHunt);
    }
}

static void wh_scene_details_render(WavehoundApp* app) {
    const WhTarget* t = &app->target;
    Widget* w = app->widget;
    widget_reset(w);

    const char* title = t->name[0] ? t->name : (t->is_ap ? "(hidden AP)" : "(device)");
    widget_add_string_element(w, 0, 11, AlignLeft, AlignBottom, FontPrimary, title);
    widget_add_string_element(w, 0, 23, AlignLeft, AlignBottom, FontSecondary, t->mac);

    char vendor[40];
    wh_oui_lookup(app->mac_bytes, vendor, sizeof(vendor));
    char line[56];
    snprintf(line, sizeof(line), "Vendor: %s", vendor);
    widget_add_string_element(w, 0, 35, AlignLeft, AlignBottom, FontSecondary, line);

    /* "<AP|Station>[ chN][ <security>] <rssi>dBm". Security is filled in for an
     * access point once Marauder's info -a reply arrives. */
    size_t n = snprintf(line, sizeof(line), "%s", t->is_ap ? "AP" : "Station");
    if(t->channel) n += snprintf(line + n, sizeof(line) - n, " ch%u", t->channel);
    if(t->security[0]) n += snprintf(line + n, sizeof(line) - n, " %s", t->security);
    snprintf(line + n, sizeof(line) - n, " %ddBm", t->rssi);
    widget_add_string_element(w, 0, 47, AlignLeft, AlignBottom, FontSecondary, line);

    widget_add_button_element(w, GuiButtonTypeLeft, "Save", wh_scene_details_button_cb, app);
    widget_add_button_element(w, GuiButtonTypeRight, "Hunt", wh_scene_details_button_cb, app);
}

void wh_scene_details_on_enter(void* context) {
    WavehoundApp* app = context;
    wh_scene_details_render(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, WhViewWidget);

    /* Ask the board for this AP's security; the reply (WhEventApInfoReady)
     * redraws the line. Only APs are in Marauder's list by index. */
    if(app->target.is_ap && app->target.mo_index >= 0) {
        app->board_mode = WhBoardApInfo;
        wh_marauder_info_ap(app, app->target.mo_index);
    }
}

bool wh_scene_details_on_event(void* context, SceneManagerEvent event) {
    WavehoundApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;

    if(event.event == WhEventApInfoReady) {
        wh_scene_details_render(app); /* security just arrived: redraw */
        return true;
    }
    if(event.event == WhEventDetailsHunt) {
        scene_manager_next_scene(app->scene_manager, WhSceneHunt);
        return true;
    }
    if(event.event == WhEventDetailsSave) {
        /* Reuse the name-input scene's add path (editing == false). */
        const WhTarget* t = &app->target;
        app->save_channel = t->channel;
        strlcpy(app->name_buf, t->name[0] ? t->name : t->mac, sizeof(app->name_buf));
        app->editing = false;
        scene_manager_next_scene(app->scene_manager, WhSceneNameInput);
        return true;
    }
    return false;
}

void wh_scene_details_on_exit(void* context) {
    WavehoundApp* app = context;
    app->board_mode = WhBoardIdle;
    widget_reset(app->widget);
}
