/*
 * Wavehound - WiFi fox hunting with the Flipper Zero WiFi dev board.
 *
 * Hunt an access point or any other device found during a live scan, or a
 * MAC typed in by hand. The board reports the target's signal
 * strength; the Flipper turns it into a meter, a graph and Geiger clicks.
 */
#include "wavehound_i.h"
#include "wh_marauder.h"
#include "wh_store.h"

#include <furi_hal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TAG "Wavehound"

/* ---------- helpers shared with scenes ---------- */

void wh_send(WavehoundApp* app, const char* line) {
    if(app->uart) wh_uart_send(app->uart, line);
}

static int hex_nibble(char c) {
    if(c >= '0' && c <= '9') return c - '0';
    if(c >= 'a' && c <= 'f') return c - 'a' + 10;
    if(c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool wh_parse_mac(const char* str, uint8_t mac[6]) {
    for(int i = 0; i < 6; i++) {
        int hi = hex_nibble(str[i * 3]);
        int lo = hi < 0 ? -1 : hex_nibble(str[i * 3 + 1]);
        if(lo < 0) return false;
        if(i < 5 && str[i * 3 + 2] != ':') return false;
        mac[i] = (uint8_t)(hi << 4 | lo);
    }
    return true;
}

/* ---------- board line parsing (UART worker thread) ---------- */

/* Find an existing result by MAC string, or reserve a new slot. Returns NULL
 * only when the table is full. Caller must hold results_mutex. Marauder may
 * re-send a device, and the live list appends in discovery order, so matching
 * keeps each device in its original slot instead of duplicating it. */
static WhTarget* wh_results_slot(WavehoundApp* app, const char* mac) {
    for(size_t i = 0; i < app->result_count; i++) {
        if(strcmp(app->results[i].mac, mac) == 0) return &app->results[i];
    }
    if(app->result_count >= WH_MAX_RESULTS) return NULL;
    WhTarget* t = &app->results[app->result_count++];
    memset(t, 0, sizeof(WhTarget));
    t->mo_index = -1;
    t->ap_index = -1;
    t->sta_index = -1;
    return t;
}

/* Marauder's AP list is indexed by the order APs are first printed; match an
 * already-seen BSSID to recover its index for a station's "-> ap:" reference. */
static int16_t wh_ap_index_for_mac(WavehoundApp* app, const uint8_t mac[6]) {
    char s[18];
    snprintf(s, sizeof(s), "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3],
             mac[4], mac[5]);
    for(size_t i = 0; i < app->result_count; i++) {
        if(app->results[i].is_ap && strcmp(app->results[i].mac, s) == 0)
            return app->results[i].mo_index;
    }
    return -1;
}

static void wh_on_scan_line(WavehoundApp* app, char* line) {
    WhMarauderAp ap;
    WhMarauderSta sta;
    if(wh_marauder_parse_ap_line(line, &ap)) {
        furi_mutex_acquire(app->results_mutex, FuriWaitForever);
        WhTarget* t = wh_results_slot(app, ap.mac_str);
        if(t) {
            strlcpy(t->mac, ap.mac_str, sizeof(t->mac));
            strlcpy(t->name, ap.ssid, sizeof(t->name));
            t->channel = ap.channel;
            t->rssi = ap.rssi;
            t->is_ap = true;
            t->randomized = (ap.mac[0] & 0x02) != 0;
            if(t->mo_index < 0) t->mo_index = (int16_t)app->scan_ap_count++;
        }
        furi_mutex_release(app->results_mutex);
    } else if(wh_marauder_parse_sta_line(line, &sta)) {
        furi_mutex_acquire(app->results_mutex, FuriWaitForever);
        WhTarget* t = wh_results_slot(app, sta.sta_mac_str);
        if(t && !t->is_ap) {
            strlcpy(t->mac, sta.sta_mac_str, sizeof(t->mac));
            t->is_ap = false;
            t->randomized = (sta.sta_mac[0] & 0x02) != 0;
            t->ap_index = wh_ap_index_for_mac(app, sta.ap_mac);
            /* Marauder prints "<n>: " where n is the post-insert list size, so
             * this station's index is n-1; we track it by appearance order. */
            if(t->sta_index < 0) t->sta_index = (int16_t)(app->scan_sta_count++);
        }
        furi_mutex_release(app->results_mutex);
    }
}

static void wh_on_line(char* line, void* context) {
    WavehoundApp* app = context;

    /* Marauder prints its "> " prompt without a newline, so it prefixes the
     * first streamed line after a command (e.g. "> -45 Ch: 6 .."). Strip any
     * such leading prompt(s) before parsing. */
    while(line[0] == '>' && line[1] == ' ') line += 2;

    switch(app->board_mode) {
    case WhBoardScanning:
        wh_on_scan_line(app, line);
        break;

    case WhBoardHunting: {
        int8_t rssi;
        if(wh_marauder_parse_foxhunt(line, &rssi)) {
            furi_mutex_acquire(app->results_mutex, FuriWaitForever);
            app->hunt_frames++;
            app->hunt_sum += rssi;
            app->hunt_last = rssi;
            if(app->hunt_frames == 1 || rssi > app->hunt_max) app->hunt_max = rssi;
            furi_mutex_release(app->results_mutex);
        }
        break;
    }

    case WhBoardInfo:
        /* Collect "<label>: <value>" lines; the scene refreshes on its tick. */
        if(strchr(line, ':')) {
            furi_mutex_acquire(app->results_mutex, FuriWaitForever);
            furi_string_cat_printf(app->board_info, "%s\n", line);
            furi_mutex_release(app->results_mutex);
        }
        break;

    default:
        break;
    }

    /* "Added AP [n]" can arrive in any mode after a typed-MAC add. */
    int16_t idx;
    if(wh_marauder_parse_added_ap(line, &idx)) app->pending_ap_index = idx;
}

/* Drain the fox-hunt RSSI accumulator into the hunt view as one 100 ms sample. */
void wh_hunt_feed_flush(WavehoundApp* app) {
    furi_mutex_acquire(app->results_mutex, FuriWaitForever);
    uint16_t frames = app->hunt_frames;
    int32_t sum = app->hunt_sum;
    int8_t last = app->hunt_last;
    int8_t max = app->hunt_max;
    app->hunt_frames = 0;
    app->hunt_sum = 0;
    app->hunt_max = 0;
    furi_mutex_release(app->results_mutex);

    int8_t avg = frames ? (int8_t)(sum / (int32_t)frames) : 0;
    /* locked = we are parked on a channel (not auto-hopping). */
    wh_hunt_view_push_sample(
        app->hunt, app->hunt_channel, frames, last, avg, frames ? max : 0,
        app->hunt_channel != 0);
}

/* Hunt view asks (Right key) to save the current target. */
static void wh_hunt_save_cb(void* context) {
    WavehoundApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, WhEventSaveTarget);
}

/* ---------- dispatcher plumbing ---------- */

static bool wh_custom_event_callback(void* context, uint32_t event) {
    WavehoundApp* app = context;
    return scene_manager_handle_custom_event(app->scene_manager, event);
}

static bool wh_back_event_callback(void* context) {
    WavehoundApp* app = context;
    return scene_manager_handle_back_event(app->scene_manager);
}

static void wh_tick_event_callback(void* context) {
    WavehoundApp* app = context;
    app->board_alive = app->uart && wh_uart_board_alive(app->uart);
    scene_manager_handle_tick_event(app->scene_manager);
}

static WavehoundApp* wh_app_alloc(void) {
    WavehoundApp* app = malloc(sizeof(WavehoundApp));
    memset(app, 0, sizeof(WavehoundApp));
    app->results_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->board_info = furi_string_alloc();

    app->gui = furi_record_open(RECORD_GUI);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);
    app->view_dispatcher = view_dispatcher_alloc();
    app->scene_manager = scene_manager_alloc(&wh_scene_handlers, app);
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(app->view_dispatcher, wh_custom_event_callback);
    view_dispatcher_set_navigation_event_callback(app->view_dispatcher, wh_back_event_callback);
    view_dispatcher_set_tick_event_callback(
        app->view_dispatcher, wh_tick_event_callback, WH_TICK_MS);
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    app->submenu = submenu_alloc();
    view_dispatcher_add_view(app->view_dispatcher, WhViewSubmenu, submenu_get_view(app->submenu));
    app->byte_input = byte_input_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, WhViewByteInput, byte_input_get_view(app->byte_input));
    app->text_input = text_input_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, WhViewTextInput, text_input_get_view(app->text_input));
    app->widget = widget_alloc();
    view_dispatcher_add_view(app->view_dispatcher, WhViewWidget, widget_get_view(app->widget));
    app->hunt = wh_hunt_view_alloc();
    wh_hunt_view_set_save_callback(app->hunt, wh_hunt_save_cb, app);
    view_dispatcher_add_view(app->view_dispatcher, WhViewHunt, wh_hunt_view_get_view(app->hunt));
    app->var_item_list = variable_item_list_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, WhViewVarItemList, variable_item_list_get_view(app->var_item_list));

    wh_store_load(app);
    wh_settings_load(app);
    wh_hunt_view_set_volume(app->hunt, app->settings.click_volume);
    wh_hunt_view_set_sound(app->hunt, !app->settings.mute_default);
    wh_hunt_view_set_haptic(app->hunt, app->settings.haptic);
    int8_t range_floor, range_ceil;
    wh_settings_range_bounds(app->settings.range_mode, &range_floor, &range_ceil);
    wh_hunt_view_set_range(app->hunt, range_floor, range_ceil);

    return app;
}

