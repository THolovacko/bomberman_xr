#define XR_USE_PLATFORM_ANDROID
#define XR_USE_GRAPHICS_API_VULKAN

#include <android_native_app_glue.h>
#include <android/native_window.h>
#include <jni.h>
#include <unistd.h>
#include <sys/system_properties.h>
#include <sched.h>
#include <vulkan/vulkan.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <openxr/openxr_reflection.h>
#include <string>
#include <cstdio>
#include <assert.h>
#include <vector>
#include <thread>
#include <chrono>
#include <map>
#include <mutex>
#include <miniaudio.h>
#include <pthread.h>
#include "config.h"
#include "graphics.h"
#include "audio.h"
#include "maths.h"
#include "simulation.h"
#include "tom_std.h"
#include "profiler.h"


struct XrSwapchainContext {
  XrSwapchain                 handle;
  uint32_t                    width;
  uint32_t                    height;
  uint32_t                    image_count;
  XrSwapchainImageBaseHeader* images;
};

struct XrInputState {
  XrActionSet action_set;
  XrAction    pose_action;
  XrAction    select_action;
  XrPath      hand_subaction_paths[2];
  XrPath      gamepad_subaction_path;
  XrSpace     hand_spaces[2];
  XrPosef     hand_poses[2];
  XrBool32    render_hands[2];
  XrBool32    hand_selects[2];
  XrBool32    gamepad_select;
};

struct AndroidUserData {
  ANativeWindow* native_window = nullptr;
  bool is_paused = true;
};

XrInstance                           platform_xr_instance                = {};
XrSystemId                           platform_xr_system_id               = XR_NULL_SYSTEM_ID;
XrSession                            platform_xr_session                 = {};
XrSessionState                       platform_xr_session_state           = XR_SESSION_STATE_UNKNOWN;
XrSpace                              platform_xr_app_space               = {};
XrFormFactor                         platform_xr_form_factor             = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
XrViewConfigurationType              platform_xr_view_configuration_type = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
XrEnvironmentBlendMode               platform_xr_blend_mode              = {XR_ENVIRONMENT_BLEND_MODE_OPAQUE};  // "The Meta Quest OpenXR implementation requires XrFrameEndInfo::environmentBlendMode to be set to XR_ENVIRONMENT_BLEND_MODE_OPAQUE, even when the Passthrough is used" - https://developer.oculus.com/documentation/native/android/mobile-passthrough/
XrDebugUtilsMessengerEXT             platform_xr_debug_ext               = {};
XrPassthroughFB                      platform_xr_passthrough_feature     = XR_NULL_HANDLE;
XrPassthroughLayerFB                 platform_xr_passthrough_layer       = XR_NULL_HANDLE;
bool                                 is_xr_session_active                = false;
const XrPosef                        xr_pose_identity                    = { {0,0,0,1}, {0,0,0} };
std::vector<XrView>                  platform_xr_views;
std::vector<XrViewConfigurationView> platform_xr_config_views;
XrSwapchainContext                   platform_xr_swapchain;
XrInputState                         platform_xr_input;
std::vector<std::thread>             platform_thread_pool;  // one per logical core (size is logical_core_count - 1; minus 1 is for main thread); thread index 0 is auto set to render thread; the thread at audio_thread_pool_index needs to be accessed with ma_audio_device.thread
std::vector<void (*)()>              platform_thread_pool_assigned_function_ptrs;
std::vector<tom::Semaphore*>         platform_thread_pool_semaphores;
std::atomic<bool>                    is_platform_quit_requested(false);
std::atomic<bool>                    set_audio_thread_affinity_mask(false);
tom::Semaphore                       platform_render_semaphore;
tom::Semaphore                       platform_render_thread_sync_semaphore;
XrFrameState                         platform_render_thread_frame_state;
InputState                           input_state;
std::atomic<Vector3f>                hmd_global_position{};
std::atomic<Quaternionf>             hmd_global_orientation{};

std::atomic<void*> android_app_VM;
std::atomic<void*> android_app_activity;
std::map<std::thread::id, pid_t> std_thread_id_to_tid;
std::mutex all_threads_mutex;
AAssetManager* android_asset_manager = nullptr;
AndroidUserData android_app_state = {};
struct android_app* platform_android_app;
cpu_set_t audio_thread_affinity_mask;

extern VkInstance       vulkan_instance;
extern VkPhysicalDevice vulkan_physical_device;
extern VkDevice         vulkan_logical_device;
extern uint32_t         vulkan_queue_family_index;
extern ma_device        ma_audio_device;
extern ma_context       ma_audio_context;

extern void vulkan_allocate_xr_swapchain_images(const uint32_t image_count, const XrSwapchainCreateInfo& swapchain_create_info, XrSwapchainImageBaseHeader*& xr_swapchain_images);
extern int64_t vulkan_select_color_swapchain_format(const std::vector<int64_t>& runtime_formats);
extern void vulkan_render_xr_views(const std::vector<XrCompositionLayerProjectionView>& xr_views, const uint32_t swapchain_image_index);

constexpr uint32_t audio_thread_pool_index = 1;
SimulationState sim_state;
float           delta_time_seconds = 0.0f;

int android_readfn(void* cookie, char* buffer, int size_bytes) {
  return AAsset_read((AAsset*)cookie, buffer, size_bytes);
}

int android_writefn(void* cookie, const char* buffer, int size_bytes) {
  assert(false);  // writing to apk is not allowed
  return -1;
}

int android_closefn(void* cookie) {
  AAsset_close((AAsset*)cookie);
  return 0;
}

fpos_t android_seekfn(void* cookie, fpos_t offset, int whence) {
  return AAsset_seek((AAsset*)cookie, offset, whence);
}

FILE* android_fopen(const char* file_name, const char* mode) {
  assert(mode[0] != 'w'); // writing to apk is not allowed

  AAsset* asset = nullptr;
  if (memcmp(file_name, "assets/", 7) == 0) {
    asset = AAssetManager_open(android_asset_manager, &file_name[7], AASSET_MODE_UNKNOWN);  // 7 is the index after "assets/"
  } else {
    asset = AAssetManager_open(android_asset_manager, file_name, AASSET_MODE_UNKNOWN);
  }
  if(!asset) return NULL;

  return funopen(asset, android_readfn, android_writefn, android_seekfn, android_closefn);
}

