#include "../wavehound_i.h"
#include "../wh_marauder.h"

/* Marauder streams one "<name> RSSI: <dBm>" line per frame heard, at no fixed
 * rate. A 100 ms timer drains the accumulator (filled by the UART worker) into
 * the hunt view as one sample, so the meter/graph/geiger keep their cadence. */
#define WH_FEED_PERIOD_MS 100

static FuriTimer* s_feed_timer;
/* A typed/saved MAC is not in Marauder's list, so it is added with `add -a`;
 * once the board echoes "Added AP [n]" (app->pending_ap_index) we start the
 * hunt by that index. */
static bool s_awaiting_add;

static void wh_scene_hunt_feed_cb(void* context) {
    WavehoundApp* app = context;

    if(s_awaiting_add && app->pending_ap_index >= 0) {
        s_awaiting_add = false;
        wh_marauder_foxhunt_ap(app, app->pending_ap_index);
    }
    wh_hunt_feed_flush(app);
}

/* Seed the fox hunt's starting channel when we know it (an AP/station from a
 * scan, or a pinned manual channel), so it locks on immediately instead of
 * hopping to find the target. Marauder's fox hunt owns the channel from there:
 * it stays on the target's channel while fresh and hops after ~1.5 s of
 * silence, so a true "pin" (stay put when quiet) is not possible -- this is the
 * best-effort channel behavior. */
static void wh_scene_hunt_apply_channel(WavehoundApp* app, uint8_t channel) {
    if(channel) {
        wh_marauder_set_channel(app, channel);
        app->hunt_channel = channel;
    } else {
        app->hunt_channel = 0;
    }
}

void wh_scene_hunt_on_enter(void* context) {
    WavehoundApp* app = context;
    const WhTarget* t = &app->target;

    wh_hunt_view_set_target(app->hunt, t->name[0] ? t->name : t->mac, t->channel);

    if(app->settings.keep_backlight) {
        notification_message(app->notifications, &sequence_display_backlight_enforce_on);
    }

    /* Reset the RSSI accumulator before samples start arriving. */
    furi_mutex_acquire(app->results_mutex, FuriWaitForever);
    app->hunt_frames = 0;
    app->hunt_sum = 0;
    app->hunt_max = 0;
    furi_mutex_release(app->results_mutex);

    s_awaiting_add = false;
    app->board_mode = WhBoardHunting;

    const bool is_station = !t->is_ap && t->ap_index >= 0 && t->sta_index >= 0;
    wh_scene_hunt_apply_channel(app, t->channel);

    if(is_station) {
        wh_marauder_foxhunt_sta(app, t->ap_index, t->sta_index);
    } else if(t->is_ap && t->mo_index >= 0) {
        wh_marauder_foxhunt_ap(app, t->mo_index);
    } else {
        /* Typed or saved MAC: add it to Marauder's AP list, then hunt by the
         * index it reports back (handled in the feed timer). */
        app->pending_ap_index = -1;
        s_awaiting_add = true;
        wh_marauder_add_ap(app, t->mac, t->channel, t->name);
    }

    s_feed_timer = furi_timer_alloc(wh_scene_hunt_feed_cb, FuriTimerTypePeriodic, app);
    furi_timer_start(s_feed_timer, furi_ms_to_ticks(WH_FEED_PERIOD_MS));

    view_dispatcher_switch_to_view(app->view_dispatcher, WhViewHunt);
}

bool wh_scene_hunt_on_event(void* context, SceneManagerEvent event) {
    WavehoundApp* app = context;
    if(event.type == SceneManagerEventTypeCustom && event.event == WhEventSaveTarget) {
        /* Remember the channel we are actually locked on (fall back to the
         * configured one, which may be 0 = auto), then name and save it. */
        app->save_channel = wh_hunt_view_current_channel(app->hunt);
        if(app->save_channel == 0) app->save_channel = app->target.channel;
        const WhTarget* t = &app->target;
        strlcpy(app->name_buf, t->name[0] ? t->name : t->mac, sizeof(app->name_buf));
        app->editing = false; /* adding a new favorite, not editing one */
        scene_manager_next_scene(app->scene_manager, WhSceneNameInput);
        return true;
    }
    return false;
}

void wh_scene_hunt_on_exit(void* context) {
    WavehoundApp* app = context;
    if(s_feed_timer) {
        furi_timer_stop(s_feed_timer);
        furi_timer_free(s_feed_timer);
        s_feed_timer = NULL;
    }
    wh_marauder_stop(app);
    app->board_mode = WhBoardIdle;
    /* Always release the enforce, regardless of the current setting: it is a
     * global display state, so toggling "Keep backlight" off between enter and
     * exit must not leave it stuck on. Harmless if it was never enforced. */
    notification_message(app->notifications, &sequence_display_backlight_enforce_auto);
}
