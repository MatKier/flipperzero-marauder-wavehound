#include "wh_store.h"

#include <furi.h>
#include <storage/storage.h>
#include <toolbox/stream/buffered_file_stream.h>
#include <toolbox/stream/stream.h>
#include <stdlib.h>
#include <string.h>

#ifndef APP_DATA_PATH
#define APP_DATA_PATH(p) EXT_PATH("apps_data/wavehound/" p)
#endif

#define WH_SAVE_DIR APP_DATA_PATH("")
#define WH_SAVE_FILE APP_DATA_PATH("saved.txt")
#define WH_SAVE_TMP APP_DATA_PATH("saved.tmp")

/* One entry per line: "<channel>\t<mac>\t<name>". Tabs/newlines never appear in
 * a stored name (sanitized on save), so the split is unambiguous. */

static void sanitize_name(char* s) {
    for(; *s; s++) {
        if(*s == '\t' || *s == '\n' || *s == '\r') *s = ' ';
    }
}

void wh_store_load(WavehoundApp* app) {
    app->saved_count = 0;
    Storage* storage = furi_record_open(RECORD_STORAGE);
    Stream* stream = buffered_file_stream_alloc(storage);

    if(buffered_file_stream_open(stream, WH_SAVE_FILE, FSAM_READ, FSOM_OPEN_EXISTING)) {
        FuriString* line = furi_string_alloc();
        while(app->saved_count < WH_MAX_SAVED && stream_read_line(stream, line)) {
            furi_string_trim(line, "\r\n");
            if(furi_string_empty(line)) continue;

            char buf[8 + 18 + WH_SAVED_NAME_MAX + 4];
            strlcpy(buf, furi_string_get_cstr(line), sizeof(buf));

            char* tab1 = strchr(buf, '\t');
            if(!tab1) continue;
            *tab1 = '\0';
            char* mac = tab1 + 1;
            char* tab2 = strchr(mac, '\t');
            if(!tab2) continue;
            *tab2 = '\0';
            char* name = tab2 + 1;

            int ch = atoi(buf);
            uint8_t mac_bytes[6];
            if(ch < 0 || ch > WH_MAX_CHANNEL || !name[0]) continue;
            /* A hand-edited bad MAC would only be refused by the board later. */
            if(strlen(mac) != 17 || !wh_parse_mac(mac, mac_bytes)) continue;

            WhSavedMac* s = &app->saved[app->saved_count];
            strlcpy(s->mac, mac, sizeof(s->mac));
            strlcpy(s->name, name, sizeof(s->name));
            s->channel = (uint8_t)ch;
            app->saved_count++;
        }
        furi_string_free(line);
    }

    buffered_file_stream_close(stream);
    stream_free(stream);
    furi_record_close(RECORD_STORAGE);
}

/* Write the list to a temp file and rename it over the real one, so a full SD
 * card or a power cut mid-write leaves the previous list intact. */
static bool wh_store_write(WavehoundApp* app) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_common_mkdir(storage, WH_SAVE_DIR); /* no-op if it already exists */
    Stream* stream = buffered_file_stream_alloc(storage);

    bool ok = buffered_file_stream_open(stream, WH_SAVE_TMP, FSAM_WRITE, FSOM_CREATE_ALWAYS);
    for(size_t i = 0; ok && i < app->saved_count; i++) {
        const WhSavedMac* s = &app->saved[i];
        ok = stream_write_format(stream, "%u\t%s\t%s\n", s->channel, s->mac, s->name) > 0;
    }
    ok = ok && buffered_file_stream_sync(stream);

    buffered_file_stream_close(stream);
    stream_free(stream);
    if(ok) ok = storage_common_rename(storage, WH_SAVE_TMP, WH_SAVE_FILE) == FSE_OK;
    furi_record_close(RECORD_STORAGE);
    return ok;
}

WhStoreResult wh_store_add(WavehoundApp* app, const char* name, const char* mac, uint8_t channel) {
    if(app->saved_count >= WH_MAX_SAVED) return WhStoreFull;
    if(!name || !name[0] || !mac || !mac[0]) return WhStoreWriteFailed;

    WhSavedMac* s = &app->saved[app->saved_count];
    strlcpy(s->name, name, sizeof(s->name));
    sanitize_name(s->name);
    strlcpy(s->mac, mac, sizeof(s->mac));
    s->channel = channel;
    app->saved_count++;

    if(!wh_store_write(app)) {
        app->saved_count--; /* keep memory in step with the card */
        return WhStoreWriteFailed;
    }
    return WhStoreOk;
}

void wh_store_update(
    WavehoundApp* app,
    size_t index,
    const char* name,
    const char* mac,
    uint8_t channel) {
    if(index >= app->saved_count || !name || !name[0] || !mac || !mac[0]) return;
    WhSavedMac* s = &app->saved[index];
    strlcpy(s->name, name, sizeof(s->name));
    sanitize_name(s->name);
    strlcpy(s->mac, mac, sizeof(s->mac));
    s->channel = channel;
    wh_store_write(app);
}

void wh_store_delete(WavehoundApp* app, size_t index) {
    if(index >= app->saved_count) return;
    for(size_t i = index; i + 1 < app->saved_count; i++) {
        app->saved[i] = app->saved[i + 1];
    }
    app->saved_count--;
    wh_store_write(app);
}