std::string xr_result_to_str(XrResult xr_result) {
  switch (xr_result) {
    case XR_SUCCESS:
      return "SUCCESS";
    case XR_TIMEOUT_EXPIRED:
      return "XR_TIMEOUT_EXPIRED";
    case XR_SESSION_LOSS_PENDING:
      return "XR_SESSION_LOSS_PENDING";
    case XR_EVENT_UNAVAILABLE:
      return "XR_EVENT_UNAVAILABLE";
    case XR_SPACE_BOUNDS_UNAVAILABLE:
      return "XR_SPACE_BOUNDS_UNAVAILABLE";
    case XR_SESSION_NOT_FOCUSED:
      return "XR_SESSION_NOT_FOCUSED";
    case XR_FRAME_DISCARDED:
      return "XR_FRAME_DISCARDED";
    case XR_ERROR_VALIDATION_FAILURE:
      return "XR_ERROR_VALIDATION_FAILURE";
    case XR_ERROR_RUNTIME_FAILURE:
      return "XR_ERROR_RUNTIME_FAILURE";
    case XR_ERROR_OUT_OF_MEMORY:
      return "XR_ERROR_OUT_OF_MEMORY";
    case XR_ERROR_API_VERSION_UNSUPPORTED:
      return "XR_ERROR_API_VERSION_UNSUPPORTED";
    case XR_ERROR_INITIALIZATION_FAILED:
      return "XR_ERROR_INITIALIZATION_FAILED";
    case XR_ERROR_FUNCTION_UNSUPPORTED:
      return "XR_ERROR_FUNCTION_UNSUPPORTED";
    case XR_ERROR_FEATURE_UNSUPPORTED:
      return "XR_ERROR_FEATURE_UNSUPPORTED";
    case XR_ERROR_EXTENSION_NOT_PRESENT:
      return "XR_ERROR_EXTENSION_NOT_PRESENT";
    case XR_ERROR_LIMIT_REACHED:
      return "XR_ERROR_LIMIT_REACHED";
    case XR_ERROR_SIZE_INSUFFICIENT:
      return "XR_ERROR_SIZE_INSUFFICIENT";
    case XR_ERROR_HANDLE_INVALID:
      return "XR_ERROR_HANDLE_INVALID";
    case XR_ERROR_INSTANCE_LOST:
      return "XR_ERROR_INSTANCE_LOST";
    case XR_ERROR_SESSION_RUNNING:
      return "XR_ERROR_SESSION_RUNNING";
    case XR_ERROR_SESSION_NOT_RUNNING:
      return "XR_ERROR_SESSION_NOT_RUNNING";
    case XR_ERROR_SESSION_LOST:
      return "XR_ERROR_SESSION_LOST";
    case XR_ERROR_SYSTEM_INVALID:
      return "XR_ERROR_SYSTEM_INVALID";
    case XR_ERROR_PATH_INVALID:
      return "XR_ERROR_PATH_INVALID";
    case XR_ERROR_PATH_COUNT_EXCEEDED:
      return "XR_ERROR_PATH_COUNT_EXCEEDED";
    case XR_ERROR_PATH_FORMAT_INVALID:
      return "XR_ERROR_PATH_FORMAT_INVALID";
    case XR_ERROR_PATH_UNSUPPORTED:
      return "XR_ERROR_PATH_UNSUPPORTED";
    case XR_ERROR_LAYER_INVALID:
      return "XR_ERROR_LAYER_INVALID";
    case XR_ERROR_LAYER_LIMIT_EXCEEDED:
      return "XR_ERROR_LAYER_LIMIT_EXCEEDED";
    case XR_ERROR_SWAPCHAIN_RECT_INVALID:
      return "XR_ERROR_SWAPCHAIN_RECT_INVALID";
    case XR_ERROR_SWAPCHAIN_FORMAT_UNSUPPORTED:
      return "XR_ERROR_SWAPCHAIN_FORMAT_UNSUPPORTED";
    case XR_ERROR_ACTION_TYPE_MISMATCH:
      return "XR_ERROR_ACTION_TYPE_MISMATCH";
    case XR_ERROR_SESSION_NOT_READY:
      return "XR_ERROR_SESSION_NOT_READY";
    case XR_ERROR_SESSION_NOT_STOPPING:
      return "XR_ERROR_SESSION_NOT_STOPPING";
    case XR_ERROR_TIME_INVALID:
      return "XR_ERROR_TIME_INVALID";
    case XR_ERROR_REFERENCE_SPACE_UNSUPPORTED:
      return "XR_ERROR_REFERENCE_SPACE_UNSUPPORTED";
    case XR_ERROR_FILE_ACCESS_ERROR:
      return "XR_ERROR_FILE_ACCESS_ERROR";
    case XR_ERROR_FILE_CONTENTS_INVALID:
      return "XR_ERROR_FILE_CONTENTS_INVALID";
    case XR_ERROR_FORM_FACTOR_UNSUPPORTED:
      return "XR_ERROR_FORM_FACTOR_UNSUPPORTED";
    case XR_ERROR_FORM_FACTOR_UNAVAILABLE:
      return "XR_ERROR_FORM_FACTOR_UNAVAILABLE";
    case XR_ERROR_API_LAYER_NOT_PRESENT:
      return "XR_ERROR_API_LAYER_NOT_PRESENT";
    case XR_ERROR_CALL_ORDER_INVALID:
      return "XR_ERROR_CALL_ORDER_INVALID";
    case XR_ERROR_GRAPHICS_DEVICE_INVALID:
      return "XR_ERROR_GRAPHICS_DEVICE_INVALID";
    case XR_ERROR_POSE_INVALID:
      return "XR_ERROR_POSE_INVALID";
    case XR_ERROR_INDEX_OUT_OF_RANGE:
      return "XR_ERROR_INDEX_OUT_OF_RANGE";
    case XR_ERROR_VIEW_CONFIGURATION_TYPE_UNSUPPORTED:
      return "XR_ERROR_VIEW_CONFIGURATION_TYPE_UNSUPPORTED";
    case XR_ERROR_ENVIRONMENT_BLEND_MODE_UNSUPPORTED:
      return "XR_ERROR_ENVIRONMENT_BLEND_MODE_UNSUPPORTED";
    case XR_ERROR_NAME_DUPLICATED:
      return "XR_ERROR_NAME_DUPLICATED";
    case XR_ERROR_NAME_INVALID:
      return "XR_ERROR_NAME_INVALID";
    case XR_ERROR_ACTIONSET_NOT_ATTACHED:
      return "XR_ERROR_ACTIONSET_NOT_ATTACHED";
    case XR_ERROR_ACTIONSETS_ALREADY_ATTACHED:
      return "XR_ERROR_ACTIONSETS_ALREADY_ATTACHED";
    case XR_ERROR_LOCALIZED_NAME_DUPLICATED:
      return "XR_ERROR_LOCALIZED_NAME_DUPLICATED";
    case XR_ERROR_LOCALIZED_NAME_INVALID:
      return "XR_ERROR_LOCALIZED_NAME_INVALID";
    case XR_ERROR_GRAPHICS_REQUIREMENTS_CALL_MISSING:
      return "XR_ERROR_GRAPHICS_REQUIREMENTS_CALL_MISSING";
    case XR_ERROR_RUNTIME_UNAVAILABLE:
      return "XR_ERROR_RUNTIME_UNAVAILABLE";
    case XR_ERROR_ANDROID_THREAD_SETTINGS_ID_INVALID_KHR:
      return "XR_ERROR_ANDROID_THREAD_SETTINGS_ID_INVALID_KHR";
    case XR_ERROR_ANDROID_THREAD_SETTINGS_FAILURE_KHR:
      return "XR_ERROR_ANDROID_THREAD_SETTINGS_FAILURE_KHR";
    case XR_ERROR_CREATE_SPATIAL_ANCHOR_FAILED_MSFT:
      return "XR_ERROR_CREATE_SPATIAL_ANCHOR_FAILED_MSFT";
    case XR_ERROR_SECONDARY_VIEW_CONFIGURATION_TYPE_NOT_ENABLED_MSFT:
      return "XR_ERROR_SECONDARY_VIEW_CONFIGURATION_TYPE_NOT_ENABLED_MSFT";
    case XR_ERROR_CONTROLLER_MODEL_KEY_INVALID_MSFT:
      return "XR_ERROR_CONTROLLER_MODEL_KEY_INVALID_MSFT";
    case XR_ERROR_REPROJECTION_MODE_UNSUPPORTED_MSFT:
      return "XR_ERROR_REPROJECTION_MODE_UNSUPPORTED_MSFT";
    case XR_ERROR_COMPUTE_NEW_SCENE_NOT_COMPLETED_MSFT:
      return "XR_ERROR_COMPUTE_NEW_SCENE_NOT_COMPLETED_MSFT";
    case XR_ERROR_SCENE_COMPONENT_ID_INVALID_MSFT:
      return "XR_ERROR_SCENE_COMPONENT_ID_INVALID_MSFT";
    case XR_ERROR_SCENE_COMPONENT_TYPE_MISMATCH_MSFT:
      return "XR_ERROR_SCENE_COMPONENT_TYPE_MISMATCH_MSFT";
    case XR_ERROR_SCENE_MESH_BUFFER_ID_INVALID_MSFT:
      return "XR_ERROR_SCENE_MESH_BUFFER_ID_INVALID_MSFT";
    case XR_ERROR_SCENE_COMPUTE_FEATURE_INCOMPATIBLE_MSFT:
      return "XR_ERROR_SCENE_COMPUTE_FEATURE_INCOMPATIBLE_MSFT";
    case XR_ERROR_SCENE_COMPUTE_CONSISTENCY_MISMATCH_MSFT:
      return "XR_ERROR_SCENE_COMPUTE_CONSISTENCY_MISMATCH_MSFT";
    case XR_ERROR_DISPLAY_REFRESH_RATE_UNSUPPORTED_FB:
      return "XR_ERROR_DISPLAY_REFRESH_RATE_UNSUPPORTED_FB";
    case XR_ERROR_COLOR_SPACE_UNSUPPORTED_FB:
      return "XR_ERROR_COLOR_SPACE_UNSUPPORTED_FB";
    case XR_ERROR_UNEXPECTED_STATE_PASSTHROUGH_FB:
      return "XR_ERROR_UNEXPECTED_STATE_PASSTHROUGH_FB";
    case XR_ERROR_FEATURE_ALREADY_CREATED_PASSTHROUGH_FB:
      return "XR_ERROR_FEATURE_ALREADY_CREATED_PASSTHROUGH_FB";
    case XR_ERROR_FEATURE_REQUIRED_PASSTHROUGH_FB:
      return "XR_ERROR_FEATURE_REQUIRED_PASSTHROUGH_FB";
    case XR_ERROR_NOT_PERMITTED_PASSTHROUGH_FB:
      return "XR_ERROR_NOT_PERMITTED_PASSTHROUGH_FB";
    case XR_ERROR_INSUFFICIENT_RESOURCES_PASSTHROUGH_FB:
      return "XR_ERROR_INSUFFICIENT_RESOURCES_PASSTHROUGH_FB";
    case XR_ERROR_UNKNOWN_PASSTHROUGH_FB:
      return "XR_ERROR_UNKNOWN_PASSTHROUGH_FB";
    case XR_ERROR_MARKER_NOT_TRACKED_VARJO:
      return "XR_ERROR_MARKER_NOT_TRACKED_VARJO";
    case XR_ERROR_MARKER_ID_INVALID_VARJO:
      return "XR_ERROR_MARKER_ID_INVALID_VARJO";
    case XR_ERROR_SPATIAL_ANCHOR_NAME_NOT_FOUND_MSFT:
      return "XR_ERROR_SPATIAL_ANCHOR_NAME_NOT_FOUND_MSFT";
    case XR_ERROR_SPATIAL_ANCHOR_NAME_INVALID_MSFT:
      return "XR_ERROR_SPATIAL_ANCHOR_NAME_INVALID_MSFT";
    case XR_RESULT_MAX_ENUM:
      return "XR_RESULT_MAX_ENUM";
    default:
      return "did not find xr_result type";
  }
}

