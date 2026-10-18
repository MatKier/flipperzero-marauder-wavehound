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
            if(ch < 0 || ch > WH_MAX_CHANNEL || !mac[0] || !name[0]) continue;

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

static void wh_store_write(WavehoundApp* app) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_common_mkdir(storage, WH_SAVE_DIR); /* no-op if it already exists */
    Stream* stream = buffered_file_stream_alloc(storage);

    if(buffered_file_stream_open(stream, WH_SAVE_FILE, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        for(size_t i = 0; i < app->saved_count; i++) {
            const WhSavedMac* s = &app->saved[i];
            stream_write_format(stream, "%u\t%s\t%s\n", s->channel, s->mac, s->name);
        }
    }

    buffered_file_stream_close(stream);
    stream_free(stream);
    furi_record_close(RECORD_STORAGE);
}

bool wh_store_add(WavehoundApp* app, const char* name, const char* mac, uint8_t channel) {
    if(app->saved_count >= WH_MAX_SAVED) return false;
    if(!name || !name[0] || !mac || !mac[0]) return false;

    WhSavedMac* s = &app->saved[app->saved_count];
    strlcpy(s->name, name, sizeof(s->name));
    sanitize_name(s->name);
    strlcpy(s->mac, mac, sizeof(s->mac));
    s->channel = channel;
    app->saved_count++;

    wh_store_write(app);
    return true;
}

void wh_store_delete(WavehoundApp* app, size_t index) {
    if(index >= app->saved_count) return;
    for(size_t i = index; i + 1 < app->saved_count; i++) {
        app->saved[i] = app->saved[i + 1];
    }
    app->saved_count--;
    wh_store_write(app);
}
