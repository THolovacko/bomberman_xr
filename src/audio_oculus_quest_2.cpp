#include <OVR_Audio.h>
#include <miniaudio.h>
#include <bitset>
#include <sched.h>
#include <vector>
#include <openxr/openxr.h>
#include "maths.h"
#include "platform.h"

#define SAMPLE_RATE_HZ 48000
#define SAMPLE_COUNT   480
#define CHANNEL_COUNT  2

struct AudioSources {
  typedef uint32_t Index;
  static constexpr uint32_t max_count = 32;

  struct ColdData {
    float attenuation_range_min_meters;
    float attenuation_range_max_meters;
    float radius_meters;  // 0.0 is a point source
    float reverb_send_level;  // (0.0f to 1.0f)
  };

  std::bitset<max_count> is_audio_source_playing;
  std::bitset<max_count> is_audio_source_looping;
  Vector3f   global_positions[max_count];
  ColdData   cold_data[max_count];
  ma_decoder decoders[max_count];

  uint32_t current_available_id = 0;
};

ma_context      ma_audio_context;
ma_device       ma_audio_device;
ovrAudioContext ovr_audio_context;
AudioSources    all_audio_sources;

void audio_data_callback(ma_device* device, void* output, const void* input, ma_uint32 frame_count)
{
  // The contents of the output buffer passed into the data callback will always be pre-initialized to silence
  // miniaudio will automatically clip samples by default (assuming ma_format_f32 playback sample format)

  assert(device->playback.channels == CHANNEL_COUNT);
  assert(device->playback.format == ma_format_f32);

  if (set_audio_thread_affinity_mask) { // HACK: need to figure out how to handle this in platform_audio_thread
    sched_setaffinity(getpid(), sizeof(audio_thread_affinity_mask), &audio_thread_affinity_mask);
    PFN_xrSetAndroidApplicationThreadKHR xrSetAndroidApplicationThreadKHR = nullptr;
    xrGetInstanceProcAddr(platform_xr_instance, "xrSetAndroidApplicationThreadKHR", (PFN_xrVoidFunction *)(&xrSetAndroidApplicationThreadKHR));
    xrSetAndroidApplicationThreadKHR(platform_xr_session, XR_ANDROID_THREAD_TYPE_APPLICATION_MAIN_KHR, getpid());
    set_audio_thread_affinity_mask = false;
  }

  static float* original_samples    = ovrAudio_AllocSamples(SAMPLE_COUNT);  // these are monophonic
  static float* spatialized_samples = ovrAudio_AllocSamples(SAMPLE_COUNT * CHANNEL_COUNT);
  static uint32_t statuses[AudioSources::max_count];
  float* mix_buffer = (float*)output;
  ovrResult ovr_result;

  const Vector3f tmp_hmd_global_position = hmd_global_position.load();
  Quaternionf tmp_hmd_global_orientation = hmd_global_orientation.load();
  const Vector3f hmd_forward_vector      = calculate_forward_vector(tmp_hmd_global_orientation);
  const Vector3f hmd_up_vector           = calculate_up_vector(tmp_hmd_global_orientation);
  ovr_result = ovrAudio_SetListenerVectors(ovr_audio_context, tmp_hmd_global_position.x, tmp_hmd_global_position.y, tmp_hmd_global_position.z, hmd_forward_vector.x, hmd_forward_vector.y, hmd_forward_vector.z, hmd_up_vector.x, hmd_up_vector.y, hmd_up_vector.z);

  for (AudioSources::Index audio_source_index=0; audio_source_index < AudioSources::max_count; ++audio_source_index) {
    if (all_audio_sources.is_audio_source_playing[audio_source_index] || (statuses[audio_source_index] == ovrAudioSpatializationStatus_Working)) {
      // TODO: I don't think the standard guarantees that all bits being zero represent 0.0f but I think most modern architectures do so maybe memset
      for (size_t i=0; i < SAMPLE_COUNT; ++i) original_samples[i] = 0.0f;
      for (size_t i=0; i < (SAMPLE_COUNT * CHANNEL_COUNT); ++i) spatialized_samples[i] = 0.0f;

      const Vector3f& position = all_audio_sources.global_positions[audio_source_index];
      ovr_result = ovrAudio_SetAudioSourcePos(ovr_audio_context, audio_source_index, position.x, position.y, position.z);

      ma_uint32 total_frames_read = 0;
      while (total_frames_read < frame_count) {
        const ma_uint32 total_frames_remaining = frame_count - total_frames_read;

        ma_uint32 frames_to_read_this_iteration = SAMPLE_COUNT;
        frames_to_read_this_iteration = ( total_frames_remaining * static_cast<ma_uint32>(frames_to_read_this_iteration > total_frames_remaining) ) + ( frames_to_read_this_iteration * static_cast<ma_uint32>(frames_to_read_this_iteration <= total_frames_remaining) );

        ma_uint64 frames_read;
        if (all_audio_sources.is_audio_source_playing[audio_source_index]) {
          ma_decoder_read_pcm_frames(&all_audio_sources.decoders[audio_source_index], original_samples, frames_to_read_this_iteration, &frames_read);
        } else {  // handle reverberation tail by play silence until status is ovrAudioSpatializationStatus_Finished
          for (size_t i=0; i < SAMPLE_COUNT; ++i) original_samples[i] = 0.0f;
          frames_read = frames_to_read_this_iteration;
        }

        ovr_result = ovrAudio_SpatializeMonoSourceInterleaved(ovr_audio_context, audio_source_index, &(statuses[audio_source_index]), spatialized_samples, original_samples);

        const ma_uint64 offset = total_frames_read * CHANNEL_COUNT;
        for (ma_uint64 sample_index=0; sample_index < (frames_read * CHANNEL_COUNT); ++sample_index) {
          mix_buffer[offset + sample_index] += spatialized_samples[sample_index];
        }

        total_frames_read += (ma_uint32)frames_read;

        if (frames_read < frames_to_read_this_iteration) {
          if (all_audio_sources.is_audio_source_looping[audio_source_index]) {
            ma_decoder_seek_to_pcm_frame(&all_audio_sources.decoders[audio_source_index], 0);
          } else {
            all_audio_sources.is_audio_source_playing[audio_source_index] = false;
          }
        }
      }
    }
  }

  uint32_t status;
  ovr_result = ovrAudio_MixInSharedReverbInterleaved(ovr_audio_context, &status, mix_buffer);
}