bool check_xr_result(XrResult xr_result) {
  return (xr_result == XR_SUCCESS) ? true : false;
}

uint32_t get_thread_pool_size() {
  return (uint32_t)platform_thread_pool.size();
}

void platform_request_exit() {
  if (!is_platform_quit_requested) xrRequestExitSession(platform_xr_session);
}

void default_thread(const uint32_t thread_pool_index) {
  all_threads_mutex.lock();
  JNIEnv* jni_env;
  platform_android_app->activity->vm->AttachCurrentThread(&jni_env, nullptr);  // attaching to the JavaVM so this thread can make JNI calls
  std_thread_id_to_tid[std::this_thread::get_id()] = gettid();
  all_threads_mutex.unlock();

  platform_thread_pool_semaphores[thread_pool_index]->wait();
  auto assigned_function_ptr = platform_thread_pool_assigned_function_ptrs[thread_pool_index];
  assigned_function_ptr();

  all_threads_mutex.lock();
  platform_android_app->activity->vm->DetachCurrentThread();
  all_threads_mutex.unlock();

  DEBUG_LOG("Ending thread: %d\n", thread_pool_index);
}

void init_thread(void (*function_ptr)(), const uint32_t thread_pool_index) {
  platform_thread_pool_assigned_function_ptrs[thread_pool_index] = function_ptr;
  platform_thread_pool_semaphores[thread_pool_index]->signal();
}

