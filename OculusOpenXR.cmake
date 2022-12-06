
add_library(OpenXR::openxr_loader SHARED IMPORTED)
target_include_directories(OpenXR::openxr_loader INTERFACE
  dependencies/ovr_openxr_mobile_sdk_42.0/3rdParty/khronos/openxr/OpenXR-SDK/include/)

set_target_properties(OpenXR::openxr_loader PROPERTIES IMPORTED_LOCATION_DEBUG
  ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/ovr_openxr_mobile_sdk_42.0/OpenXR/Libs/Android/arm64-v8a/Debug/libopenxr_loader.so
)
set_target_properties(OpenXR::openxr_loader PROPERTIES IMPORTED_LOCATION
  ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/ovr_openxr_mobile_sdk_42.0/OpenXR/Libs/Android/arm64-v8a/Release/libopenxr_loader.so
)