bool init_audio() {
  {
    if (ma_context_init(NULL, 0, NULL, &ma_audio_context) != MA_SUCCESS) return false;

    ma_device_info* playback_info;
    ma_uint32 playback_count;
    ma_device_info* capture_info;
    ma_uint32 capture_count;
    if (ma_context_get_devices(&ma_audio_context, &playback_info, &playback_count, &capture_info, &capture_count) != MA_SUCCESS) return false;

    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format    = ma_format_f32;
    config.playback.channels  = CHANNEL_COUNT;
    config.sampleRate         = SAMPLE_RATE_HZ;
    config.dataCallback       = audio_data_callback;
    config.performanceProfile = ma_performance_profile_low_latency;
    config.pUserData          = nullptr;  // a void* that can be accessed from device object during audio_data_callback
    // using the operating system's default device by not setting config.playback.pDeviceID

    if (ma_device_init(&ma_audio_context, &config, &ma_audio_device) != MA_SUCCESS) return false;
  }

  // assert audio sdk version is correct
  {
    int major;
    int minor;
    int patch;
    const char* version_str;

    version_str = ovrAudio_GetVersion(&major, &minor, &patch);
    DEBUG_LOG("OVRAudio version: %s\n", version_str);

    assert(major == OVR_AUDIO_MAJOR_VERSION && minor == OVR_AUDIO_MINOR_VERSION);
  }

  // create audio context
  {
    ovrAudioContextConfiguration config = {};
    config.acc_Size          = sizeof(config);
    config.acc_SampleRate    = SAMPLE_RATE_HZ;
    config.acc_BufferLength  = SAMPLE_COUNT;
    config.acc_MaxNumSources = AudioSources::max_count;

    if ( ovrAudio_CreateContext(&ovr_audio_context, &config) != ovrSuccess ) return false;
  }

  ovrAudio_Enable(ovr_audio_context, ovrAudioEnable_SimpleRoomModeling, 1);
  ovrAudio_Enable(ovr_audio_context, ovrAudioEnable_LateReverberation, 1);
  ovrAudio_Enable(ovr_audio_context, ovrAudioEnable_RandomizeReverb, 1);

  if ( ovrAudio_SetReflectionModel(ovr_audio_context, ovrAudioReflectionModel_StaticShoeBox) != ovrSuccess ) return false;

  // setup box room model
  {
    ovrAudioBoxRoomParameters box_room_params = {};
    box_room_params.brp_Size          = sizeof(box_room_params);
    box_room_params.brp_ReflectLeft   = 0.1f; // 0.0 is fully absorbed and 1.0 is fully reflected (max is 0.97 though)
    box_room_params.brp_ReflectRight  = 0.1f;
    box_room_params.brp_ReflectUp     = 0.1f;
    box_room_params.brp_ReflectDown   = 0.1f;
    box_room_params.brp_ReflectFront  = 0.1f;
    box_room_params.brp_ReflectBehind = 0.1f;
    box_room_params.brp_Width         = 8.0f; // meters
    box_room_params.brp_Height        = 8.0f;
    box_room_params.brp_Depth         = 8.0f;

    if ( ovrAudio_SetSimpleBoxRoomParameters(ovr_audio_context, &box_room_params) != ovrSuccess ) return false;
  }

  ovrAudio_SetSharedReverbWetLevel(ovr_audio_context, 1.0f);

  return true;
}