bool init_platform() {
  {
    XrLoaderInitInfoAndroidKHR loader_init_info = {XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR};
    loader_init_info.applicationVM      = android_app_VM;
    loader_init_info.applicationContext = android_app_activity;

    PFN_xrInitializeLoaderKHR xrInitializeLoaderKHR = nullptr;
    xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrInitializeLoaderKHR", (PFN_xrVoidFunction*)(&xrInitializeLoaderKHR));
    xrInitializeLoaderKHR((const XrLoaderInitInfoBaseHeaderKHR*)&loader_init_info);
  }

  {
    std::vector<const char*> active_extensions;
    const char *request_extensions[] = {
      "XR_KHR_vulkan_enable2",
      "XR_KHR_loader_init",
      "XR_KHR_loader_init_android",
      "XR_KHR_android_create_instance",
      "XR_KHR_android_thread_settings",
      "XR_KHR_swapchain_usage_input_attachment_bit",
      "XR_KHR_visibility_mask",
      "XR_KHR_vulkan_swapchain_format_list",
      "XR_KHR_android_surface_swapchain",
      "XR_FB_android_surface_swapchain_create",
      "XR_META_vulkan_swapchain_create_info",
      "XR_FB_display_refresh_rate",
      "XR_FB_passthrough",
      "XR_FB_triangle_mesh",
      //"XR_OCULUS_android_session_state_enable",
      #ifndef NDEBUG
      "XR_EXT_debug_utils",
      #endif
    };

    uint32_t extension_count = 0;
    xrEnumerateInstanceExtensionProperties(nullptr, 0, &extension_count, nullptr);
    std::vector<XrExtensionProperties> xr_extensions(extension_count, {XR_TYPE_EXTENSION_PROPERTIES});
    xrEnumerateInstanceExtensionProperties(nullptr, extension_count, &extension_count, xr_extensions.data());

    bool is_vulkan_available = false;
    for (size_t i=0; i < xr_extensions.size(); ++i) {
      if ( strcmp(xr_extensions[i].extensionName, "XR_KHR_vulkan_enable2") == 0 ) {
        is_vulkan_available = true;
      }

      for (int32_t request_extension_index=0; request_extension_index < std::size(request_extensions); ++request_extension_index) {
        if (strcmp(request_extensions[request_extension_index], xr_extensions[i].extensionName) == 0) {
          active_extensions.push_back(request_extensions[request_extension_index]);
          break;
        }
      }
    }

    if (!is_vulkan_available) return false;

    std::vector<const char*> active_layers;
    #ifndef NDEBUG
    const char *request_layers[] = {
      "",
    };

    uint32_t layer_count = 0;
    xrEnumerateApiLayerProperties(0, &layer_count, nullptr);
    std::vector<XrApiLayerProperties> xr_layers(layer_count, {XR_TYPE_API_LAYER_PROPERTIES});
    xrEnumerateApiLayerProperties(layer_count, &layer_count, xr_layers.data());

    for (size_t i=0; i < xr_layers.size(); ++i) {
      for (int32_t request_layer_index=0; request_layer_index < std::size(request_layers); ++request_layer_index) {
        DEBUG_LOG("found API layer: %s\n", xr_layers[i].layerName);
        if (strcmp(request_layers[request_layer_index], xr_layers[i].layerName) == 0) {
          active_layers.push_back(request_layers[request_layer_index]);
          break;
        }
      }
    }
    #endif

    XrInstanceCreateInfoAndroidKHR create_info_android = {XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR};
    create_info_android.applicationVM       = android_app_VM;
    create_info_android.applicationActivity = android_app_activity;

    XrInstanceCreateInfo create_info               = {XR_TYPE_INSTANCE_CREATE_INFO};
    create_info.next                               = (XrBaseInStructure*)&create_info_android;
    create_info.enabledExtensionCount              = static_cast<uint32_t>(active_extensions.size());
    create_info.enabledExtensionNames              = active_extensions.data();
    create_info.enabledApiLayerCount               = static_cast<uint32_t>(active_layers.size());
    create_info.enabledApiLayerNames               = active_layers.data();
    create_info.applicationInfo.apiVersion         = XR_CURRENT_API_VERSION;
    create_info.applicationInfo.applicationVersion = APPLICATION_VERSION;
    create_info.applicationInfo.engineVersion      = ENGINE_VERSION;
    strcpy(create_info.applicationInfo.applicationName, APPLICATION_NAME);
    strcpy(create_info.applicationInfo.engineName, ENGINE_NAME);
    xrCreateInstance(&create_info, &platform_xr_instance); 
    if (platform_xr_instance == nullptr) return false;
  }

#ifndef NDEBUG
  {
    PFN_xrCreateDebugUtilsMessengerEXT  xrCreateDebugUtilsMessengerEXT = nullptr;
    xrGetInstanceProcAddr(platform_xr_instance, "xrCreateDebugUtilsMessengerEXT", (PFN_xrVoidFunction *)(&xrCreateDebugUtilsMessengerEXT));
    XrDebugUtilsMessengerCreateInfoEXT debug_info{XR_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
    debug_info.messageTypes      = XR_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | XR_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | XR_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT | XR_DEBUG_UTILS_MESSAGE_TYPE_CONFORMANCE_BIT_EXT;
    debug_info.messageSeverities = XR_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | XR_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT | XR_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | XR_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    debug_info.userCallback = [](XrDebugUtilsMessageSeverityFlagsEXT severity, XrDebugUtilsMessageTypeFlagsEXT types, const XrDebugUtilsMessengerCallbackDataEXT *msg, void* user_data) {
      DEBUG_LOG("%s: %s\n", msg->functionName, msg->message);
      return static_cast<XrBool32>(XR_FALSE);
    };

    platform_xr_debug_ext = {};
    if (xrCreateDebugUtilsMessengerEXT) xrCreateDebugUtilsMessengerEXT(platform_xr_instance, &debug_info, &platform_xr_debug_ext);
  }
  #endif

  XrResult current_xr_result;
  {
    XrSystemPassthroughPropertiesFB passthrough_system_properties{XR_TYPE_SYSTEM_PASSTHROUGH_PROPERTIES_FB};
    XrSystemProperties system_properties{XR_TYPE_SYSTEM_PROPERTIES, &passthrough_system_properties};

    XrSystemGetInfo system_info = {XR_TYPE_SYSTEM_GET_INFO};
    platform_xr_system_id       = XR_NULL_SYSTEM_ID;
    system_info.formFactor      = platform_xr_form_factor;
    current_xr_result           = xrGetSystem(platform_xr_instance, &system_info, &platform_xr_system_id);
    assert(check_xr_result(current_xr_result)); // make sure headset is on

    xrGetSystemProperties(platform_xr_instance, platform_xr_system_id, &system_properties);
    assert(passthrough_system_properties.supportsPassthrough != XR_FALSE);

    PFN_xrGetVulkanGraphicsRequirements2KHR xrGetVulkanGraphicsRequirements2KHR = nullptr;
    xrGetInstanceProcAddr(platform_xr_instance, "xrGetVulkanGraphicsRequirements2KHR", (PFN_xrVoidFunction *)(&xrGetVulkanGraphicsRequirements2KHR));
    XrGraphicsRequirementsVulkan2KHR xr_graphics_requirements{XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN2_KHR};
    xrGetVulkanGraphicsRequirements2KHR(platform_xr_instance, platform_xr_system_id, &xr_graphics_requirements);

    #ifndef NDEBUG
    DEBUG_LOG("Min Vulkan version: %" PRIu64 " Max Vulkan version:%" PRIu64 "\n", xr_graphics_requirements.minApiVersionSupported, xr_graphics_requirements.maxApiVersionSupported);
    #endif

    if ( !init_graphics() ) return false;

    XrGraphicsBindingVulkan2KHR xr_graphics_binding{XR_TYPE_GRAPHICS_BINDING_VULKAN2_KHR};
    xr_graphics_binding.instance         = vulkan_instance;
    xr_graphics_binding.physicalDevice   = vulkan_physical_device;
    xr_graphics_binding.device           = vulkan_logical_device;
    xr_graphics_binding.queueFamilyIndex = vulkan_queue_family_index;
    xr_graphics_binding.queueIndex       = 0;

    XrSessionCreateInfo session_info = {XR_TYPE_SESSION_CREATE_INFO};
    session_info.next                = &xr_graphics_binding;
    session_info.systemId            = platform_xr_system_id;
    current_xr_result = xrCreateSession(platform_xr_instance, &session_info, &platform_xr_session);
    assert(check_xr_result(current_xr_result));

    if (platform_xr_session == nullptr) return false;

    XrReferenceSpaceCreateInfo ref_space_info = {XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    ref_space_info.poseInReferenceSpace       = xr_pose_identity;
    ref_space_info.referenceSpaceType         = XR_REFERENCE_SPACE_TYPE_STAGE;
    xrCreateReferenceSpace(platform_xr_session, &ref_space_info, &platform_xr_app_space);

    uint32_t view_count = 0;
    xrEnumerateViewConfigurationViews(platform_xr_instance, platform_xr_system_id, platform_xr_view_configuration_type, 0, &view_count, nullptr);
    platform_xr_config_views.resize(view_count, {XR_TYPE_VIEW_CONFIGURATION_VIEW});
    platform_xr_views.resize(view_count, {XR_TYPE_VIEW});
    xrEnumerateViewConfigurationViews(platform_xr_instance, platform_xr_system_id, platform_xr_view_configuration_type, view_count, &view_count, platform_xr_config_views.data());
    assert(view_count == 2);  // one view per eye

    XrPassthroughCreateInfoFB passthrough_info{XR_TYPE_PASSTHROUGH_CREATE_INFO_FB};
    passthrough_info.next  = nullptr;
    passthrough_info.flags = XR_PASSTHROUGH_IS_RUNNING_AT_CREATION_BIT_FB;

    PFN_xrCreatePassthroughFB xrCreatePassthroughFB = nullptr;
    current_xr_result = xrGetInstanceProcAddr(platform_xr_instance, "xrCreatePassthroughFB", (PFN_xrVoidFunction*)(&xrCreatePassthroughFB));
    current_xr_result = xrCreatePassthroughFB(platform_xr_session, &passthrough_info, &platform_xr_passthrough_feature);

    XrPassthroughLayerCreateInfoFB passthrough_layer_info{XR_TYPE_PASSTHROUGH_LAYER_CREATE_INFO_FB};
    passthrough_layer_info.next        = nullptr;
    passthrough_layer_info.passthrough = platform_xr_passthrough_feature;
    passthrough_layer_info.purpose     = XR_PASSTHROUGH_LAYER_PURPOSE_RECONSTRUCTION_FB;
    passthrough_layer_info.flags       = XR_PASSTHROUGH_IS_RUNNING_AT_CREATION_BIT_FB;

    PFN_xrCreatePassthroughLayerFB xrCreatePassthroughLayerFB = nullptr;
    current_xr_result = xrGetInstanceProcAddr(platform_xr_instance, "xrCreatePassthroughLayerFB", (PFN_xrVoidFunction*)(&xrCreatePassthroughLayerFB));
    current_xr_result = xrCreatePassthroughLayerFB(platform_xr_session, &passthrough_layer_info, &platform_xr_passthrough_layer);

    uint32_t swapchain_format_count;
    xrEnumerateSwapchainFormats(platform_xr_session, 0, &swapchain_format_count, nullptr);
    std::vector<int64_t> swapchain_formats(swapchain_format_count);
    xrEnumerateSwapchainFormats(platform_xr_session, (uint32_t)swapchain_formats.size(), &swapchain_format_count, swapchain_formats.data());
    assert(swapchain_format_count == swapchain_formats.size());
    uint64_t swapchain_format = vulkan_select_color_swapchain_format(swapchain_formats);

    const XrViewConfigurationView& view = platform_xr_config_views[0];

    XrSwapchainCreateInfo swapchain_info = {XR_TYPE_SWAPCHAIN_CREATE_INFO};
    swapchain_info.arraySize   = 2;
    swapchain_info.mipCount    = 1;
    swapchain_info.faceCount   = 1;
    swapchain_info.format      = swapchain_format;
    swapchain_info.width       = view.recommendedImageRectWidth;
    swapchain_info.height      = view.recommendedImageRectHeight;
    swapchain_info.sampleCount = 1; // xr swapchain image is a resolve attachment
    swapchain_info.usageFlags  = XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;

    xrCreateSwapchain(platform_xr_session, &swapchain_info, &platform_xr_swapchain.handle);

    platform_xr_swapchain.width  = swapchain_info.width;
    platform_xr_swapchain.height = swapchain_info.height;

    xrEnumerateSwapchainImages(platform_xr_swapchain.handle, 0, &platform_xr_swapchain.image_count, nullptr);
    vulkan_allocate_xr_swapchain_images(platform_xr_swapchain.image_count, swapchain_info, platform_xr_swapchain.images);
    xrEnumerateSwapchainImages(platform_xr_swapchain.handle, platform_xr_swapchain.image_count, &platform_xr_swapchain.image_count, platform_xr_swapchain.images);
  }

  {
    /* OpenXR input overview (https://developer.oculus.com/documentation/native/android/mobile-openxr-input/)
       XrAction                             -> user intent
       XrActionSet                          -> context of actions
       XrInteractionProfileSuggestedBinding -> bindings for actions to physical input for devices
        action paths are defined per interaction profile ("/interaction_profiles/khr/simple_controller" defines the action path "/user/hand/left/input/grip/pose")
        https://registry.khronos.org/OpenXR/specs/1.0/html/xrspec.html#semantic-path-interaction-profiles

       you must call xrSuggestInteractionProfileBindings for each interaction profile that you intend to support
    */

    XrActionSetCreateInfo action_set_info = {XR_TYPE_ACTION_SET_CREATE_INFO};
    strcpy(action_set_info.actionSetName,          "gameplay");
    strcpy(action_set_info.localizedActionSetName, "Gameplay");
    current_xr_result = xrCreateActionSet(platform_xr_instance, &action_set_info, &platform_xr_input.action_set);

    xrStringToPath(platform_xr_instance, "/user/hand/left", &platform_xr_input.hand_subaction_paths[0]);
    xrStringToPath(platform_xr_instance, "/user/hand/right", &platform_xr_input.hand_subaction_paths[1]);
    xrStringToPath(platform_xr_instance, "/user/gamepad", &platform_xr_input.gamepad_subaction_path);
    XrPath all_subaction_paths[] = { platform_xr_input.hand_subaction_paths[0], platform_xr_input.hand_subaction_paths[1], platform_xr_input.gamepad_subaction_path };

    XrActionCreateInfo action_info  = {XR_TYPE_ACTION_CREATE_INFO};
    action_info.countSubactionPaths = static_cast<uint32_t>( std::size(platform_xr_input.hand_subaction_paths) );
    action_info.subactionPaths      = platform_xr_input.hand_subaction_paths;
    action_info.actionType          = XR_ACTION_TYPE_POSE_INPUT;
    strcpy(action_info.actionName,          "hand_pose");
    strcpy(action_info.localizedActionName, "Hand Pose");
    current_xr_result = xrCreateAction(platform_xr_input.action_set, &action_info, &platform_xr_input.pose_action);

    action_info.countSubactionPaths = static_cast<uint32_t>( std::size(all_subaction_paths) );
    action_info.subactionPaths      = all_subaction_paths;
    action_info.actionType          = XR_ACTION_TYPE_BOOLEAN_INPUT;
    strcpy(action_info.actionName,          "select");
    strcpy(action_info.localizedActionName, "Select");
    current_xr_result = xrCreateAction(platform_xr_input.action_set, &action_info, &platform_xr_input.select_action);

    for (int32_t i=0; i < 2; ++i) {
      XrActionSpaceCreateInfo action_space_info = {XR_TYPE_ACTION_SPACE_CREATE_INFO};
      action_space_info.action            = platform_xr_input.pose_action;
      action_space_info.poseInActionSpace = xr_pose_identity;
      action_space_info.subactionPath     = platform_xr_input.hand_subaction_paths[i];
      xrCreateActionSpace(platform_xr_session, &action_space_info, &platform_xr_input.hand_spaces[i]);
    }

    /* Oculus Touch Controller Interaction Profile */
    {
      XrPath profile_path;
      XrPath pose_paths[2];
      XrPath select_paths[2];
      xrStringToPath(platform_xr_instance, "/user/hand/left/input/grip/pose",     &pose_paths[0]);
      xrStringToPath(platform_xr_instance, "/user/hand/right/input/grip/pose",    &pose_paths[1]);
      xrStringToPath(platform_xr_instance, "/user/hand/left/input/x/click",       &select_paths[0]);
      xrStringToPath(platform_xr_instance, "/user/hand/right/input/a/click",      &select_paths[1]);
      xrStringToPath(platform_xr_instance, "/interaction_profiles/oculus/touch_controller", &profile_path);
      XrActionSuggestedBinding action_bindings[] = {
        { platform_xr_input.pose_action,   pose_paths[0]   },
        { platform_xr_input.pose_action,   pose_paths[1]   },
        { platform_xr_input.select_action, select_paths[0] },
        { platform_xr_input.select_action, select_paths[1] }
      };
      XrInteractionProfileSuggestedBinding profile_bindings = {XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
      profile_bindings.interactionProfile     = profile_path;
      profile_bindings.suggestedBindings      = &action_bindings[0];
      profile_bindings.countSuggestedBindings = static_cast<uint32_t>( std::size(action_bindings) );
      current_xr_result = xrSuggestInteractionProfileBindings(platform_xr_instance, &profile_bindings);
    }

    /* Xbox Controller Interaction Profile */
    {
      XrPath profile_path;
      XrPath select_path;
      xrStringToPath(platform_xr_instance, "/user/gamepad/input/a/click", &select_path);
      xrStringToPath(platform_xr_instance, "/interaction_profiles/microsoft/xbox_controller", &profile_path);
      XrActionSuggestedBinding action_bindings[] = {
        { platform_xr_input.select_action, select_path }
      };
      XrInteractionProfileSuggestedBinding profile_bindings = {XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
      profile_bindings.interactionProfile     = profile_path;
      profile_bindings.suggestedBindings      = &action_bindings[0];
      profile_bindings.countSuggestedBindings = static_cast<uint32_t>( std::size(action_bindings) );
      current_xr_result = xrSuggestInteractionProfileBindings(platform_xr_instance, &profile_bindings);
    }

    XrSessionActionSetsAttachInfo attach_info = {XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
    attach_info.countActionSets = 1;
    attach_info.actionSets      = &platform_xr_input.action_set;
    current_xr_result = xrAttachSessionActionSets(platform_xr_session, &attach_info);
  }

  if ( !init_audio() ) return false;

  uint32_t logical_processor_count = static_cast<uint32_t>(sysconf(_SC_NPROCESSORS_ONLN));  //static_cast<uint32_t>( get_nprocs() );
  assert(logical_processor_count >= 3);

  const uint32_t thread_pool_size = logical_processor_count - 1;  // minus 1 to exclude main thread
  platform_thread_pool_assigned_function_ptrs.resize(thread_pool_size);
  platform_thread_pool_semaphores.resize(thread_pool_size);
  for (auto& Semaphore : platform_thread_pool_semaphores) {
    Semaphore = new tom::Semaphore;
  }

  cpu_set_t affinity_mask;
  CPU_ZERO(&affinity_mask);
  sched_getaffinity(getpid(), sizeof(affinity_mask), &affinity_mask); // process id is same as main thread id
  assert(CPU_COUNT_S(sizeof(affinity_mask), &affinity_mask) > 0);
  uint32_t main_thread_processor_number;
  for (uint32_t i=0; i < logical_processor_count; ++i) {
    if (CPU_ISSET(i, &affinity_mask)) {
      main_thread_processor_number = i;
      break;
    }
  }
  CPU_ZERO(&affinity_mask);
  CPU_SET(main_thread_processor_number, &affinity_mask);
  sched_setaffinity(getpid(), sizeof(affinity_mask), &affinity_mask); // process id is same as main thread id

  uint32_t thread_pool_index = 0;
  for (uint32_t i=0; i < logical_processor_count; ++i) {
    if (i != main_thread_processor_number) {
      platform_thread_pool.push_back(std::thread(default_thread,thread_pool_index));
      CPU_ZERO(&affinity_mask);
      CPU_SET(i, &affinity_mask);

      all_threads_mutex.lock();
      pid_t tid = std_thread_id_to_tid[platform_thread_pool.back().get_id()];
      all_threads_mutex.unlock();

      sched_setaffinity(tid, sizeof(affinity_mask), &affinity_mask);
      ++thread_pool_index;
    }
  }

  /* TODO: XR_ANDROID_THREAD_TYPE_APPLICATION_WORKER_KHR and XR_ANDROID_THREAD_TYPE_RENDERER_WORKER_KHR return
   * XR_ERROR_ANDROID_THREAD_SETTINGS_FAILURE_KHR. I tried first using XR_ANDROID_THREAD_TYPE_APPLICATION_MAIN_KHR then
   * XR_ANDROID_THREAD_TYPE_APPLICATION_WORKER_KHR for audio thread but still get same error. Logs say the thread type is unsupported.
   * The spec says MAIN thread types are for background tasks and WORKER thread types are for "time critical device tasks". I would
   * like my main cpu thread, main render thread, and audio thread to all be "time critical".
   */

  PFN_xrSetAndroidApplicationThreadKHR xrSetAndroidApplicationThreadKHR = nullptr;
  xrGetInstanceProcAddr(platform_xr_instance, "xrSetAndroidApplicationThreadKHR", (PFN_xrVoidFunction *)(&xrSetAndroidApplicationThreadKHR));
  xrSetAndroidApplicationThreadKHR(platform_xr_session, XR_ANDROID_THREAD_TYPE_APPLICATION_MAIN_KHR, gettid());

  graphics_render_thread_sim_state.mesh_instances = *( (GraphicsMeshInstances*) malloc(sizeof(GraphicsMeshInstances)) );
  platform_render_thread_frame_state.predictedDisplayTime = (XrTime)0;

  PFN_xrRequestDisplayRefreshRateFB xrRequestDisplayRefreshRateFB = nullptr;
  xrGetInstanceProcAddr(platform_xr_instance, "xrRequestDisplayRefreshRateFB", (PFN_xrVoidFunction *)(&xrRequestDisplayRefreshRateFB));
  xrRequestDisplayRefreshRateFB(platform_xr_session, 90.0f);

  return true;
}

void deactivate_platform() {
  platform_render_semaphore.signal();  // make sure render thread isn't waiting
  platform_thread_pool[0].join();
  for (uint32_t i=1; i < platform_thread_pool.size(); ++i) {
    init_thread([]{ /* empty function */ }, i);
    platform_thread_pool[i].join();
  }

  deactivate_audio();

  xrDestroySwapchain(platform_xr_swapchain.handle);

  if (platform_xr_input.hand_spaces[0] != XR_NULL_HANDLE) xrDestroySpace(platform_xr_input.hand_spaces[0]);
  if (platform_xr_input.hand_spaces[1] != XR_NULL_HANDLE) xrDestroySpace(platform_xr_input.hand_spaces[1]);
  xrDestroyActionSet(platform_xr_input.action_set);

  xrDestroySpace(platform_xr_app_space);
  xrDestroySession(platform_xr_session);
  #ifndef NDEBUG
  PFN_xrDestroyDebugUtilsMessengerEXT xrDestroyDebugUtilsMessengerEXT = nullptr;
  xrGetInstanceProcAddr(platform_xr_instance, "xrDestroyDebugUtilsMessengerEXT", (PFN_xrVoidFunction *)(&xrDestroyDebugUtilsMessengerEXT));
  if (platform_xr_debug_ext != XR_NULL_HANDLE) xrDestroyDebugUtilsMessengerEXT(platform_xr_debug_ext);
  #endif
  xrDestroyInstance(platform_xr_instance);

  deactivate_graphics();
}

bool poll_platform_events() {
  PROFILE_FUNCTION();
  is_platform_quit_requested = false;

  if (platform_android_app->destroyRequested != 0) {
    is_platform_quit_requested = true;
    return !is_platform_quit_requested;
  }

  while (true) {
    int pending_events;
    struct android_poll_source* poll_source;

    const int timeout_milliseconds = (android_app_state.is_paused && !is_xr_session_active && (platform_android_app->destroyRequested == 0)) ? -1 : 0;  // "If the timeout is zero, returns immediately without blocking. If the timeout is negative, waits indefinitely until an event appears" - https://developer.android.com/ndk/reference/group/looper#alooper_pollall
    int result = ALooper_pollAll(timeout_milliseconds, nullptr, &pending_events, (void**)&poll_source);
    if (result < 0) break;

    if (poll_source) poll_source->process(platform_android_app, poll_source);
  }

  XrEventDataBuffer event_buffer = {XR_TYPE_EVENT_DATA_BUFFER};
  while (xrPollEvent(platform_xr_instance, &event_buffer) == XR_SUCCESS) {
    switch (event_buffer.type) {
      case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
        XrEventDataSessionStateChanged* state_changed_event = (XrEventDataSessionStateChanged*)&event_buffer;
        platform_xr_session_state = state_changed_event->state;
      
        switch (platform_xr_session_state) {
          case XR_SESSION_STATE_READY: {
            XrSessionBeginInfo begin_session_info = {XR_TYPE_SESSION_BEGIN_INFO};
            begin_session_info.primaryViewConfigurationType = platform_xr_view_configuration_type;
            xrBeginSession(platform_xr_session, &begin_session_info);
            is_xr_session_active = true;
            platform_render_thread_sync_semaphore.signal();
            play_audio_device();
          } break;
          case XR_SESSION_STATE_STOPPING:
            is_xr_session_active = false;
            stop_audio_device();
            platform_render_thread_sync_semaphore.wait();
            xrEndSession(platform_xr_session);
            break;
          case XR_SESSION_STATE_EXITING:
            is_platform_quit_requested = true;
            break;
          case XR_SESSION_STATE_LOSS_PENDING:
            is_platform_quit_requested = true;
            break;
        }
      } break;
      case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
        is_platform_quit_requested = true;
        return !is_platform_quit_requested;
    }

    event_buffer = {XR_TYPE_EVENT_DATA_BUFFER};
  }

  if (is_platform_quit_requested) return !is_platform_quit_requested;

  if (!is_xr_session_active) {
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    poll_platform_events();
  }

  if (platform_xr_session_state != XR_SESSION_STATE_FOCUSED) return !is_platform_quit_requested;

  XrActiveActionSet action_set = {};
  action_set.actionSet     = platform_xr_input.action_set;
  action_set.subactionPath = XR_NULL_PATH;

  XrActionsSyncInfo sync_info = {XR_TYPE_ACTIONS_SYNC_INFO};
  sync_info.countActiveActionSets = 1;
  sync_info.activeActionSets      = &action_set;

  xrSyncActions(platform_xr_session, &sync_info);

  for (uint32_t hand=0; hand < 2; ++hand) {
    XrActionStateGetInfo get_info = {XR_TYPE_ACTION_STATE_GET_INFO};
    get_info.subactionPath = platform_xr_input.hand_subaction_paths[hand];

    XrActionStatePose pose_state = {XR_TYPE_ACTION_STATE_POSE};
    get_info.action = platform_xr_input.pose_action;
    xrGetActionStatePose(platform_xr_session, &get_info, &pose_state);
    platform_xr_input.render_hands[hand] = pose_state.isActive;

    XrActionStateBoolean select_state = {XR_TYPE_ACTION_STATE_BOOLEAN};
    get_info.action = platform_xr_input.select_action;
    xrGetActionStateBoolean(platform_xr_session, &get_info, &select_state);
    platform_xr_input.hand_selects[hand] = select_state.currentState && select_state.changedSinceLastSync;

    if (platform_xr_input.render_hands[hand]) {
      XrSpaceLocation space_location = {XR_TYPE_SPACE_LOCATION};
      XrResult result                = xrLocateSpace(platform_xr_input.hand_spaces[hand], platform_xr_app_space, platform_render_thread_frame_state.predictedDisplayTime, &space_location);
      if (XR_UNQUALIFIED_SUCCESS(result) &&
         (space_location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT   ) != 0 &&
         (space_location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0) {
           platform_xr_input.hand_poses[hand] = space_location.pose;
      }
    }
  }

  /*  Note: for quest2 "/user/gamepad" is an unsupported subaction path (gamepad doesn't work without subaction path as well...)
  {
    XrActionStateGetInfo get_info = {XR_TYPE_ACTION_STATE_GET_INFO};
    get_info.subactionPath        = platform_xr_input.gamepad_subaction_path;

    XrActionStateBoolean select_state = {XR_TYPE_ACTION_STATE_BOOLEAN};
    get_info.action = platform_xr_input.select_action;
    XrResult xr_result = xrGetActionStateBoolean(platform_xr_session, &get_info, &select_state);
    platform_xr_input.gamepad_select = select_state.currentState && select_state.changedSinceLastSync;
  }
  */
  input_state.gamepad_select = false;
  //input_state.gamepad_select = platform_xr_input.gamepad_select;

  input_state.left_hand_select  = platform_xr_input.hand_selects[0];
  input_state.right_hand_select = platform_xr_input.hand_selects[1];

  input_state.left_hand_transform.position     = platform_xr_input.hand_poses[0].position;
  input_state.left_hand_transform.orientation  = platform_xr_input.hand_poses[0].orientation;
  input_state.left_hand_transform.scale        = { 1.0f, 1.0f, 1.0f };
  input_state.right_hand_transform.position    = platform_xr_input.hand_poses[1].position;
  input_state.right_hand_transform.orientation = platform_xr_input.hand_poses[1].orientation;
  input_state.right_hand_transform.scale       = { 1.0f, 1.0f, 1.0f };

  return !is_platform_quit_requested;
}

void sync_render_thread_simulation_state() {
  PROFILE_FUNCTION();
  memcpy(&graphics_render_thread_sim_state.mesh_instances, &graphics_all_mesh_instances, sizeof(GraphicsMeshInstances));
  memcpy(&graphics_render_thread_sim_state.animation_states, &graphics_all_skeleton_instances.animation_states, sizeof(AnimationState) * std::size(graphics_render_thread_sim_state.animation_states));
}

void platform_render_frame() {
  PROFILE_FUNCTION();
  platform_render_thread_sync_semaphore.wait();

  XrFrameState frame_state = {XR_TYPE_FRAME_STATE};
  xrWaitFrame(platform_xr_session, nullptr, &frame_state);

  xrBeginFrame(platform_xr_session, nullptr);

  if (platform_xr_session_state == XR_SESSION_STATE_FOCUSED) {
    for (uint32_t hand=0; hand < 2; ++hand) {
      if (!platform_xr_input.render_hands[hand]) continue;

      XrSpaceLocation space_location = {XR_TYPE_SPACE_LOCATION};
      XrResult result = xrLocateSpace(platform_xr_input.hand_spaces[hand], platform_xr_app_space, platform_render_thread_frame_state.predictedDisplayTime, &space_location);

      if (XR_UNQUALIFIED_SUCCESS(result) &&
         (space_location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT   ) != 0 &&
         (space_location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0) {
           platform_xr_input.hand_poses[hand] = space_location.pose;
      }
    }

    input_state.left_hand_transform.position     = platform_xr_input.hand_poses[0].position;
    input_state.left_hand_transform.orientation  = platform_xr_input.hand_poses[0].orientation;
    input_state.right_hand_transform.position    = platform_xr_input.hand_poses[1].position;
    input_state.right_hand_transform.orientation = platform_xr_input.hand_poses[1].orientation;
  }

  head_pose_dependent_sim();

  for (GraphicsSkeletonInstances::Index i=0; i < (GraphicsSkeletonInstances::Index)std::size(graphics_render_thread_sim_state.animation_states); ++i) {
    if (graphics_render_thread_sim_state.animation_states[i].animation_id == uint32_t(-1)) {
      continue;
    }
    graphics_all_skeleton_instances.animation_states[i].tick_time();
  }

  sync_render_thread_simulation_state();
  platform_render_thread_frame_state = frame_state;

  platform_render_semaphore.signal();
}

void platform_render_thread() {
  PROFILE_THREAD("RenderThread");

  PFN_xrSetAndroidApplicationThreadKHR xrSetAndroidApplicationThreadKHR = nullptr;
  xrGetInstanceProcAddr(platform_xr_instance, "xrSetAndroidApplicationThreadKHR", (PFN_xrVoidFunction *)(&xrSetAndroidApplicationThreadKHR));
  xrSetAndroidApplicationThreadKHR(platform_xr_session, XR_ANDROID_THREAD_TYPE_RENDERER_MAIN_KHR, gettid());

  while (true) {
    platform_render_semaphore.wait();
    if (is_platform_quit_requested) {
      return;
    }

    XrCompositionLayerBaseHeader* application_composition_layer           = nullptr;
    XrCompositionLayerProjection application_composition_layer_projection = {XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    std::vector<XrCompositionLayerProjectionView> xr_views;

    if (platform_render_thread_frame_state.shouldRender) {
      uint32_t view_count               = 0;
      XrViewState view_state            = {XR_TYPE_VIEW_STATE};
      XrViewLocateInfo locate_info      = {XR_TYPE_VIEW_LOCATE_INFO};
      locate_info.viewConfigurationType = platform_xr_view_configuration_type;
      locate_info.displayTime           = platform_render_thread_frame_state.predictedDisplayTime;
      locate_info.space                 = platform_xr_app_space;
      xrLocateViews(platform_xr_session, &locate_info, &view_state, (uint32_t)platform_xr_views.size(), &view_count, platform_xr_views.data());
      assert(view_count == 2);
      xr_views.resize(view_count);

      uint32_t swapchain_image_index;
      XrSwapchainImageAcquireInfo acquire_info = {XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
      xrAcquireSwapchainImage(platform_xr_swapchain.handle, &acquire_info, &swapchain_image_index);

      for (uint32_t view_index=0; view_index < view_count; ++view_index) {
        xr_views[view_index] = {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};
        xr_views[view_index].pose                      = platform_xr_views[view_index].pose;
        xr_views[view_index].fov                       = platform_xr_views[view_index].fov;
        xr_views[view_index].subImage.swapchain        = platform_xr_swapchain.handle;
        xr_views[view_index].subImage.imageRect.offset = { 0, 0 };
        xr_views[view_index].subImage.imageRect.extent = { (int32_t)platform_xr_swapchain.width, (int32_t)platform_xr_swapchain.height };
        xr_views[view_index].subImage.imageArrayIndex  = view_index;
      }

      hmd_global_position    = lerp(xr_views[0].pose.position, xr_views[1].pose.position, 0.5f);
      hmd_global_orientation = slerp(xr_views[0].pose.orientation, xr_views[1].pose.orientation, 0.5f);

      vulkan_render_xr_views(xr_views, swapchain_image_index);  // this calls xrWaitSwapchainImage before rendering to swapchain image

      XrSwapchainImageReleaseInfo release_info = {XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
      xrReleaseSwapchainImage(platform_xr_swapchain.handle, &release_info);

      application_composition_layer_projection.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
      application_composition_layer_projection.space      = platform_xr_app_space;
      application_composition_layer_projection.viewCount  = (uint32_t)xr_views.size();
      application_composition_layer_projection.views      = xr_views.data();

      application_composition_layer = (XrCompositionLayerBaseHeader*)&application_composition_layer_projection;
    }

    XrCompositionLayerPassthroughFB passthrough_composition_layer{XR_TYPE_COMPOSITION_LAYER_PASSTHROUGH_FB};
    passthrough_composition_layer.next        = nullptr;
    passthrough_composition_layer.layerHandle = platform_xr_passthrough_layer;
    passthrough_composition_layer.flags       = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
    passthrough_composition_layer.space       = XR_NULL_HANDLE; // passthrough composition layer doesn’t contain any image data

    XrCompositionLayerBaseHeader* composition_layers[] = {
      (XrCompositionLayerBaseHeader*) &passthrough_composition_layer,
      application_composition_layer,
    };

    XrFrameEndInfo end_info{XR_TYPE_FRAME_END_INFO};
    end_info.displayTime          = platform_render_thread_frame_state.predictedDisplayTime;
    end_info.environmentBlendMode = platform_xr_blend_mode;
    end_info.layerCount           = application_composition_layer == nullptr ? 0 : std::size(composition_layers);
    end_info.layers               = composition_layers;
    xrEndFrame(platform_xr_session, &end_info);

    platform_render_thread_sync_semaphore.signal();
  }
}

void platform_audio_thread() {
  CPU_ZERO(&audio_thread_affinity_mask);
  sched_getaffinity(gettid(), sizeof(audio_thread_affinity_mask), &audio_thread_affinity_mask); // process id is same as main thread id
  assert(CPU_COUNT_S(sizeof(audio_thread_affinity_mask), &audio_thread_affinity_mask) > 0);

  uint32_t logical_processor_count = static_cast<uint32_t>(sysconf(_SC_NPROCESSORS_ONLN));
  uint32_t audio_thread_processor_number;
  for (uint32_t i=0; i < logical_processor_count; ++i) {
    if (CPU_ISSET(i, &audio_thread_affinity_mask)) {
      audio_thread_processor_number = i;
      break;
    }
  }

  CPU_ZERO(&audio_thread_affinity_mask);
  CPU_SET(audio_thread_processor_number, &audio_thread_affinity_mask);

  /*
  sched_setaffinity(pthread_gettid_np(ma_audio_device.thread), sizeof(affinity_mask), &affinity_mask);
  PFN_xrSetAndroidApplicationThreadKHR xrSetAndroidApplicationThreadKHR = nullptr;
  xrGetInstanceProcAddr(platform_xr_instance, "xrSetAndroidApplicationThreadKHR", (PFN_xrVoidFunction *)(&xrSetAndroidApplicationThreadKHR));
  xrSetAndroidApplicationThreadKHR(platform_xr_session, XR_ANDROID_THREAD_TYPE_APPLICATION_MAIN_KHR, pthread_gettid_np(ma_audio_device.thread));
  */

  set_audio_thread_affinity_mask = true;
}

static void app_cmd_callback(struct android_app* app, int32_t cmd) {
  AndroidUserData* app_state = (AndroidUserData*)app->userData;

  switch (cmd) {
    // APP_CMD_CREATE isn't needed when using ANativeActivity
    case APP_CMD_START: {
      DEBUG_LOG("APP_CMD_START\n");
      break;
    }
    case APP_CMD_RESUME: {
      DEBUG_LOG("APP_CMD_RESUME\n");
      app_state->is_paused = false;
      break;
    }
    case APP_CMD_PAUSE: {
      DEBUG_LOG("APP_CMD_PAUSE\n");
      app_state->is_paused = true;
      break;
    }
    case APP_CMD_STOP: {
      DEBUG_LOG("APP_CMD_STOP\n");
      break;
    }
    case APP_CMD_DESTROY: {
      DEBUG_LOG("APP_CMD_DESTROY\n");
      app_state->native_window = NULL;
      break;
    }
    case APP_CMD_INIT_WINDOW: {
      DEBUG_LOG("APP_CMD_INIT_WINDOW\n");
      app_state->native_window = app->window;
      break;
    }
    case APP_CMD_TERM_WINDOW: {
      DEBUG_LOG("APP_CMD_TERM_WINDOW\n");
      app_state->native_window = NULL;
      break;
    }
  }
}

void android_main(struct android_app* app) {  // entry point when using android_native_app_glue
  platform_android_app = app;
  JNIEnv* jni_env;
  app->activity->vm->AttachCurrentThread(&jni_env, nullptr);  // attaching to the JavaVM so this thread can make JNI calls

  app->userData = &android_app_state;
  app->onAppCmd = app_cmd_callback;

  android_app_activity  = app->activity->clazz;
  android_app_VM        = app->activity->vm;
  android_asset_manager = app->activity->assetManager;

  init_platform();
  sim_state.init();

  init_thread(platform_render_thread, 0);
  init_thread(platform_audio_thread, audio_thread_pool_index);  // audio_thread_pool_index should be 1

  tom::Clock frame_rate_clock;
  frame_rate_clock.start();

  while ( poll_platform_events() ) {
    PROFILE_FRAME("SimulationAndMainThread");
    sim_state.update();
    platform_render_frame();

    frame_rate_clock.stop();
    delta_time_seconds = frame_rate_clock.get_elapsed_time_seconds();
    frame_rate_clock.restart();
  }

  sim_state.exit();
  deactivate_platform();
  app->activity->vm->DetachCurrentThread();

  exit(0);
}