static void wh_app_free(WavehoundApp* app) {
    view_dispatcher_remove_view(app->view_dispatcher, WhViewSubmenu);
    view_dispatcher_remove_view(app->view_dispatcher, WhViewByteInput);
    view_dispatcher_remove_view(app->view_dispatcher, WhViewTextInput);
    view_dispatcher_remove_view(app->view_dispatcher, WhViewWidget);
    view_dispatcher_remove_view(app->view_dispatcher, WhViewHunt);
    view_dispatcher_remove_view(app->view_dispatcher, WhViewVarItemList);
    submenu_free(app->submenu);
    byte_input_free(app->byte_input);
    text_input_free(app->text_input);
    widget_free(app->widget);
    variable_item_list_free(app->var_item_list);
    wh_hunt_view_free(app->hunt);

    scene_manager_free(app->scene_manager);
    view_dispatcher_free(app->view_dispatcher);
    /* Guarantee the hunt screen's "Keep backlight" enforce can never outlive
     * the app: scene_manager_free does not run the active scene's on_exit, so
     * a quit straight from the hunt screen would otherwise leave the backlight
     * forced on system-wide. Releasing here is a no-op if nothing enforced it. */
    notification_message(app->notifications, &sequence_display_backlight_enforce_auto);
    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_GUI);
    furi_mutex_free(app->results_mutex);
    furi_string_free(app->board_info);
    free(app);
}

