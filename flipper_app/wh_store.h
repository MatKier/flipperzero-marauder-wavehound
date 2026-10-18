#pragma once

#include "wavehound_i.h"

/** Load saved MACs from the SD card into app->saved. Missing file = empty list. */
void wh_store_load(WavehoundApp* app);

/**
 * Append a saved MAC (name, "aa:bb:..", channel; channel 0 = auto) and persist.
 * Returns false if the list is full or the input is empty.
 */
bool wh_store_add(WavehoundApp* app, const char* name, const char* mac, uint8_t channel);

/** Remove the entry at `index` and persist. */
void wh_store_delete(WavehoundApp* app, size_t index);
