cmake_minimum_required(VERSION 3.22.1)

set(ANDROID_STL c++_static)
project("bomberman_xr")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(BUILD_STANDARD_OPENXR_LOADER "Use the standard OpenXR loader instead of the Oculus one." OFF)
set(solution_dir "../../../../")

file(GLOB vulkan_shaders
  ${solution_dir}shaders/glsl/*.vert
  ${solution_dir}shaders/glsl/*.frag
)

if(ANDROID)
  file(GLOB GLSLC_DIRS ${CMAKE_ANDROID_NDK}/shader-tools/*)
else()
  file(GLOB GLSLC_DIRS $ENV{VULKAN_SDK}/*)
endif()
find_program(glslc_program glslc PATHS ${GLSLC_DIRS})
function(compile_shader target shader_absolute_file_path)
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
  ${solution_dir}dependencies/miniaudio-master-11-05-2022/miniaudio-master/
  ${solution_dir}dependencies/openxr_linear-05-27-2022/
  ${solution_dir}dependencies/cgltf-1.12/
  ${solution_dir}dependencies/stb-master-09-10-2021/
  ${solution_dir}dependencies/glm-0.9.9.8/
  $ENV{VK_SDK_PATH}/Include/
  ${solution_dir}src/
  ${solution_dir}shaders/glsl/bin/
  ${ANDROID_NDK}/sources/android/native_app_glue
)
target_compile_definitions(bomberman_xr PRIVATE MA_ENABLE_ONLY_SPECIFIC_BACKENDS MA_ENABLE_OPENSL GLM_FORCE_CXX2A)
foreach(file ${vulkan_shaders})
  message(${file})
  compile_shader(bomberman_xr ${file})
endforeach()


find_library(log-lib log)

if(BUILD_STANDARD_OPENXR_LOADER)
  find_package(OpenXR REQUIRED)
  target_compile_definitions(bomberman_xr PRIVATE BUILD_STANDARD_OPENXR_LOADER)
else()
  target_compile_definitions(bomberman_xr PRIVATE OCULUS_QUEST_2)
  include(OculusOpenXR.cmake)
  include(OculusAudio.cmake)
  target_link_libraries(bomberman_xr
    ovr_audio_static_64_lib
    ovr_audio_64_lib)
endif()
  target_link_libraries(bomberman_xr OpenXR::openxr_loader)

target_link_libraries(bomberman_xr
  android
  ${log-lib}
  libvulkan.so
)
