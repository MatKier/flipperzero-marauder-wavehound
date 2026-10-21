#include "../wavehound_i.h"

/* Injected by fbt from fap_version in application.fam (e.g. "1.3"). */
#ifndef FAP_VERSION
#define FAP_VERSION "?"
#endif

static const char* const about_text =
    "\e#Wavehound " FAP_VERSION "\n"
    "WiFi fox hunting for a\n"
    "WiFi dev board running\n"
    "ESP32 Marauder v1.19.0.\n\n"
    "\e#Scanning\n"
    "Networks and devices\n"
    "appear live as they are\n"
    "heard. Pick one anytime\n"
    "or keep waiting. Select\n"
    "~ Search again to restart.\n\n"
    "\e#Hunt access point\n"
    "Scan for networks, pick\n"
    "one, then\n"
    "walk. Clicks speed up as\n"
    "you get closer.\n\n"
    "\e#Hunt devices\n"
    "Scan for stations seen\n"
    "with an access point:\n"
    "phones, laptops, plugs.\n"
    "The scan sweeps every\n"
    "channel; a channel pick\n"
    "just seeds the hunt you\n"
    "start next.\n"
    "* = randomized MAC\n"
    "(may change over time).\n\n"
    "\e#Enter MAC\n"
    "Type a MAC, then pick\n"
    "Auto, a channel, or a\n"
    "nearby AP's channel. A\n"
    "chosen channel parks the\n"
    "hunt there. The board\n"
    "can't sweep: Auto stays\n"
    "on the channel Marauder\n"
    "knows for that MAC (ch 1\n"
    "if new). Pick the channel\n"
    "if you know it.\n\n"
    "\e#Device details\n"
    "After picking a result:\n"
    "vendor, type, signal, and\n"
    "an AP's security.\n"
    "Hunt or Save it.\n\n"
    "\e#Hunt screen\n"
    "OK: mute  Left: reset pk\n"
    "Right: save this MAC\n"
    "?ch = not hearing target\n"
    "ch? = waiting for board\n\n"
    "\e#Saved MACs\n"
    "Favorites saved from the\n"
    "hunt screen. Pick one for\n"
    "Hunt / Edit / Delete.\n\n"
    "\e#Board info\n"
    "Firmware and identity of\n"
    "the Marauder board.\n\n"
    "\e#Settings\n"
    "Click volume, sound,\n"
    "saved-hunt channel.\n"
    "Range: Near/Normal/Far.\n"
    "Haptic buzz and keep\n"
    "backlight on.\n\n"
    "Receive-only. Only hunt\n"
    "devices you own or are\n"
    "allowed to locate.";

void wh_scene_about_on_enter(void* context) {
    WavehoundApp* app = context;
    widget_reset(app->widget);
    widget_add_text_scroll_element(app->widget, 0, 0, 128, 64, about_text);
    view_dispatcher_switch_to_view(app->view_dispatcher, WhViewWidget);
}

bool wh_scene_about_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void wh_scene_about_on_exit(void* context) {
    WavehoundApp* app = context;
    widget_reset(app->widget);
}
