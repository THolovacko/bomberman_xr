#pragma once

#define ENGINE_NAME         "Tom Engine"
#define ENGINE_VERSION      1
#define APPLICATION_VERSION 1

//#define USE_PROFILER

#pragma warning(disable:4530) // allow using std libs with exceptions disabled
#pragma warning(disable:4068) // hide unknown pragma clang warnings
#pragma clang diagnostic ignored "-Wswitch" // allow not handling all enumeration values in switch statements
