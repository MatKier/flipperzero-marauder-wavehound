#pragma once

#include "wavehound_i.h"

/** Load saved MACs from the SD card into app->saved. Missing file = empty list. */
void wh_store_load(WavehoundApp* app);

typedef enum {
    WhStoreOk,
    WhStoreFull,        /* WH_MAX_SAVED entries already */
    WhStoreWriteFailed, /* empty input, or the SD card write failed (not added) */
} WhStoreResult;

/**
 * Append a saved MAC (name, "aa:bb:..", channel; channel 0 = auto) and persist.
 * Writes are atomic (temp file + rename); on failure the entry is not added.
 */
WhStoreResult wh_store_add(WavehoundApp* app, const char* name, const char* mac, uint8_t channel);

/** Replace the entry at `index` with new values and persist. */
void wh_store_update(
    WavehoundApp* app,
    size_t index,
    const char* name,
    const char* mac,
    uint8_t channel);

/** Remove the entry at `index` and persist. */
void wh_store_delete(WavehoundApp* app, size_t index);
