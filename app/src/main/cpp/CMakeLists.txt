cmake_minimum_required(VERSION 3.22.1)

project("bomberman_xr")
set(solution_dir "../../../../")

file(GLOB vulkan_shaders 
  ${solution_dir}shaders/glsl/*.vert
  ${solution_dir}shaders/glsl/*.frag
)

function(compile_shader target shader_absolute_file_path)
  find_program(glslc_program glslc)
  get_filename_component(shader_name ${shader_absolute_file_path} NAME)
  set(relative_shader_path ../../../../../shaders/glsl/${shader_name})

  if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(output_file_path ../../../../../shaders/glsl/bin/debug/${shader_name}.spv)
  else()
    set(output_file_path ../../../../../shaders/glsl/bin/release/${shader_name}.spv)
  endif()

  get_filename_component(output_dir ${output_file_path} DIRECTORY)
  file(MAKE_DIRECTORY ${output_dir})

  if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_custom_command(
      OUTPUT ${output_file_path}
      COMMAND ${glslc_program} -mfmt=num -o ${output_file_path} ${relative_shader_path}
      DEPENDS ${relative_shader_path}
      IMPLICIT_DEPENDS CXX ${relative_shader_path}
      VERBATIM
    )
  else()
    add_custom_command(
      OUTPUT ${output_file_path}
      COMMAND ${glslc_program} -O -mfmt=num -o ${output_file_path} ${relative_shader_path}
      DEPENDS ${relative_shader_path}
      IMPLICIT_DEPENDS CXX ${relative_shader_path}
      VERBATIM
    )
  endif()

  set_source_files_properties(${output_file_path} PROPERTIES GENERATED TRUE)
  target_sources(${target} PRIVATE ${output_file_path})
endfunction()

# Gradle automatically packages shared libraries with APK
add_library(bomberman_xr SHARED
  ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c
  ${solution_dir}src/pch.cpp
  ${solution_dir}src/core_header_implementations.cpp
  ${solution_dir}src/simulation.cpp
)

target_include_directories(bomberman_xr PRIVATE
  ${solution_dir}dependencies/ovr_audio_spatializer_native_32.0.0/AudioSDK/Include/
  ${solution_dir}dependencies/miniaudio-master-11-05-2022/miniaudio-master/
  ${solution_dir}dependencies/openxr_linear-05-27-2022/
  ${solution_dir}dependencies/cgltf-1.12/
  ${solution_dir}dependencies/stb-master-09-10-2021/
  ${solution_dir}dependencies/ovr_openxr_mobile_sdk_42.0/3rdParty/khronos/openxr/OpenXR-SDK/include/
  ${solution_dir}dependencies/glm-0.9.9.8/
  $ENV{VK_SDK_PATH}/Include/
  ${solution_dir}src/
  ${solution_dir}shaders/glsl/bin/
  ${ANDROID_NDK}/sources/android/native_app_glue
)

foreach(file ${vulkan_shaders})
  message(${file})
  compile_shader(bomberman_xr ${file})
endforeach()

add_library(ovr_audio_static_64_lib STATIC IMPORTED)
set_target_properties(ovr_audio_static_64_lib PROPERTIES IMPORTED_LOCATION
  ${CMAKE_CURRENT_SOURCE_DIR}${solution_dir}../dependencies/ovr_audio_spatializer_native_32.0.0/AudioSDK/Lib/Android/arm64-v8a/libovraudiostatic64.a
)

add_library(ovr_audio_64_lib SHARED IMPORTED)
set_target_properties(ovr_audio_64_lib PROPERTIES IMPORTED_LOCATION
  ${CMAKE_CURRENT_SOURCE_DIR}${solution_dir}../dependencies/ovr_audio_spatializer_native_32.0.0/AudioSDK/Lib/Android/arm64-v8a/libovraudio64.so
)

find_library(log-lib log)

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_library(openxr_loader_lib SHARED IMPORTED)
  set_target_properties(openxr_loader_lib PROPERTIES IMPORTED_LOCATION
    ${CMAKE_CURRENT_SOURCE_DIR}${solution_dir}../dependencies/ovr_openxr_mobile_sdk_42.0/OpenXR/Libs/Android/arm64-v8a/Debug/libopenxr_loader.so
  )
else()
  add_library(openxr_loader_lib SHARED IMPORTED)
  set_target_properties(openxr_loader_lib PROPERTIES IMPORTED_LOCATION
    ${CMAKE_CURRENT_SOURCE_DIR}${solution_dir}../dependencies/ovr_openxr_mobile_sdk_42.0/OpenXR/Libs/Android/arm64-v8a/Release/libopenxr_loader.so
  )
endif()

target_link_libraries(bomberman_xr
  android
  ${log-lib}
  ovr_audio_static_64_lib
  ovr_audio_64_lib
  openxr_loader_lib
  libvulkan.so
)
