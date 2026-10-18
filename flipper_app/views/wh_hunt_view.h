#pragma once

#include <gui/view.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct WhHuntView WhHuntView;

WhHuntView* wh_hunt_view_alloc(void);
void wh_hunt_view_free(WhHuntView* hunt);
View* wh_hunt_view_get_view(WhHuntView* hunt);

/** Reset the view for a new target. */
void wh_hunt_view_set_target(WhHuntView* hunt, const char* name, uint8_t channel);

/** Feed one `H` sample from the board. Safe to call from any thread. */
void wh_hunt_view_push_sample(
    WhHuntView* hunt,
    uint8_t channel,
    uint32_t frames,
    int8_t last,
    int8_t avg,
    int8_t max,
    bool locked);
