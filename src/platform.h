#ifndef INCLUDE_TOM_ENGINE_PLATFORM_H
#define INCLUDE_TOM_ENGINE_PLATFORM_H

#include "maths.h"
#include <thread>

struct InputState {
  Transform left_hand_transform;
  Transform right_hand_transform;
  bool      left_hand_select;
  bool      right_hand_select;
  bool      gamepad_select;
};

void init_thread(void (*function_ptr)(), const uint32_t thread_pool_index);
uint32_t get_thread_pool_size();
void platform_request_exit();

extern float      delta_time_seconds;
extern InputState input_state;
extern std::atomic<Vector3f> hmd_global_position;

#if defined(OCULUS_QUEST_2)
  #include <android/log.h>
  #ifndef NDEBUG
    #define DEBUG_LOG(...) __android_log_print(ANDROID_LOG_DEBUG, "DEBUG", __VA_ARGS__)
  #else
    #define DEBUG_LOG(...)
  #endif

  #include <stdio.h>
  #include <android/asset_manager.h>
  FILE* android_fopen(const char* file_name, const char* mode);
  #define fopen(name, mode) android_fopen(name, mode)
#elif defined(OCULUS_PC)
  #ifndef NDEBUG
    #include <cstdio>
    #define DEBUG_LOG(...) printf(__VA_ARGS__)
  #else
    #define DEBUG_LOG(...)
  #endif
#endif

#endif  // INCLUDE_TOM_ENGINE_PLATFORM_H


//////////////////////////////////////////////////


#ifdef TOM_ENGINE_PLATFORM_IMPLEMENTATION
#ifndef TOM_ENGINE_PLATFORM_IMPLEMENTATION_SINGLE
#define TOM_ENGINE_PLATFORM_IMPLEMENTATION_SINGLE

#if defined(OCULUS_PC)
  #include "platform_oculus_pc.cpp"
#elif defined(OCULUS_QUEST_2)
  #include "platform_oculus_quest_2.cpp"
#endif

#endif  // TOM_ENGINE_PLATFORM_IMPLEMENTATION_SINGLE
#endif  // TOM_ENGINE_PLATFORM_IMPLEMENTATION
