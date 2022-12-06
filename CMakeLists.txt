cmake_minimum_required(VERSION 3.22.1)

set(ANDROID_STL c++_static)
project("bomberman_xr")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(ANDROID)
  option(OCULUS_QUEST_2 "Build for the Quest only?" OFF)
else()
  option(OCULUS_PC "Build for Oculus headsets only?" OFF)
endif()

file(GLOB vulkan_shaders
  shaders/glsl/*.vert
  shaders/glsl/*.frag
)

find_package(Vulkan REQUIRED)
if(ANDROID)
  file(GLOB GLSLC_DIRS ${CMAKE_ANDROID_NDK}/shader-tools/*)
else()
  file(GLOB GLSLC_DIRS $ENV{VULKAN_SDK}/*)
endif()
find_program(glslc_program glslc PATHS ${GLSLC_DIRS})
if(NOT glslc_program)
  message(FATAL_ERROR "Need glslc")
endif()


function(compile_shader target shader_absolute_file_path)
  get_filename_component(shader_name ${shader_absolute_file_path} NAME)
  set(relative_shader_path shaders/glsl/${shader_name})


  set(OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR})
  add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${shader_name}.spv
    COMMAND ${glslc_program} $<IF:$<CONFIG:Debug>,,-O> -mfmt=num -o ${CMAKE_CURRENT_BINARY_DIR}/${shader_name}.spv ${relative_shader_path}
    DEPENDS ${relative_shader_path}
    IMPLICIT_DEPENDS CXX ${relative_shader_path})

  set_source_files_properties(${output_file_path} PROPERTIES GENERATED TRUE)
  target_sources(${target} PRIVATE ${output_file_path})
endfunction()

# Gradle automatically packages shared libraries with APK
add_library(bomberman_xr SHARED
  src/pch.cpp
  src/core_header_implementations.cpp
  src/simulation.cpp
)
if(ANDROID)
  find_library(log_lib log)
  target_sources(bomberman_xr PRIVATE
  ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c)
  target_include_directories(bomberman_xr PRIVATE
    ${ANDROID_NDK}/sources/android/native_app_glue
  )
  target_link_libraries(bomberman_xr
    android
    ${log_lib})
endif()

target_include_directories(bomberman_xr PRIVATE
  dependencies/miniaudio-master-11-05-2022/miniaudio-master/
  dependencies/openxr_linear-05-27-2022/
  dependencies/cgltf-1.12/
  dependencies/stb-master-09-10-2021/
  dependencies/glm-0.9.9.8/
  src/
  ${CMAKE_CURRENT_BINARY_DIR}
)
target_compile_definitions(bomberman_xr PRIVATE GLM_FORCE_CXX2A)
foreach(file ${vulkan_shaders})
  message(${file})
  compile_shader(bomberman_xr ${file})
endforeach()

if(ANDROID)
  target_compile_definitions(bomberman_xr PRIVATE MA_ENABLE_ONLY_SPECIFIC_BACKENDS MA_ENABLE_OPENSL)
endif()

if(OCULUS_QUEST_2)
  target_compile_definitions(bomberman_xr PRIVATE OCULUS_QUEST_2)
  include(OculusOpenXR.cmake)
  include(OculusAudio.cmake)
  target_link_libraries(bomberman_xr
    ovr_audio_static_64_lib
    ovr_audio_64_lib)
else()
  find_package(OpenXR REQUIRED)
endif()

if(OCULUS_PC)
  target_compile_definitions(bomberman_xr PRIVATE OCULUS_PC)
endif()

target_link_libraries(bomberman_xr OpenXR::openxr_loader Vulkan::Vulkan)
target_compile_definitions(bomberman_xr PRIVATE APPLICATION_NAME="${PROJECT_NAME}")