static bool wh_board_power_on(WavehoundApp* app) {
    if(furi_hal_power_is_otg_enabled()) return true;
    /* The boost converter sometimes needs a couple of attempts. */
    for(int attempt = 0; attempt < 5; attempt++) {
        if(furi_hal_power_enable_otg()) {
            app->otg_enabled_by_us = true;
            return true;
        }
        furi_delay_ms(10);
    }
    return false;
}

int32_t wavehound_app(void* p) {
    UNUSED(p);
    WavehoundApp* app = wh_app_alloc();

    /* The expansion service also listens on the USART; take it over. */
    app->expansion = furi_record_open(RECORD_EXPANSION);
    expansion_disable(app->expansion);
    if(!wh_board_power_on(app)) FURI_LOG_W(TAG, "could not enable 5V on GPIO");

    app->uart = wh_uart_alloc(wh_on_line, app);
    if(app->uart) {
        wh_marauder_stop(app); /* leave whatever the board was doing */
    } else {
        FURI_LOG_E(TAG, "USART busy");
    }

    scene_manager_next_scene(app->scene_manager, WhSceneStart);
    view_dispatcher_run(app->view_dispatcher);

    if(app->uart) {
        /* Leaving the app: stop any scan/hunt. The board stays powered on the
         * Flipper's always-on 3V3 and keeps its own idle state. */
        wh_marauder_stop(app);
        wh_uart_free(app->uart);
    }
    if(app->otg_enabled_by_us) furi_hal_power_disable_otg();
    expansion_enable(app->expansion);
    furi_record_close(RECORD_EXPANSION);

    wh_app_free(app);
    return 0;
}
