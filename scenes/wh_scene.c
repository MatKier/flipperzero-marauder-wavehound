#include "wh_scene.h"

#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_enter,
static void (*const wh_on_enter_handlers[])(void*) = {
#include "wh_scene_config.h"
};
#undef ADD_SCENE

#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_event,
static bool (*const wh_on_event_handlers[])(void* context, SceneManagerEvent event) = {
#include "wh_scene_config.h"
};
#undef ADD_SCENE

#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_exit,
static void (*const wh_on_exit_handlers[])(void* context) = {
#include "wh_scene_config.h"
};
#undef ADD_SCENE

const SceneManagerHandlers wh_scene_handlers = {
    .on_enter_handlers = wh_on_enter_handlers,
    .on_event_handlers = wh_on_event_handlers,
    .on_exit_handlers = wh_on_exit_handlers,
    .scene_num = WhSceneNum,
};
