#include "../wavehound_i.h"
#include "../wh_marauder.h"

#include <stdio.h>

#define ITEM_SEARCH_AGAIN 0
/* Result i is submenu index i + 1. */

/* Scene state: FRESH clears the list and starts a new scan on enter; RESUME
 * keeps the results already found (set when drilling into a result's Details,
 * so returning does not throw them away). "Search again" always starts fresh.
 * Either way the board's own AP list restarts (scanall rebuilds it), which is
 * fine: rows are keyed by MAC and hunts look the index up by MAC. */
#define TARGET_LIST_FRESH 0
#define TARGET_LIST_RESUME 1

/* Rows already added to the submenu, so a tick only appends the newcomers and
 * never touches existing rows (the submenu keeps the user's highlight put). */
static size_t s_shown;       /* results examined (matched or not) */
static size_t s_rows_shown;  /* rows actually added for the current filter */
static size_t s_header_count; /* count last written to the header */
static uint8_t s_spin; /* animated scan indicator phase */
static uint32_t s_picked_item; /* submenu index of the last picked result */
static uint16_t s_ticks; /* ticks since the scan was (re)started */

/* Give the "#scanall" echo this long before scanning anyway (echo lost). */
#define WH_ARM_TIMEOUT_TICKS (1000 / WH_TICK_MS)
/* Only call the board missing after this long without a line from it. */
#define WH_NO_BOARD_TICKS (3000 / WH_TICK_MS)

