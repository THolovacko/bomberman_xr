
add_library(ovr_audio_static_64_lib STATIC IMPORTED)
set_target_properties(ovr_audio_static_64_lib PROPERTIES IMPORTED_LOCATION
  ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/ovr_audio_spatializer_native_32.0.0/AudioSDK/Lib/Android/arm64-v8a/libovraudiostatic64.a
)
target_include_directories(ovr_audio_static_64_lib INTERFACE
  dependencies/ovr_audio_spatializer_native_32.0.0/AudioSDK/Include/)

add_library(ovr_audio_64_lib SHARED IMPORTED)
set_target_properties(ovr_audio_64_lib PROPERTIES IMPORTED_LOCATION
  ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/ovr_audio_spatializer_native_32.0.0/AudioSDK/Lib/Android/arm64-v8a/libovraudio64.so
)
target_include_directories(ovr_audio_64_lib INTERFACE
  dependencies/ovr_audio_spatializer_native_32.0.0/AudioSDK/Include/)
