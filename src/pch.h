#pragma once

#include "config.h"

#include <vector>
#include <bitset>
#include <cstdio>
#include <string>
#include <algorithm>
#include <map>
#include <type_traits>
#include <fstream>
#include <cstddef>
#include <memory>
#include <unordered_map>
#include <cmath>
#include <limits>
#include <stdio.h>
#include <thread>
#include <assert.h>
#include <chrono>
#include <semaphore>

#if defined(OCULUS_PC)
  #define XR_USE_PLATFORM_WIN32
  #define XR_USE_GRAPHICS_API_VULKAN
  #define WIN32_LEAN_AND_MEAN
  #define NOMINMAX

  #include <windows.h>
  #include <unknwn.h>
  #include <timeapi.h>
  #include <atlstr.h>
  #include <vulkan/vulkan.h>
  #include <vulkan/vulkan_win32.h>
  #include <openxr/openxr.h>
  #include <openxr/openxr_platform.h>
  #include <openxr/openxr_reflection.h>
#elif defined(OCULUS_QUEST_2)
  #define XR_USE_PLATFORM_ANDROID
  #define XR_USE_GRAPHICS_API_VULKAN

  #include <android/log.h>
  #include <android_native_app_glue.h>
  #include <jni.h>
  #include <android/native_window.h>
  #include <sys/system_properties.h>
  #include <sys/stat.h>
  #include <android/asset_manager.h>
  #include <unistd.h>
  #include <sched.h>
  #include <vulkan/vulkan.h>
  #include <vulkan/vulkan_android.h>
  #include <openxr/openxr.h>
  #include <openxr/openxr_platform.h>
  #include <openxr/openxr_reflection.h>
#endif