static void wh_scene_target_list_callback(void* context, uint32_t index) {
    WavehoundApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

static void format_label(const WhTarget* t, char* out, size_t size) {
    if(t->is_ap) {
        /* hidden networks have no SSID: show the BSSID */
        snprintf(out, size, "%d c%u %s", t->rssi, t->channel, t->name[0] ? t->name : t->mac);
    } else {
        /* Marauder's station scan reports no per-station RSSI, so there is no
         * signal figure to show here (it appears once you hunt). '*' marks a
         * randomized (privacy) MAC. */
        snprintf(out, size, "c%u %s%s", t->channel, t->mac, t->randomized ? "*" : "");
    }
}

static const char* wh_scene_target_list_noun(WhSearchKind kind) {
    return kind == WhSearchAccessPoints ? "networks" :
           kind == WhSearchClients      ? "clients" :
                                          "devices";
}

/* Start a Marauder scanall (finds APs and their stations). Marauder's AP/STA
 * scan always hops every channel regardless of the ChanHop setting, so a
 * device/client channel choice can't restrict the scan itself; it is applied
 * later, at hunt time, to park the fox hunt. scanall rebuilds the board's AP
 * list itself; a fresh start also clears its station list (clearlist) so known
 * stations are printed again. The worker ignores lines until the "#scanall"
 * echo, dropping what the previous scan printed before it saw our stopscan. */
static void wh_scene_target_list_send_discover(WavehoundApp* app, bool fresh) {
    wh_set_board_mode(app, WhBoardScanArming);
    s_ticks = 0;
    wh_marauder_stop(app);
    if(fresh) wh_marauder_clear(app);
    wh_marauder_scan(app);
}

/* Header text: either the AP-picker prompt, or a live "scanning" count. */
static void wh_scene_target_list_set_header(WavehoundApp* app, size_t count) {
    char header[40];
    if(app->picking_ap_channel) {
        strlcpy(
            header,
            app->channel_purpose == WhChannelForDeviceSearch ? "Clients of which AP:" :
                                                               "Use channel of:",
            sizeof(header));
    } else if(!app->uart) {
        strlcpy(header, "USART busy!", sizeof(header));
    } else if(!app->board_alive && s_ticks >= WH_NO_BOARD_TICKS) {
        strlcpy(header, "No board! Check it", sizeof(header));
    } else {
        /* A block bouncing across a bracketed field -- reads as a radar
         * scanning, even at zero found. Every frame holds the same glyphs
         * (one '#', three spaces), so the header width stays constant; the
         * proportional header font would otherwise make the line jitter as
         * the block moved. */
        static const char* const scan_frames[] = {
            "[#   ]", "[ #  ]", "[  # ]", "[   #]", "[  # ]", "[ #  ]"};
        const char* bar = scan_frames[s_spin % (sizeof(scan_frames) / sizeof(scan_frames[0]))];
        /* A full table drops whatever is heard next: say so. */
        snprintf(
            header, sizeof(header), "Scan %s %u %s", bar, (unsigned)count,
            app->result_count >= WH_MAX_RESULTS ? "(full)" :
                                                  wh_scene_target_list_noun(app->search_kind));
    }
    submenu_set_header(app->submenu, header);
}

/* Does this result belong in the list for the current search kind? scanall
 * always finds both APs and stations; we show only the kind being hunted (and,
 * for a client search, only stations tied to the chosen AP). */
static bool wh_scene_target_list_wanted(WavehoundApp* app, const WhTarget* t) {
    switch(app->search_kind) {
    case WhSearchAccessPoints:
        return t->is_ap;
    case WhSearchClients:
        /* The station line names its AP's MAC, so this holds even before (or
         * without) the AP's own row. */
        return !t->is_ap && strcmp(t->ap_mac, app->clients_ap.mac) == 0;
    case WhSearchDevices:
    default:
        return !t->is_ap;
    }
}

/* Append submenu rows for results heard since s_shown that match the filter;
 * returns the count of matching rows now shown. The submenu item id stays the
 * result's own index + 1 (stable across filtering), so the pick handler can
 * look it up directly. Never rebuilds existing rows. */
static size_t wh_scene_target_list_sync_rows(WavehoundApp* app) {
    char label[64];
    furi_mutex_acquire(app->results_mutex, FuriWaitForever);
    const size_t count = app->result_count;
    for(; s_shown < count; s_shown++) {
        if(!wh_scene_target_list_wanted(app, &app->results[s_shown])) continue;
        format_label(&app->results[s_shown], label, sizeof(label));
        submenu_add_item(app->submenu, label, s_shown + 1, wh_scene_target_list_callback, app);
        s_rows_shown++;
    }
    furi_mutex_release(app->results_mutex);
    return s_rows_shown;
}

/* Clear the list and (re)start a continuous scan. */
static void wh_scene_target_list_restart(WavehoundApp* app) {
    /* Stop storing the old scan's lines before emptying the table. */
    wh_set_board_mode(app, WhBoardIdle);
    furi_mutex_acquire(app->results_mutex, FuriWaitForever);
    app->result_count = 0;
    furi_mutex_release(app->results_mutex);
    s_shown = 0;
    s_rows_shown = 0;
    s_header_count = 0;

    submenu_reset(app->submenu);
    submenu_add_item(
        app->submenu, "~ Search again", ITEM_SEARCH_AGAIN, wh_scene_target_list_callback, app);
    wh_scene_target_list_set_header(app, 0);

    if(app->uart) wh_scene_target_list_send_discover(app, true);
}

/* Re-enter after drilling into a result: keep every row found so far and just
 * rebuild the submenu that on_exit tore down, then resume the board scan. */
static void wh_scene_target_list_resume(WavehoundApp* app) {
    s_shown = 0;
    s_rows_shown = 0;
    submenu_reset(app->submenu);
    submenu_add_item(
        app->submenu, "~ Search again", ITEM_SEARCH_AGAIN, wh_scene_target_list_callback, app);

    const size_t count = wh_scene_target_list_sync_rows(app);
    wh_scene_target_list_set_header(app, count);
    s_header_count = count;

    /* No clearlist: the stations Marauder already knows are not printed
     * again, but their rows are still here. */
    if(app->uart) wh_scene_target_list_send_discover(app, false);
}

void wh_scene_target_list_on_enter(void* context) {
    WavehoundApp* app = context;
    const bool resume =
        scene_manager_get_scene_state(app->scene_manager, WhSceneTargetList) ==
        TARGET_LIST_RESUME;
    if(resume) {
        wh_scene_target_list_resume(app);
        submenu_set_selected_item(app->submenu, s_picked_item);
    } else {
        wh_scene_target_list_restart(app);
        submenu_set_selected_item(app->submenu, ITEM_SEARCH_AGAIN);
    }
    /* The next entry is fresh unless a pick arms RESUME again. */
    scene_manager_set_scene_state(app->scene_manager, WhSceneTargetList, TARGET_LIST_FRESH);
    view_dispatcher_switch_to_view(app->view_dispatcher, WhViewSubmenu);
}

bool wh_scene_target_list_on_event(void* context, SceneManagerEvent event) {
    WavehoundApp* app = context;

    if(event.type == SceneManagerEventTypeTick) {
        if(s_ticks < UINT16_MAX) s_ticks++;
        if(app->board_mode == WhBoardScanArming && s_ticks >= WH_ARM_TIMEOUT_TICKS) {
            wh_set_board_mode(app, WhBoardScanning); /* "#scanall" echo lost */
        }
        /* Append devices heard since the last tick; never rebuild, so the
         * highlight stays where the user left it. */
        const size_t count = wh_scene_target_list_sync_rows(app);
        /* While a live scan is on, refresh every tick so the sweep animates;
         * otherwise only rewrite the header when the count actually changes. */
        if(app->uart && !app->picking_ap_channel) {
            s_spin++;
            wh_scene_target_list_set_header(app, count);
        } else if(count != s_header_count) {
            wh_scene_target_list_set_header(app, count);
        }
        s_header_count = count;
        return false;
    }

    if(event.type != SceneManagerEventTypeCustom) return false;

    if(event.event == ITEM_SEARCH_AGAIN) {
        wh_scene_target_list_restart(app);
        return true;
    }

    WhTarget picked_target;
    bool picked = false;
    furi_mutex_acquire(app->results_mutex, FuriWaitForever);
    if(event.event - 1 < app->result_count) {
        picked_target = app->results[event.event - 1];
        picked = true;
    }
    furi_mutex_release(app->results_mutex);
    if(!picked) return false;

    if(app->picking_ap_channel) {
        if(app->channel_purpose == WhChannelForDeviceSearch) {
            /* Device search scoped to this AP: list the stations associated with
             * it, on its channel. The next scan reads clients_ap. */
            app->clients_ap = picked_target;
            app->device_search_clients = true;
        } else {
            /* MAC hunt only needs the channel number to seed the typed MAC's hunt. */
            app->target.channel = picked_target.channel;
        }
        app->picking_ap_channel = false;
        app->channel_pick_pending = true;
        scene_manager_search_and_switch_to_previous_scene(
            app->scene_manager, WhSceneChannelSelect);
        return true;
    }

    app->target = picked_target;

    /* Keep the found list when we come back from Details, and land back on the
     * row just picked. */
    s_picked_item = event.event;
    scene_manager_set_scene_state(app->scene_manager, WhSceneTargetList, TARGET_LIST_RESUME);

    /* Pre-fill manual entry with the last target. */
    wh_parse_mac(app->target.mac, app->mac_bytes);
    scene_manager_next_scene(app->scene_manager, WhSceneDetails);
    return true;
}

void wh_scene_target_list_on_exit(void* context) {
    WavehoundApp* app = context;
    /* Stop the board scanning whenever we leave (pick, back or restart handles
     * its own resend). Harmless once the board is idle. The board keeps its
     * list after stopscan, so a hunt launched next finds the picked target's
     * entry by MAC. */
    wh_set_board_mode(app, WhBoardIdle);
    wh_marauder_stop(app);
    submenu_reset(app->submenu);
}
