#ifndef INCLUDE_TOM_ENGINE_PROFILER_H
#define INCLUDE_TOM_ENGINE_PROFILER_H

#include <chrono>
#include "platform.h"

void empty_profiler_function(const char* str="");

struct ScopedTimer {
  const char* name;
  std::chrono::time_point<std::chrono::steady_clock> start_time_point;

  ScopedTimer(const char* p_name);
  ~ScopedTimer();
};

#ifdef USE_PROFILER
  #if defined(OCULUS_PC)
  #include <optick.h>
  #define PROFILE_THREAD OPTICK_THREAD
  #define PROFILE_FUNCTION OPTICK_EVENT
  #define PROFILE_FRAME OPTICK_FRAME
  #define PROFILE_GPU_INIT OPTICK_GPU_INIT_VULKAN
  #define PROFILE_GPU_CONTEXT OPTICK_GPU_CONTEXT
  #define PROFILE_GPU_EVENT OPTICK_GPU_EVENT
  #define PROFILE_GPU_PRESENT OPTICK_GPU_FLIP
  #endif
#else
#define PROFILE_FRAME empty_profiler_function
#define PROFILE_FUNCTION empty_profiler_function
#define PROFILE_THREAD empty_profiler_function
#define PROFILE_GPU_INIT empty_profiler_function
#define PROFILE_GPU_CONTEXT empty_profiler_function
#define PROFILE_GPU_EVENT empty_profiler_function
#define PROFILE_GPU_PRESENT empty_profiler_function
#endif  // USE_PROFILER

#endif  // INCLUDE_TOM_ENGINE_PROFILER_H

//////////////////////////////////////////////////

#ifdef  TOM_ENGINE_PROFILER_IMPLEMENTATION
#ifndef TOM_ENGINE_PROFILER_IMPLEMENTATION_SINGLE
#define TOM_ENGINE_PROFILER_IMPLEMENTATION_SINGLE

void empty_profiler_function(const char* str) {};

ScopedTimer::ScopedTimer(const char* p_name) : name(p_name) {
  this->start_time_point = std::chrono::high_resolution_clock::now();
}

ScopedTimer::~ScopedTimer() {
  const auto end_time_point = std::chrono::high_resolution_clock::now();

  const long long start_time_microseconds = std::chrono::time_point_cast<std::chrono::microseconds>(start_time_point).time_since_epoch().count();
  const long long end_time_microseconds   = std::chrono::time_point_cast<std::chrono::microseconds>(end_time_point).time_since_epoch().count();

  // TODO: implement for release mode
  DEBUG_LOG("%s: %lld microseconds\n", name, end_time_microseconds - start_time_microseconds);
}

#endif  // TOM_ENGINE_PROFILER_IMPLEMENTATION_SINGLE
#endif  // TOM_ENGINE_PROFILER_IMPLEMENTATION
