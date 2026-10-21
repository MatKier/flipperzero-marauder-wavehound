#include "../wavehound_i.h"
#include "../wh_marauder.h"

/* Marauder streams one "<name> RSSI: <dBm>" line per frame heard, at no fixed
 * rate. A 100 ms timer drains the accumulator (filled by the UART worker) into
 * the hunt view as one sample, so the meter/graph/geiger keep their cadence. */
#define WH_FEED_PERIOD_MS 100
/* Each handshake step waits this many timer ticks for the board, then retries. */
#define WH_HUNT_STEP_TICKS 15
#define WH_HUNT_TRIES 3

/* Starting a hunt is a small handshake driven from the feed timer, because the
 * board's CLI has no other acknowledgements:
 *  1. `add -a` the target's MAC. Marauder dedups by MAC, so this returns the
 *     entry's current list index whether it is a scanned AP, a station or a
 *     typed/saved MAC. (Indices are never cached: every scanall rebuilds the
 *     board's AP list.)
 *  2. `foxhunt -w <index>` until the board prints "Starting...Fox Hunt", the
 *     only sign it actually started (it is silently ignored if the board is
 *     still scanning or the index is out of range).
 *  3. Park the radio: starting the hunt tunes to the list entry's channel, so
 *     the user's channel is applied after it. The board's reply to that (or to
 *     a bare `channel` for Auto) is the channel the hunt header shows. */
typedef enum {
    WhHuntStepAdd,
    WhHuntStepStart,
    WhHuntStepRunning,
    WhHuntStepFailed,
} WhHuntStep;

static WhHuntStep s_step;
static uint8_t s_tries;
static uint16_t s_wait;
static int16_t s_index;
static const char* s_fail;

static void wh_scene_hunt_fail(const char* why) {
    s_step = WhHuntStepFailed;
    s_fail = why;
}

static void wh_scene_hunt_send_start(WavehoundApp* app) {
    app->hunt_started = false;
    wh_marauder_foxhunt_ap(app, s_index);
}

static void wh_scene_hunt_step(WavehoundApp* app) {
    const WhTarget* t = &app->target;

    switch(s_step) {
    case WhHuntStepAdd: {
        furi_mutex_acquire(app->results_mutex, FuriWaitForever);
        const WhAddState state = app->add_state;
        const int16_t index = app->add_index;
        const uint8_t error = app->add_error;
        furi_mutex_release(app->results_mutex);

        if(state == WhAddDone) {
            s_index = index;
            s_step = WhHuntStepStart;
            s_tries = 0;
            s_wait = 0;
            wh_scene_hunt_send_start(app);
        } else if(state == WhAddFailed) {
            wh_scene_hunt_fail(error == WhAddErrMemory ? "low board memory" : "MAC rejected");
        } else if(++s_wait >= WH_HUNT_STEP_TICKS) {
            /* Re-adding is idempotent (dedup by MAC). */
            s_wait = 0;
            if(++s_tries >= WH_HUNT_TRIES) {
                wh_scene_hunt_fail("no board reply");
            } else {
                wh_add_begin(app, t->mac, t->channel);
            }
        }
        break;
    }

    case WhHuntStepStart:
        if(app->hunt_started) {
            s_step = WhHuntStepRunning;
            if(t->channel) {
                wh_marauder_set_channel(app, t->channel);
            } else {
                /* Auto: stay where the hunt put us, but learn where that is. */
                wh_marauder_query_channel(app);
            }
        } else if(++s_wait >= WH_HUNT_STEP_TICKS) {
            s_wait = 0;
            if(++s_tries >= WH_HUNT_TRIES) {
                wh_scene_hunt_fail("start failed");
            } else {
                /* Maybe the board was still busy (a lost stopscan): stop it and
                 * ask again. Safe, as no hunt is running yet. */
                wh_marauder_stop(app);
                wh_scene_hunt_send_start(app);
            }
        }
        break;

    case WhHuntStepRunning:
    case WhHuntStepFailed:
    default:
        break;
    }
}

static void wh_scene_hunt_feed_cb(void* context) {
    WavehoundApp* app = context;

    wh_scene_hunt_step(app);

    const char* status = "";
    if(s_step == WhHuntStepFailed) {
        status = s_fail;
    } else if(!app->board_alive) {
        status = "no board";
    } else if(s_step != WhHuntStepRunning) {
        status = "starting...";
    }
    wh_hunt_view_set_status(app->hunt, status);

    wh_hunt_feed_flush(app);
}

void wh_scene_hunt_on_enter(void* context) {
    WavehoundApp* app = context;
    const WhTarget* t = &app->target;

    /* Channel 0 until the board reports where the hunt is parked. */
    wh_hunt_view_set_target(app->hunt, t->name[0] ? t->name : t->mac, 0);

    if(app->settings.keep_backlight) {
        notification_message(app->notifications, &sequence_display_backlight_enforce_on);
    }

    /* Reset the RSSI accumulator before samples start arriving. */
    furi_mutex_acquire(app->results_mutex, FuriWaitForever);
    app->hunt_frames = 0;
    app->hunt_sum = 0;
    app->hunt_max = 0;
    app->hunt_last = 0;
    furi_mutex_release(app->results_mutex);

    app->hunt_started = false;
    app->hunt_channel = 0;
    wh_set_board_mode(app, WhBoardHunting);

    s_step = WhHuntStepAdd;
    s_tries = 0;
    s_wait = 0;
    s_index = -1;
    if(app->uart) {
        wh_add_begin(app, t->mac, t->channel);
    } else {
        wh_scene_hunt_fail("USART busy");
    }

    app->hunt_timer = furi_timer_alloc(wh_scene_hunt_feed_cb, FuriTimerTypePeriodic, app);
    furi_timer_start(app->hunt_timer, furi_ms_to_ticks(WH_FEED_PERIOD_MS));

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
    if(app->hunt_timer) {
        furi_timer_stop(app->hunt_timer);
        furi_timer_free(app->hunt_timer);
        app->hunt_timer = NULL;
    }
    wh_marauder_stop(app);
    wh_set_board_mode(app, WhBoardIdle);
    furi_mutex_acquire(app->results_mutex, FuriWaitForever);
    app->add_state = WhAddIdle;
    furi_mutex_release(app->results_mutex);
    app->hunt_started = false;
    /* Always release the enforce, regardless of the current setting: it is a
     * global display state, so toggling "Keep backlight" off between enter and
     * exit must not leave it stuck on. Harmless if it was never enforced. */
    notification_message(app->notifications, &sequence_display_backlight_enforce_auto);
}
