#include "config.h"

#define TOM_ENGINE_PROFILER_IMPLEMENTATION
#include "profiler.h"

#define TOM_ENGINE_MATHS_IMPLEMENTATION
#include "maths.h"

#define TOM_ENGINE_PLATFORM_IMPLEMENTATION
#include "platform.h"

#define TOM_ENGINE_GRAPHICS_IMPLEMENTATION
#include "graphics.h"

#define TOM_ENGINE_AUDIO_IMPLEMENTATION
#include "audio.h"


/* 3rd party single-file-headers go last */
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <stb_image_resize.h>

#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#pragma warning(disable : 4996)
#define CGLTF_IMPLEMENTATION
#define CGLTF_WRITE_IMPLEMENTATION
#include <cgltf_write.h>  // this file includes cgltf.h
