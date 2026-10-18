#pragma once

#include <gui/scene_manager.h>

#define ADD_SCENE(prefix, name, id) WhScene##id,
typedef enum {
#include "wh_scene_config.h"
    WhSceneNum,
} WhScene;
#undef ADD_SCENE

extern const SceneManagerHandlers wh_scene_handlers;

#define ADD_SCENE(prefix, name, id) void prefix##_scene_##name##_on_enter(void*);
#include "wh_scene_config.h"
#undef ADD_SCENE

#define ADD_SCENE(prefix, name, id) \
    bool prefix##_scene_##name##_on_event(void* context, SceneManagerEvent event);
#include "wh_scene_config.h"
#undef ADD_SCENE

#define ADD_SCENE(prefix, name, id) void prefix##_scene_##name##_on_exit(void* context);
#include "wh_scene_config.h"
#undef ADD_SCENE