void play_audio_device() {
  ma_device_start(&ma_audio_device);
}

void stop_audio_device() {
  ma_device_stop(&ma_audio_device);
}

void deactivate_audio() {
  ma_device_uninit(&ma_audio_device);
  ma_context_uninit(&ma_audio_context);
  ovrAudio_DestroyContext(ovr_audio_context);
}

uint32_t create_audio_source(const char* file_path, const float attenuation_range_min_meters, const float attenuation_range_max_meters, const float radius_meters, const float reverb_send_level, const bool is_narrow_band) {
  const uint32_t id = all_audio_sources.current_available_id;
  assert(id < AudioSources::max_count);
  all_audio_sources.current_available_id += 1;
  
  ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 1, SAMPLE_RATE_HZ);
  ma_decoder_init_file(file_path, &config, &all_audio_sources.decoders[id]);

  all_audio_sources.cold_data[id].attenuation_range_min_meters = attenuation_range_min_meters;
  all_audio_sources.cold_data[id].attenuation_range_max_meters = attenuation_range_max_meters;
  all_audio_sources.cold_data[id].radius_meters                = radius_meters;
  all_audio_sources.cold_data[id].reverb_send_level            = reverb_send_level;

  if (!is_narrow_band) {
    ovrAudio_SetAudioSourceFlags(ovr_audio_context, id, ovrAudioSourceFlag_WideBand_HINT);
  } else {
    ovrAudio_SetAudioSourceFlags(ovr_audio_context, id, ovrAudioSourceFlag_NarrowBand_HINT);
  }

  ovrAudio_SetAudioSourceAttenuationMode(ovr_audio_context, id, ovrAudioSourceAttenuationMode_InverseSquare, 1.0f); // last param is only relevant if using ovrAudioSourceAttenuationMode_Fixed
  ovrAudio_SetAudioSourceRange(ovr_audio_context, id, all_audio_sources.cold_data[id].attenuation_range_min_meters, all_audio_sources.cold_data[id].attenuation_range_max_meters);
  ovrAudio_SetAudioSourceRadius(ovr_audio_context, id, all_audio_sources.cold_data[id].radius_meters);

  return id;
}

void update_audio_source(const uint32_t id, const Vector3f position) {
  all_audio_sources.global_positions[id] = position;
}

void delete_audio_source(const uint32_t id) {
  all_audio_sources.is_audio_source_playing[id] = false;
  all_audio_sources.is_audio_source_looping[id] = false;
  ovrAudio_ResetAudioSource(ovr_audio_context, (int)id);
  ma_decoder_uninit(&all_audio_sources.decoders[id]);
}

void play_audio_source(const uint32_t id, const bool should_loop) {
  ma_decoder_seek_to_pcm_frame(&all_audio_sources.decoders[id], 0);
  all_audio_sources.is_audio_source_playing[id] = true;
  all_audio_sources.is_audio_source_looping[id] = should_loop;
}

void stop_audio_source(const uint32_t id) {
  all_audio_sources.is_audio_source_playing[id] = false;
  ovrAudio_ResetAudioSource(ovr_audio_context, (int)id);
}
