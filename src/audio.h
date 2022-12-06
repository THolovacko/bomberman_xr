#ifndef INCLUDE_TOM_ENGINE_AUDIO_H
#define INCLUDE_TOM_ENGINE_AUDIO_H

bool init_audio();
void deactivate_audio();
uint32_t create_audio_source(const char* file_path, const float attenuation_range_min_meters, const float attenuation_range_max_meters, const float radius_meters, const float reverb_send_level, const bool is_narrow_band=false);
void update_audio_source(const uint32_t id, const Vector3f position);
void delete_audio_source(const uint32_t id);
void play_audio_source(const uint32_t id, const bool should_loop=false);
void stop_audio_source(const uint32_t id);
void play_audio_device();
void stop_audio_device();

#endif  // INCLUDE_TOM_ENGINE_AUDIO_H


//////////////////////////////////////////////////


#ifdef TOM_ENGINE_AUDIO_IMPLEMENTATION
#ifndef TOM_ENGINE_AUDIO_IMPLEMENTATION_SINGLE
#define TOM_ENGINE_AUDIO_IMPLEMENTATION_SINGLE

#if defined(OCULUS_PC)
  #include "audio_oculus_pc.cpp"
#elif defined(OCULUS_QUEST_2)
  #include "audio_oculus_quest_2.cpp"
#endif

#endif  // TOM_ENGINE_AUDIO_IMPLEMENTATION_SINGLE
#endif  // TOM_ENGINE_AUDIO_IMPLEMENTATION
