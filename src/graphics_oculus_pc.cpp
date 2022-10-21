#define XR_USE_GRAPHICS_API_VULKAN

#include <vulkan/vulkan.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define XR_USE_PLATFORM_WIN32
#include <windows.h>
#include <unknwn.h>
#include <vulkan/vulkan_win32.h>

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <openxr/openxr_reflection.h>
#include <cstdio>
#include <string>
#include <algorithm>
#include <map>
#include <vector>
#include <type_traits>
#include <fstream>
#include <cstddef>
#include <memory>
#include <stb_image.h>
#include <stb_image_resize.h>
#include <cgltf.h>
#include <unordered_map>
#include <bitset>
#include <cmath>
#include <limits>
#include "config.h"
#include "platform.h"
#include "simulation.h"
#include "tom_std.h"
#include "profiler.h"


constexpr VkSampleCountFlagBits  vulkan_msaa_count           = VK_SAMPLE_COUNT_4_BIT;
constexpr uint32_t               vulkan_max_frames_in_flight = 2;

constexpr VkIndexType get_vulkan_index_type() {
  if (std::is_same<GraphicsIndex,uint16_t>::value) {
    return VK_INDEX_TYPE_UINT16;
  } else if (std::is_same<GraphicsIndex,uint32_t>::value) {
    return VK_INDEX_TYPE_UINT32;
  } else {
    return VK_INDEX_TYPE_NONE_KHR;
  }
}

struct VkVertexBuffer {
  VkBuffer buffer{VK_NULL_HANDLE};
  VkDeviceMemory memory{VK_NULL_HANDLE};
  size_t max_indices_count;
  size_t max_vertices_count;
  void* mapped_memory = nullptr;
  VkDeviceSize vertices_partition_offset;
  VkDeviceSize size;
  VkBuffer       staging_buffer;
  VkDeviceMemory staging_buffer_memory;

  void update(const GraphicsIndex* indices, const size_t indices_count, const GraphicsVertex* vertices, const size_t vertices_count, const size_t indices_offset=0, const size_t vertices_offset=0);
};

struct VkInstanceBuffer {
  VkBuffer buffer{VK_NULL_HANDLE};
  VkDeviceMemory memory{VK_NULL_HANDLE};
  size_t max_instance_count;
  void* mapped_memory = nullptr;
  VkDeviceSize size;
  VkBuffer       staging_buffer;
  VkDeviceMemory staging_buffer_memory;

  void update(const VkCommandBuffer& command_buffer, const GraphicsMeshInstances::BufferData* instances, const size_t instance_count, const size_t instance_offset=0);
};

struct VkTextureArray {
  VkFormat format;
  VkImage layered_image;
  VkDeviceMemory layered_image_memory;
  VkImageView layered_image_view;
  uint32_t layered_image_width;
  uint32_t layered_image_height;
  uint32_t layered_image_mip_level_count;
  uint32_t max_texture_count;
  VkDeviceSize texture_size;
  VkBuffer texture_staging_buffer;
  VkDeviceMemory texture_staging_buffer_memory;
  void* texture_mapped_memory = nullptr;

  void upload_texture_from_file(const std::string& file_path, const uint32_t index);
};

struct VkUniformBuffer {
  VkBuffer buffer;
  VkDeviceMemory memory{VK_NULL_HANDLE};
  void* mapped_memory;
};

struct GlobalUniformBufferObject {
  alignas(16) Matrix4x4f view_projection_matrices[2];  // one per eye
  alignas(16) Vector3f   directional_light;
  alignas(16) Vector3f   directional_light_color;
  alignas(16) Vector3f   left_eye_position;
  alignas(4)  float      ambient_light_factor;
};

struct MaterialUniformBufferObject {
  static constexpr uint32_t max_count = 16;  // this is also hardcoded in fragment shader

  GraphicsMaterial materials[max_count];
};

struct SkeletonInstanceUniformBufferObject {
  GraphicsSkeletonInstances::BufferData skeleton_instances;
};

struct VkXrSwapchainContext {
  std::vector<XrSwapchainImageVulkan2KHR> images;
  std::vector<VkImageView> image_views;
  std::vector<VkFramebuffer> framebuffers;
  VkExtent2D image_size{};
  VkDeviceMemory depth_buffer_memory{VK_NULL_HANDLE};
  VkImage depth_buffer{VK_NULL_HANDLE};
  VkImageView depth_buffer_view{VK_NULL_HANDLE};
  static constexpr VkFormat depth_buffer_format = VK_FORMAT_D32_SFLOAT;
  VkDeviceMemory color_buffer_memory{VK_NULL_HANDLE};
  VkImage color_buffer{VK_NULL_HANDLE};
  VkImageView color_buffer_view{VK_NULL_HANDLE};
  VkRenderPass render_pass_pattern{VK_NULL_HANDLE};
  VkFormat image_color_format{};
  VkPipeline pipeline{VK_NULL_HANDLE};
  VkPipelineLayout pipeline_layout{VK_NULL_HANDLE};
  VkSemaphore render_finished_semaphores[vulkan_max_frames_in_flight];
  VkFence frame_in_flight_fences[vulkan_max_frames_in_flight];
  VkCommandPool command_pool{VK_NULL_HANDLE};
  std::vector<VkCommandBuffer> command_buffers;
  uint32_t current_frame = 0;

  void bind_render_target(const uint32_t image_index, VkRenderPassBeginInfo* const render_pass_begin_info);
};

struct VkIndexedDrawCall {
  uint32_t index_count;
  uint32_t instance_count;
  uint32_t first_index;
  int32_t  vertex_offset;
  uint32_t first_instance;
};

struct VkMeshes {
  typedef uint32_t Index;
  static constexpr Index max_count = 64;

  struct HotDrawData {
    uint32_t index_count;
    uint32_t first_index;
    uint32_t first_vertex;
  };

  struct ColdDrawData {
    uint32_t vertex_count;
  };

  struct SourceFileIdData {
    Index first_mesh_id = -1;
    Index mesh_count    = 0;
  };

  HotDrawData       all_hot_draw_data[max_count];
  ColdDrawData      all_cold_draw_data[max_count];
  VkIndexedDrawCall all_indexed_draw_calls[max_count];
  float             all_radii[max_count];
  std::unordered_map<std::string, SourceFileIdData> source_file_path_to_id_data;
  Index current_available_mesh_id = 0;

  VkVertexBuffer* draw_buffer;
  size_t current_draw_buffer_index_count  = 0;
  size_t current_draw_buffer_vertex_count = 0;

  SourceFileIdData get_id_data_from_source_file_path(const char* file_path) const {
    std::string file_path_str(file_path);
    if ( source_file_path_to_id_data.count(file_path_str) ) {
      return source_file_path_to_id_data.at(file_path_str);
    }

    return SourceFileIdData();
  }

  SourceFileIdData insert_from_glb_source_file(const char* file_path, GraphicsModel* model_file_cache=nullptr);
};

struct VkAnimations {
  typedef uint32_t Index;
  static constexpr Index max_count = 16;

  struct SourceFileIdData {
    Index first_animation_id = -1;
    Index animation_count    = 0;
  };

  Animation animations[max_count];
  std::unordered_map<std::string, SourceFileIdData> source_file_path_to_id_data;
  Index current_available_animation_id = 0;

  SourceFileIdData get_id_data_from_source_file_path(const char* file_path) const {
    std::string file_path_str(file_path);
    if ( source_file_path_to_id_data.count(file_path_str) ) {
      return source_file_path_to_id_data.at(file_path_str);
    }

    return SourceFileIdData();
  }

  SourceFileIdData insert_from_glb_source_file(const char* file_path, GraphicsModel* model_file_cache=nullptr);
};

struct VkSkeletons {
  typedef uint16_t Index;
  static constexpr Index max_count = 16;

  struct SourceFileIdData {
    Index first_skeleton_id = -1;
    Index skeleton_count    = 0;
  };

  GraphicsSkeleton skeletons[max_count];
  std::unordered_map<std::string, SourceFileIdData> source_file_path_to_id_data;
  Index current_available_skeleton_id = 0;

  SourceFileIdData get_id_data_from_source_file_path(const char* file_path) const {
    std::string file_path_str(file_path);
    if ( source_file_path_to_id_data.count(file_path_str) ) {
      return source_file_path_to_id_data.at(file_path_str);
    }

    return SourceFileIdData();
  }

  SourceFileIdData insert_from_glb_source_file(const char* file_path, GraphicsModel* model_file_cache=nullptr);
};

struct VkMirrorSwapchainContext {
  static constexpr uint32_t max_image_count = 4;
  VkFormat surface_format;
  VkExtent2D image_size;
  VkSurfaceKHR surface{VK_NULL_HANDLE};
  VkSwapchainKHR swapchain{VK_NULL_HANDLE};
  VkSemaphore image_available_semaphores[vulkan_max_frames_in_flight];
  VkSemaphore render_finished_semaphores[vulkan_max_frames_in_flight];
  VkFence frame_in_flight_fences[vulkan_max_frames_in_flight];
  uint32_t image_count;
  uint32_t current_frame = 0;
  VkImage images[max_image_count]{VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE};
  VkCommandPool command_pool{VK_NULL_HANDLE};
  std::vector<VkCommandBuffer> command_buffers;
  uint32_t crop_width_src_offset_0;
  uint32_t crop_width_src_offset_1;
  uint32_t crop_height_src_offset_0;
  uint32_t crop_height_src_offset_1;
};

VkInstance                       vulkan_instance{VK_NULL_HANDLE};
VkDebugUtilsMessengerEXT         vulkan_debug_utils_messenger{VK_NULL_HANDLE};
VkPhysicalDevice                 vulkan_physical_device{VK_NULL_HANDLE};
VkDevice                         vulkan_logical_device{VK_NULL_HANDLE};
VkQueue                          vulkan_queue{VK_NULL_HANDLE};
uint32_t                         vulkan_queue_family_index;
VkPhysicalDeviceProperties       vulkan_physical_device_properties;
VkPhysicalDeviceMemoryProperties vulkan_physical_device_memory_properties;
VkXrSwapchainContext*            vulkan_xr_swapchain_context;
VkInstanceBuffer*                vulkan_instance_buffers[vulkan_max_frames_in_flight];
VkCommandPool                    vulkan_temporary_command_pool{VK_NULL_HANDLE};
VkSampler                        vulkan_texture_sampler;
constexpr float                  vulkan_texture_sampler_max_anisotropy = 2.0f;
VkDescriptorSetLayout            vulkan_descriptor_set_layout;
VkDescriptorPool                 vulkan_descriptor_pool;
VkDescriptorSet                  vulkan_descriptor_sets[vulkan_max_frames_in_flight];
VkTextureArray*                  vulkan_base_color_map_array;
VkUniformBuffer*                 vulkan_global_uniform_buffers[vulkan_max_frames_in_flight];
VkUniformBuffer*                 vulkan_material_uniform_buffer;
VkUniformBuffer*                 vulkan_skeleton_instance_uniform_buffers[vulkan_max_frames_in_flight];
VkMeshes                         vulkan_all_meshes;
VkSkeletons                      vulkan_all_skeletons;
GraphicsMeshInstances            graphics_all_mesh_instances;
GraphicsSkeletonInstances        graphics_all_skeleton_instances;
RenderThreadSimulationState      graphics_render_thread_sim_state;
float                            ambient_light_intensity;
DirectionalLight                 directional_light;
VkAnimations                     vulkan_all_animations;

extern XrInstance platform_xr_instance;
extern XrSystemId platform_xr_system_id;

VkMirrorSwapchainContext* vulkan_mirror_swapchain_context;

extern HINSTANCE platform_win32_instance;
extern HWND      platform_win32_mirror_window;

SkeletonInstanceUniformBufferObject current_render_views_skeleton_instances_ubo;  // only used with vulkan_render_xr_views but needs to be initialized

std::string vk_bool_to_str(VkBool32 vk_bool) {
  return (vk_bool == VK_TRUE) ? "VK_TRUE" : "VK_FALSE";
}

VkResult vk_allocate_memory(VkMemoryRequirements const& memory_reqs, VkDeviceMemory* logical_device_memory, VkFlags requirements = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, const void* pNext=nullptr) {
  // the spec only guarantees 4096 simultaneous memory allocations, so partition a single allocation into many objects by using offsets (check limit of a single memory allocation size)
  for (uint32_t i=0; i < vulkan_physical_device_memory_properties.memoryTypeCount; ++i) {
    static_assert(std::is_same<decltype(memory_reqs.memoryTypeBits), uint32_t>::value, "test left shift if bitmask is not 32 bits");
    if (memory_reqs.memoryTypeBits & (1 << i)) {
      if ( (vulkan_physical_device_memory_properties.memoryTypes[i].propertyFlags & requirements) == requirements ) {
        VkMemoryAllocateInfo mem_alloc{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        mem_alloc.pNext           = pNext;
        mem_alloc.allocationSize  = memory_reqs.size;
        mem_alloc.memoryTypeIndex = i;
        return vkAllocateMemory(vulkan_logical_device, &mem_alloc, nullptr, logical_device_memory);
      }
    }
  }
  DEBUG_LOG("memory format not supported\n");
  return VK_ERROR_UNKNOWN;
}

void load_spv_from_binary_file(const std::string& file_path, std::vector<uint32_t>& spv_code) {
  std::ifstream file(file_path, std::ios::ate | std::ios::binary);

  #ifndef NDEBUG
  if (!file.is_open()) {
    DEBUG_LOG("failed to open: %s", file_path.c_str());
  }
  #endif

  size_t file_size = (size_t) file.tellg();

  spv_code.resize( file_size / sizeof(uint32_t) );

  file.seekg(0);
  file.read(reinterpret_cast<char*>(spv_code.data()), file_size);
  file.close();
}

void generate_pipeline_shader_stage_create_info(const std::vector<uint32_t>& spv_code, const VkShaderStageFlagBits& stage, VkPipelineShaderStageCreateInfo& shader_info) {
  shader_info = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
  shader_info.pName = "main";
  shader_info.stage = stage;

  VkShaderModuleCreateInfo mod_info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  mod_info.codeSize = spv_code.size() * sizeof(spv_code[0]);
  mod_info.pCode    = spv_code.data();

  vkCreateShaderModule(vulkan_logical_device, &mod_info, nullptr, &shader_info.module);
}

template <typename uniform_buffer_object_type>
void create_vk_uniform_buffer(VkUniformBuffer* const uniform_buffer) {
  const VkDeviceSize uniform_buffer_object_size = sizeof(uniform_buffer_object_type);

  VkBufferCreateInfo buffer_info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  buffer_info.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
  buffer_info.size  = uniform_buffer_object_size;
  vkCreateBuffer(vulkan_logical_device, &buffer_info, nullptr, &uniform_buffer->buffer);

  VkMemoryRequirements memory_requirements = {};
  vkGetBufferMemoryRequirements(vulkan_logical_device, uniform_buffer->buffer, &memory_requirements);
  vk_allocate_memory(memory_requirements, &uniform_buffer->memory, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  vkBindBufferMemory(vulkan_logical_device, uniform_buffer->buffer, uniform_buffer->memory, 0);
  vkMapMemory(vulkan_logical_device, uniform_buffer->memory, 0, VK_WHOLE_SIZE, 0, &uniform_buffer->mapped_memory);
}

void destroy_vk_uniform_buffer(VkUniformBuffer* const uniform_buffer) {
  vkUnmapMemory(vulkan_logical_device, uniform_buffer->memory);
  vkDestroyBuffer(vulkan_logical_device, uniform_buffer->buffer, nullptr);
  vkFreeMemory(vulkan_logical_device, uniform_buffer->memory, nullptr);
}

void create_vk_xr_swapchain_context(const uint32_t image_count, const XrSwapchainCreateInfo& swapchain_create_info, VkXrSwapchainContext* const swapchain_context) {
  swapchain_context->images.resize(image_count);
  swapchain_context->image_views.resize(image_count);
  swapchain_context->framebuffers.resize(image_count);

  for (uint32_t i=0; i < image_count; ++i) {
    swapchain_context->images[i]        = { XR_TYPE_SWAPCHAIN_IMAGE_VULKAN2_KHR };
    swapchain_context->image_views[i]   = { VK_NULL_HANDLE };
    swapchain_context->framebuffers[i]  = { VK_NULL_HANDLE };
  }

  swapchain_context->image_size         = {swapchain_create_info.width, swapchain_create_info.height};
  swapchain_context->image_color_format = (VkFormat)swapchain_create_info.format;

  VkSemaphoreCreateInfo semaphore_info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
  VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
  fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
  for (uint32_t i=0; i < vulkan_max_frames_in_flight; ++i) {
    vkCreateSemaphore(vulkan_logical_device, &semaphore_info, nullptr, &swapchain_context->render_finished_semaphores[i]);
    vkCreateFence(vulkan_logical_device, &fence_info, nullptr, &swapchain_context->frame_in_flight_fences[i]);
  }

  VkCommandPoolCreateInfo command_pool_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  command_pool_info.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  command_pool_info.queueFamilyIndex = vulkan_queue_family_index;
  vkCreateCommandPool(vulkan_logical_device, &command_pool_info, nullptr, &swapchain_context->command_pool);

  swapchain_context->command_buffers.resize(vulkan_max_frames_in_flight);
  VkCommandBufferAllocateInfo command_buffer_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  command_buffer_info.commandPool        = swapchain_context->command_pool;
  command_buffer_info.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  command_buffer_info.commandBufferCount = vulkan_max_frames_in_flight;
  vkAllocateCommandBuffers(vulkan_logical_device, &command_buffer_info, swapchain_context->command_buffers.data());

  // create depth buffer
  VkMemoryRequirements memory_requirements{};
  {
    VkImageCreateInfo depth_image_info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    depth_image_info.imageType     = VK_IMAGE_TYPE_2D;
    depth_image_info.extent.width  = swapchain_context->image_size.width;
    depth_image_info.extent.height = swapchain_context->image_size.height;
    depth_image_info.extent.depth  = 1;
    depth_image_info.mipLevels     = 1;
    depth_image_info.format        = VkXrSwapchainContext::depth_buffer_format;
    depth_image_info.tiling        = VK_IMAGE_TILING_OPTIMAL;
    depth_image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depth_image_info.usage         = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    depth_image_info.samples       = vulkan_msaa_count;
    depth_image_info.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    depth_image_info.arrayLayers   = 2;
    vkCreateImage(vulkan_logical_device, &depth_image_info, nullptr, &swapchain_context->depth_buffer);

    vkGetImageMemoryRequirements(vulkan_logical_device, swapchain_context->depth_buffer, &memory_requirements);
    vk_allocate_memory(memory_requirements, &swapchain_context->depth_buffer_memory, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkBindImageMemory(vulkan_logical_device, swapchain_context->depth_buffer, swapchain_context->depth_buffer_memory, 0);

    VkImageViewCreateInfo depth_view_info{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    depth_view_info.image                           = swapchain_context->depth_buffer;
    depth_view_info.viewType                        = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    depth_view_info.format                          = VkXrSwapchainContext::depth_buffer_format;
    depth_view_info.components.r                    = VK_COMPONENT_SWIZZLE_R;
    depth_view_info.components.g                    = VK_COMPONENT_SWIZZLE_G;
    depth_view_info.components.b                    = VK_COMPONENT_SWIZZLE_B;
    depth_view_info.components.a                    = VK_COMPONENT_SWIZZLE_A;
    depth_view_info.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_DEPTH_BIT;
    depth_view_info.subresourceRange.baseMipLevel   = 0;
    depth_view_info.subresourceRange.levelCount     = 1;
    depth_view_info.subresourceRange.baseArrayLayer = 0;
    depth_view_info.subresourceRange.layerCount     = 2;
    vkCreateImageView(vulkan_logical_device, &depth_view_info, nullptr, &swapchain_context->depth_buffer_view);
  }

  // create color buffer
  {
    VkImageCreateInfo color_buffer_info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    color_buffer_info.imageType     = VK_IMAGE_TYPE_2D;
    color_buffer_info.extent.width  = swapchain_context->image_size.width;
    color_buffer_info.extent.height = swapchain_context->image_size.height;
    color_buffer_info.extent.depth  = 1;
    color_buffer_info.mipLevels     = 1;
    color_buffer_info.format        = swapchain_context->image_color_format;
    color_buffer_info.tiling        = VK_IMAGE_TILING_OPTIMAL;
    color_buffer_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color_buffer_info.usage         = VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    color_buffer_info.samples       = vulkan_msaa_count;
    color_buffer_info.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    color_buffer_info.arrayLayers   = 2;
    vkCreateImage(vulkan_logical_device, &color_buffer_info, nullptr, &swapchain_context->color_buffer);

    vkGetImageMemoryRequirements(vulkan_logical_device, swapchain_context->color_buffer, &memory_requirements);
    vk_allocate_memory(memory_requirements, &swapchain_context->color_buffer_memory, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkBindImageMemory(vulkan_logical_device, swapchain_context->color_buffer, swapchain_context->color_buffer_memory, 0);

    VkImageViewCreateInfo color_view_info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    color_view_info.image                           = swapchain_context->color_buffer;
    color_view_info.viewType                        = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    color_view_info.format                          = swapchain_context->image_color_format;
    color_view_info.components.r                    = VK_COMPONENT_SWIZZLE_R;
    color_view_info.components.g                    = VK_COMPONENT_SWIZZLE_G;
    color_view_info.components.b                    = VK_COMPONENT_SWIZZLE_B;
    color_view_info.components.a                    = VK_COMPONENT_SWIZZLE_A;
    color_view_info.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    color_view_info.subresourceRange.baseMipLevel   = 0;
    color_view_info.subresourceRange.levelCount     = 1;
    color_view_info.subresourceRange.baseArrayLayer = 0;
    color_view_info.subresourceRange.layerCount     = 2;
    vkCreateImageView(vulkan_logical_device, &color_view_info, nullptr, &swapchain_context->color_buffer_view);
  }

  // create render pass
  {
    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;

    VkAttachmentReference color_reference         = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkAttachmentReference depth_reference         = { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
    VkAttachmentReference color_resolve_reference = { 2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };

    VkAttachmentDescription attachments[3] = {};

    VkRenderPassCreateInfo render_pass_info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    render_pass_info.attachmentCount = static_cast<uint32_t>(std::size(attachments));
    render_pass_info.pAttachments    = attachments;
    render_pass_info.subpassCount    = 1;
    render_pass_info.pSubpasses      = &subpass;

    const uint32_t view_mask        = 0b00000011; // broadcast rendering to first and second view
    const uint32_t correlation_mask = 0b00000011; // indicate first and second views may be rendered concurrently

    VkRenderPassMultiviewCreateInfo multiview_create_info{};
    multiview_create_info.sType                = VK_STRUCTURE_TYPE_RENDER_PASS_MULTIVIEW_CREATE_INFO;
    multiview_create_info.subpassCount         = 1;
    multiview_create_info.pViewMasks           = &view_mask;
    multiview_create_info.correlationMaskCount = 1;
    multiview_create_info.pCorrelationMasks    = &correlation_mask;
    render_pass_info.pNext                     = &multiview_create_info;

    attachments[color_reference.attachment].format         = swapchain_context->image_color_format;
    attachments[color_reference.attachment].samples        = vulkan_msaa_count;
    attachments[color_reference.attachment].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[color_reference.attachment].storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[color_reference.attachment].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[color_reference.attachment].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[color_reference.attachment].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[color_reference.attachment].finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    attachments[depth_reference.attachment].format         = VkXrSwapchainContext::depth_buffer_format;
    attachments[depth_reference.attachment].samples        = vulkan_msaa_count;
    attachments[depth_reference.attachment].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[depth_reference.attachment].storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[depth_reference.attachment].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[depth_reference.attachment].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[depth_reference.attachment].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[depth_reference.attachment].finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    attachments[color_resolve_reference.attachment].format         = swapchain_context->image_color_format;
    attachments[color_resolve_reference.attachment].samples        = VK_SAMPLE_COUNT_1_BIT;
    attachments[color_resolve_reference.attachment].loadOp         = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[color_resolve_reference.attachment].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[color_resolve_reference.attachment].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[color_resolve_reference.attachment].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[color_resolve_reference.attachment].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[color_resolve_reference.attachment].finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    subpass.colorAttachmentCount    = 1;
    subpass.pColorAttachments       = &color_reference;
    subpass.pDepthStencilAttachment = &depth_reference;
    subpass.pResolveAttachments     = &color_resolve_reference;

    VkSubpassDependency dependencies[2];
    dependencies[0].dstSubpass      = 0;
    dependencies[0].dstStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[0].dstAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
    dependencies[0].srcSubpass      = VK_SUBPASS_EXTERNAL;
    dependencies[0].srcStageMask    = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    dependencies[0].srcAccessMask   = VK_ACCESS_MEMORY_READ_BIT;

    dependencies[1].dstSubpass      = VK_SUBPASS_EXTERNAL;
    dependencies[1].dstStageMask    = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    dependencies[1].dstAccessMask   = VK_ACCESS_MEMORY_READ_BIT;
    dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
    dependencies[1].srcSubpass      = 0;
    dependencies[1].srcStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[1].srcAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    render_pass_info.dependencyCount = (uint32_t)std::size(dependencies);
    render_pass_info.pDependencies   = dependencies;

    vkCreateRenderPass(vulkan_logical_device, &render_pass_info, nullptr, &swapchain_context->render_pass_pattern);
  }

  // create pipeline
  {
    std::vector<uint32_t> mesh_instance_vertex_spv = {
    #ifndef NDEBUG
      #include "../shaders/glsl/bin/debug/mesh_instance.vert.spv"
    #else
      #include "../shaders/glsl/bin/release/mesh_instance.vert.spv"
    #endif
    };
    std::vector<uint32_t> mesh_instance_fragment_spv = {
    #ifndef NDEBUG
      #include "../shaders/glsl/bin/debug/mesh_instance.frag.spv"
    #else
      #include "../shaders/glsl/bin/release/mesh_instance.frag.spv"
    #endif
    };

    VkPipelineShaderStageCreateInfo vertex_shader_info;
    VkPipelineShaderStageCreateInfo fragment_shader_info;
    generate_pipeline_shader_stage_create_info(mesh_instance_vertex_spv, VK_SHADER_STAGE_VERTEX_BIT, vertex_shader_info);
    generate_pipeline_shader_stage_create_info(mesh_instance_fragment_spv, VK_SHADER_STAGE_FRAGMENT_BIT, fragment_shader_info);

    VkPipelineShaderStageCreateInfo shader_stages[] = { vertex_shader_info, fragment_shader_info };

    //VkPrimitiveTopology primitive_topology{VK_PRIMITIVE_TOPOLOGY_POINT_LIST};
    VkPrimitiveTopology primitive_topology{VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    std::vector<VkDynamicState> dynamic_states;

    VkPipelineDynamicStateCreateInfo dynamic_state_info{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic_state_info.dynamicStateCount = (uint32_t)dynamic_states.size();
    dynamic_state_info.pDynamicStates    = dynamic_states.data();

    VkVertexInputBindingDescription vertex_binding_description{};
    vertex_binding_description.binding   = 0;
    vertex_binding_description.stride    = sizeof(GraphicsVertex);
    vertex_binding_description.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputBindingDescription instance_binding_description{};
    instance_binding_description.binding   = 1;
    instance_binding_description.stride    = sizeof(GraphicsMeshInstances::BufferData);
    instance_binding_description.inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

    VkVertexInputBindingDescription vertex_binding_descriptions[] = { vertex_binding_description, instance_binding_description };

    VkVertexInputAttributeDescription vertex_attributes[] = {
      { 0,  0, VK_FORMAT_R32G32B32_SFLOAT,    offsetof(GraphicsVertex, position)                                 },
      { 1,  0, VK_FORMAT_R32G32B32_SFLOAT,    offsetof(GraphicsVertex, normal)                                   },
      { 2,  0, VK_FORMAT_R32G32_SFLOAT,       offsetof(GraphicsVertex, texture_coordinates)                      },
      { 3,  0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(GraphicsVertex, joint_indices)                            },
      { 4,  0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(GraphicsVertex, joint_weights)                            },
      { 5,  1, VK_FORMAT_R32G32B32_SFLOAT,    offsetof(GraphicsMeshInstances::BufferData, model_matrix_column_0) },
      { 6,  1, VK_FORMAT_R32G32B32_SFLOAT,    offsetof(GraphicsMeshInstances::BufferData, model_matrix_column_1) },
      { 7,  1, VK_FORMAT_R32G32B32_SFLOAT,    offsetof(GraphicsMeshInstances::BufferData, model_matrix_column_2) },
      { 8,  1, VK_FORMAT_R32G32B32_SFLOAT,    offsetof(GraphicsMeshInstances::BufferData, model_matrix_column_3) },
      { 9,  1, VK_FORMAT_R32_UINT,            offsetof(GraphicsMeshInstances::BufferData, material_id)           },
      { 10, 1, VK_FORMAT_R32_UINT,            offsetof(GraphicsMeshInstances::BufferData, joint_index)           }
    };

    VkPipelineVertexInputStateCreateInfo vertex_input_state_info{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertex_input_state_info.vertexBindingDescriptionCount   = static_cast<uint32_t>(std::size(vertex_binding_descriptions));
    vertex_input_state_info.pVertexBindingDescriptions      = vertex_binding_descriptions;
    vertex_input_state_info.vertexAttributeDescriptionCount = static_cast<uint32_t>(std::size(vertex_attributes));
    vertex_input_state_info.pVertexAttributeDescriptions    = vertex_attributes;

    VkPipelineInputAssemblyStateCreateInfo input_assembly_state_info{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    input_assembly_state_info.primitiveRestartEnable = VK_FALSE;
    input_assembly_state_info.topology               = primitive_topology;

    VkPipelineRasterizationStateCreateInfo rasterization_state_info{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rasterization_state_info.depthClampEnable        = VK_FALSE;
    rasterization_state_info.rasterizerDiscardEnable = VK_FALSE;
    rasterization_state_info.polygonMode             = VK_POLYGON_MODE_FILL;
    rasterization_state_info.cullMode                = VK_CULL_MODE_BACK_BIT;
    rasterization_state_info.frontFace               = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterization_state_info.depthBiasEnable         = VK_FALSE;
    rasterization_state_info.depthBiasConstantFactor = 0.0f;
    rasterization_state_info.depthBiasClamp          = 0.0f;
    rasterization_state_info.depthBiasSlopeFactor    = 0.0f;
    rasterization_state_info.lineWidth               = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample_state_info{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample_state_info.rasterizationSamples = vulkan_msaa_count;
    multisample_state_info.sampleShadingEnable  = VK_TRUE;
    multisample_state_info.minSampleShading     = 0.25f;

    VkPipelineDepthStencilStateCreateInfo depth_stencil_state_info{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depth_stencil_state_info.depthTestEnable       = VK_TRUE;
    depth_stencil_state_info.depthWriteEnable      = VK_TRUE;
    depth_stencil_state_info.depthCompareOp        = VK_COMPARE_OP_LESS;
    depth_stencil_state_info.depthBoundsTestEnable = VK_FALSE;
    depth_stencil_state_info.minDepthBounds        = 0.0f;
    depth_stencil_state_info.maxDepthBounds        = 1.0f;
    depth_stencil_state_info.stencilTestEnable     = VK_FALSE;
    depth_stencil_state_info.front.failOp          = VK_STENCIL_OP_KEEP;
    depth_stencil_state_info.front.passOp          = VK_STENCIL_OP_KEEP;
    depth_stencil_state_info.front.depthFailOp     = VK_STENCIL_OP_KEEP;
    depth_stencil_state_info.front.compareOp       = VK_COMPARE_OP_ALWAYS;
    depth_stencil_state_info.back                  = depth_stencil_state_info.front;

    VkPipelineColorBlendAttachmentState color_blend_attachment_state{};
    color_blend_attachment_state.blendEnable         = VK_FALSE;
    color_blend_attachment_state.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    color_blend_attachment_state.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    color_blend_attachment_state.colorBlendOp        = VK_BLEND_OP_ADD;
    color_blend_attachment_state.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    color_blend_attachment_state.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    color_blend_attachment_state.alphaBlendOp        = VK_BLEND_OP_ADD;
    color_blend_attachment_state.colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo color_blend_state_info{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    color_blend_state_info.logicOpEnable     = VK_FALSE;
    color_blend_state_info.logicOp           = VK_LOGIC_OP_NO_OP;
    color_blend_state_info.attachmentCount   = 1;
    color_blend_state_info.pAttachments      = &color_blend_attachment_state;
    color_blend_state_info.blendConstants[0] = 1.0f;
    color_blend_state_info.blendConstants[1] = 1.0f;
    color_blend_state_info.blendConstants[2] = 1.0f;
    color_blend_state_info.blendConstants[3] = 1.0f;

    VkPushConstantRange push_constant_range = {};
    push_constant_range.stageFlags          = VK_SHADER_STAGE_VERTEX_BIT;
    push_constant_range.offset              = 0;
    push_constant_range.size                = 128;  // spec guaranteed minimum is 128

    VkPipelineLayoutCreateInfo pipeline_layout_create_info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipeline_layout_create_info.pushConstantRangeCount = 1;
    pipeline_layout_create_info.pPushConstantRanges    = &push_constant_range;
    pipeline_layout_create_info.setLayoutCount         = 1;
    pipeline_layout_create_info.pSetLayouts            = &vulkan_descriptor_set_layout;
    vkCreatePipelineLayout(vulkan_logical_device, &pipeline_layout_create_info, nullptr, &swapchain_context->pipeline_layout);

    VkViewport viewport = { 0.0f, 0.0f, (float)swapchain_context->image_size.width, (float)swapchain_context->image_size.height, 0.0f, 1.0f };
    VkRect2D scissor    = { {0,0}, swapchain_context->image_size };

    VkPipelineViewportStateCreateInfo viewport_state_info{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport_state_info.viewportCount = 1;
    viewport_state_info.pViewports    = &viewport;
    viewport_state_info.scissorCount  = 1;
    viewport_state_info.pScissors     = &scissor;

    VkGraphicsPipelineCreateInfo pipeline_info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipeline_info.stageCount          = static_cast<uint32_t>(std::size(shader_stages));
    pipeline_info.pStages             = shader_stages;
    pipeline_info.pVertexInputState   = &vertex_input_state_info;
    pipeline_info.pInputAssemblyState = &input_assembly_state_info;
    pipeline_info.pTessellationState  = nullptr;
    pipeline_info.pViewportState      = &viewport_state_info;
    pipeline_info.pRasterizationState = &rasterization_state_info;
    pipeline_info.pMultisampleState   = &multisample_state_info;
    pipeline_info.pDepthStencilState  = &depth_stencil_state_info;
    pipeline_info.pColorBlendState    = &color_blend_state_info;
    pipeline_info.layout              = swapchain_context->pipeline_layout;
    pipeline_info.renderPass          = swapchain_context->render_pass_pattern;
    pipeline_info.subpass             = 0;
    if (dynamic_state_info.dynamicStateCount > 0) {
      pipeline_info.pDynamicState = &dynamic_state_info;
    }
    vkCreateGraphicsPipelines(vulkan_logical_device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &swapchain_context->pipeline);

    vkDestroyShaderModule(vulkan_logical_device, vertex_shader_info.module, nullptr);
    vkDestroyShaderModule(vulkan_logical_device, fragment_shader_info.module, nullptr);
  }
}

void destroy_vk_xr_swapchain_image_context(VkXrSwapchainContext* const swapchain_context) {
  for (auto framebuffer : swapchain_context->framebuffers) {
    vkDestroyFramebuffer(vulkan_logical_device, framebuffer, nullptr);
  }
  for (auto image_view : swapchain_context->image_views) {
    vkDestroyImageView(vulkan_logical_device, image_view, nullptr);
  }

  vkDestroyImageView(vulkan_logical_device, swapchain_context->depth_buffer_view, nullptr);
  vkDestroyImage(vulkan_logical_device, swapchain_context->depth_buffer, nullptr);
  vkFreeMemory(vulkan_logical_device, swapchain_context->depth_buffer_memory, nullptr);

  vkDestroyImageView(vulkan_logical_device, swapchain_context->color_buffer_view, nullptr);
  vkDestroyImage(vulkan_logical_device, swapchain_context->color_buffer, nullptr);
  vkFreeMemory(vulkan_logical_device, swapchain_context->color_buffer_memory, nullptr);

  vkDestroyPipeline(vulkan_logical_device, swapchain_context->pipeline, nullptr);
  vkDestroyRenderPass(vulkan_logical_device, swapchain_context->render_pass_pattern, nullptr);

  vkDestroyPipelineLayout(vulkan_logical_device, swapchain_context->pipeline_layout, nullptr);

  for (uint32_t i = 0; i < vulkan_max_frames_in_flight; ++i) {
    vkDestroySemaphore(vulkan_logical_device, vulkan_xr_swapchain_context->render_finished_semaphores[i], nullptr);
    vkDestroyFence(vulkan_logical_device, vulkan_xr_swapchain_context->frame_in_flight_fences[i], nullptr);
  }
  vkDestroyCommandPool(vulkan_logical_device, vulkan_xr_swapchain_context->command_pool, nullptr);
}

std::string vk_result_to_str(VkResult vk_result) {
  switch (vk_result) {
    case VK_SUCCESS:
      return "VK_SUCCESS";
    case VK_NOT_READY:
      return "VK_NOT_READY";
    case VK_TIMEOUT:
      return "VK_TIMEOUT";
    case VK_EVENT_SET:
      return "VK_EVENT_SET";
    case VK_EVENT_RESET:
      return "VK_EVENT_RESET";
    case VK_INCOMPLETE:
      return "VK_INCOMPLETE";
    case VK_ERROR_OUT_OF_HOST_MEMORY:
      return "VK_ERROR_OUT_OF_HOST_MEMORY";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY:
      return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case VK_ERROR_INITIALIZATION_FAILED:
      return "VK_ERROR_INITIALIZATION_FAILED";
    case VK_ERROR_DEVICE_LOST:
      return "VK_ERROR_DEVICE_LOST";
    case VK_ERROR_MEMORY_MAP_FAILED:
      return "VK_ERROR_MEMORY_MAP_FAILED";
    case VK_ERROR_LAYER_NOT_PRESENT:
      return "VK_ERROR_LAYER_NOT_PRESENT";
    case VK_ERROR_EXTENSION_NOT_PRESENT:
      return "VK_ERROR_EXTENSION_NOT_PRESENT";
    case VK_ERROR_FEATURE_NOT_PRESENT:
      return "VK_ERROR_FEATURE_NOT_PRESENT";
    case VK_ERROR_INCOMPATIBLE_DRIVER:
      return "VK_ERROR_INCOMPATIBLE_DRIVER";
    case VK_ERROR_TOO_MANY_OBJECTS:
      return "VK_ERROR_TOO_MANY_OBJECTS";
    case VK_ERROR_FORMAT_NOT_SUPPORTED:
      return "VK_ERROR_FORMAT_NOT_SUPPORTED";
    case VK_ERROR_SURFACE_LOST_KHR:
      return "VK_ERROR_SURFACE_LOST_KHR";
    case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR:
      return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
    case VK_SUBOPTIMAL_KHR:
      return "VK_SUBOPTIMAL_KHR";
    case VK_ERROR_OUT_OF_DATE_KHR:
      return "VK_ERROR_OUT_OF_DATE_KHR";
    case VK_ERROR_INCOMPATIBLE_DISPLAY_KHR:
      return "VK_ERROR_INCOMPATIBLE_DISPLAY_KHR";
    case VK_ERROR_VALIDATION_FAILED_EXT:
      return "VK_ERROR_VALIDATION_FAILED_EXT";
    case VK_ERROR_INVALID_SHADER_NV:
      return "VK_ERROR_INVALID_SHADER_NV";
    default:
      return "did not find vk_result type";
  }
}

bool check_vk_result(VkResult vk_result) {
  if (vk_result == VK_SUCCESS) {
    return true;
  } else {
    DEBUG_LOG(vk_result_to_str(vk_result).data());
    DEBUG_LOG("\n");
    return false;
  }
}

VkVertexBuffer* create_vk_vertex_buffer(const size_t max_indices_count, const size_t max_vertices_count, VkVertexBuffer* const vertex_buffer) {
  VkBufferCreateInfo buffer_info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  VkMemoryRequirements memory_requirements = {};

  vertex_buffer->max_indices_count  = max_indices_count;
  vertex_buffer->max_vertices_count = max_vertices_count;
  vertex_buffer->size               = ( sizeof(GraphicsIndex) * max_indices_count ) + ( sizeof(GraphicsVertex) * max_vertices_count ) + alignof(std::max_align_t);  // alignof(std::max_align_t) is to make sure enough bytes for padding between indices and vertices

  VkFlags memory_type_flags              = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
  VkBufferUsageFlags index_buffer_usage  = VK_BUFFER_USAGE_INDEX_BUFFER_BIT  | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  VkBufferUsageFlags vertex_buffer_usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

  buffer_info.usage = index_buffer_usage | vertex_buffer_usage;
  buffer_info.size  = vertex_buffer->size;
  vkCreateBuffer(vulkan_logical_device, &buffer_info, nullptr, &vertex_buffer->buffer);
  vkGetBufferMemoryRequirements(vulkan_logical_device, vertex_buffer->buffer, &memory_requirements);
  vk_allocate_memory(memory_requirements, &vertex_buffer->memory, memory_type_flags);
  vkBindBufferMemory(vulkan_logical_device, vertex_buffer->buffer, vertex_buffer->memory, 0);

  VkBufferCreateInfo   staging_buffer_info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  VkMemoryRequirements staging_buffer_memory_requirements = {};
  VkFlags              staging_buffer_memory_type_flags   = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

  staging_buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
  staging_buffer_info.size  = vertex_buffer->size;

  vkCreateBuffer(vulkan_logical_device, &staging_buffer_info, nullptr, &vertex_buffer->staging_buffer);
  vkGetBufferMemoryRequirements(vulkan_logical_device, vertex_buffer->staging_buffer, &staging_buffer_memory_requirements);
  vk_allocate_memory(staging_buffer_memory_requirements, &vertex_buffer->staging_buffer_memory, staging_buffer_memory_type_flags);
  vkBindBufferMemory(vulkan_logical_device, vertex_buffer->staging_buffer, vertex_buffer->staging_buffer_memory, 0);

  vkMapMemory(vulkan_logical_device, vertex_buffer->staging_buffer_memory, 0, VK_WHOLE_SIZE, 0, &vertex_buffer->mapped_memory);

  size_t indices_size          = sizeof(GraphicsIndex) * vertex_buffer->max_indices_count;
  size_t vertices_size         = sizeof(GraphicsVertex) * vertex_buffer->max_vertices_count;
  void* end_of_index_memory    = static_cast<std::byte*>(vertex_buffer->mapped_memory) + indices_size;
  size_t remaining_buffer_size = vertex_buffer->size - indices_size;
  void* vertex_memory          = std::align(alignof(GraphicsVertex), vertices_size, end_of_index_memory, remaining_buffer_size);
  vertex_buffer->vertices_partition_offset = static_cast<std::byte*>(vertex_memory) - static_cast<std::byte*>(vertex_buffer->mapped_memory);

  return vertex_buffer;
}

void destroy_vk_vertex_buffer(VkVertexBuffer* const vertex_buffer) {
  vkUnmapMemory(vulkan_logical_device, vertex_buffer->staging_buffer_memory);
  vkDestroyBuffer(vulkan_logical_device, vertex_buffer->staging_buffer, nullptr);
  vkFreeMemory(vulkan_logical_device, vertex_buffer->staging_buffer_memory, nullptr);

  vkDestroyBuffer(vulkan_logical_device, vertex_buffer->buffer, nullptr);
  vkFreeMemory(vulkan_logical_device, vertex_buffer->memory, nullptr);
}

VkInstanceBuffer* create_vk_instance_buffer(const size_t max_instance_count, VkInstanceBuffer* const instance_buffer) {
  VkBufferCreateInfo buffer_info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  VkMemoryRequirements memory_requirements = {};

  instance_buffer->max_instance_count = max_instance_count;
  instance_buffer->size               = sizeof(GraphicsMeshInstances::BufferData) * instance_buffer->max_instance_count;

  VkFlags memory_type_flags                = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
  VkBufferUsageFlags instance_buffer_usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

  buffer_info.usage = instance_buffer_usage;
  buffer_info.size  = instance_buffer->size;
  vkCreateBuffer(vulkan_logical_device, &buffer_info, nullptr, &instance_buffer->buffer);
  vkGetBufferMemoryRequirements(vulkan_logical_device, instance_buffer->buffer, &memory_requirements);
  vk_allocate_memory(memory_requirements, &instance_buffer->memory, memory_type_flags);
  vkBindBufferMemory(vulkan_logical_device, instance_buffer->buffer, instance_buffer->memory, 0);

  VkBufferCreateInfo   staging_buffer_info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  VkMemoryRequirements staging_buffer_memory_requirements = {};
  VkFlags              staging_buffer_memory_type_flags   = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

  staging_buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
  staging_buffer_info.size  = instance_buffer->size;

  vkCreateBuffer(vulkan_logical_device, &staging_buffer_info, nullptr, &instance_buffer->staging_buffer);
  vkGetBufferMemoryRequirements(vulkan_logical_device, instance_buffer->staging_buffer, &staging_buffer_memory_requirements);
  vk_allocate_memory(staging_buffer_memory_requirements, &instance_buffer->staging_buffer_memory, staging_buffer_memory_type_flags);
  vkBindBufferMemory(vulkan_logical_device, instance_buffer->staging_buffer, instance_buffer->staging_buffer_memory, 0);

  vkMapMemory(vulkan_logical_device, instance_buffer->staging_buffer_memory, 0, VK_WHOLE_SIZE, 0, &instance_buffer->mapped_memory);

  return instance_buffer;
}

void destroy_vk_instance_buffer(VkInstanceBuffer* const instance_buffer) {
  vkUnmapMemory(vulkan_logical_device, instance_buffer->staging_buffer_memory);
  vkDestroyBuffer(vulkan_logical_device, instance_buffer->staging_buffer, nullptr);
  vkFreeMemory(vulkan_logical_device, instance_buffer->staging_buffer_memory, nullptr);

  vkDestroyBuffer(vulkan_logical_device, instance_buffer->buffer, nullptr);
  vkFreeMemory(vulkan_logical_device, instance_buffer->memory, nullptr);
}

VkCommandBuffer vulkan_begin_temporary_command_buffer() {
  VkCommandBufferAllocateInfo command_buffer_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  command_buffer_info.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  command_buffer_info.commandPool        = vulkan_temporary_command_pool;
  command_buffer_info.commandBufferCount = 1;

  VkCommandBuffer tmp_command_buffer;
  vkAllocateCommandBuffers(vulkan_logical_device, &command_buffer_info, &tmp_command_buffer);
  
  VkCommandBufferBeginInfo begin_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(tmp_command_buffer, &begin_info);

  return tmp_command_buffer;
}

void vulkan_end_then_submit_temporary_command_buffer(VkCommandBuffer tmp_command_buffer) {
  vkEndCommandBuffer(tmp_command_buffer);

  VkSubmitInfo submit_info{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  submit_info.commandBufferCount = 1;
  submit_info.pCommandBuffers    = &tmp_command_buffer;

  VkFence temporary_command_buffer_submit_fence{VK_NULL_HANDLE};
  VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
  vkCreateFence(vulkan_logical_device, &fence_info, nullptr, &temporary_command_buffer_submit_fence);

  vkQueueSubmit(vulkan_queue, 1, &submit_info, temporary_command_buffer_submit_fence);
  vkWaitForFences(vulkan_logical_device, 1, &temporary_command_buffer_submit_fence, VK_TRUE, UINT64_MAX);
  vkDestroyFence(vulkan_logical_device, temporary_command_buffer_submit_fence, nullptr);

  vkFreeCommandBuffers(vulkan_logical_device, vulkan_temporary_command_pool, 1, &tmp_command_buffer);
}

void VkVertexBuffer::update(const GraphicsIndex* indices, const size_t indices_count, const GraphicsVertex* vertices, const size_t vertices_count, const size_t indices_offset, const size_t vertices_offset) {
  assert( (indices_count > 0) && (vertices_count > 0) );
  assert( indices && vertices );
  assert( (indices_count + indices_offset)   < this->max_indices_count );
  assert( (vertices_count + vertices_offset) < this->max_indices_count );

  const size_t indices_size   = sizeof(GraphicsIndex)  * indices_count;
  const size_t vertices_size  = sizeof(GraphicsVertex) * vertices_count;
  void* indices_memory        = static_cast<std::byte*>(this->mapped_memory) + (indices_offset * sizeof(GraphicsIndex));
  void* vertices_memory       = (static_cast<std::byte*>(this->mapped_memory) + this->vertices_partition_offset) + (vertices_offset * sizeof(GraphicsVertex));

  memcpy(indices_memory, indices, indices_size);
  memcpy(vertices_memory, vertices, vertices_size);

  const VkDeviceSize indices_buffer_offset  = static_cast<std::byte*>(indices_memory)  - static_cast<std::byte*>(this->mapped_memory);
  const VkDeviceSize vertices_buffer_offset = static_cast<std::byte*>(vertices_memory) - static_cast<std::byte*>(this->mapped_memory);

  VkCommandBuffer tmp_command_buffer = vulkan_begin_temporary_command_buffer();

  VkBufferMemoryBarrier indices_copy_buffer_barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
  indices_copy_buffer_barrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
  indices_copy_buffer_barrier.dstAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
  indices_copy_buffer_barrier.buffer        = this->buffer;
  indices_copy_buffer_barrier.offset        = indices_buffer_offset;
  indices_copy_buffer_barrier.size          = indices_size;

  VkBufferMemoryBarrier vertices_copy_buffer_barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
  vertices_copy_buffer_barrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
  vertices_copy_buffer_barrier.dstAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
  vertices_copy_buffer_barrier.buffer        = this->buffer;
  vertices_copy_buffer_barrier.offset        = vertices_buffer_offset;
  vertices_copy_buffer_barrier.size          = vertices_size;

  VkBufferMemoryBarrier copy_buffer_barriers[2] = { indices_copy_buffer_barrier, vertices_copy_buffer_barrier };

  vkCmdPipelineBarrier(tmp_command_buffer, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, static_cast<uint32_t>(std::size(copy_buffer_barriers)), copy_buffer_barriers, 0, nullptr);

  VkBufferCopy copy_info[2];
  copy_info[0] = { indices_buffer_offset,  indices_buffer_offset,  static_cast<VkDeviceSize>(indices_size)  };
  copy_info[1] = { vertices_buffer_offset, vertices_buffer_offset, static_cast<VkDeviceSize>(vertices_size) };
  vkCmdCopyBuffer(tmp_command_buffer, this->staging_buffer, this->buffer, static_cast<uint32_t>(std::size(copy_info)), copy_info);

  vulkan_end_then_submit_temporary_command_buffer(tmp_command_buffer);
}

void VkInstanceBuffer::update(const VkCommandBuffer& command_buffer, const GraphicsMeshInstances::BufferData* instances, const size_t instance_count, const size_t instance_offset) {
  assert(instance_count > 0);
  assert(instances);
  assert( (instance_count + instance_offset) < this->max_instance_count );

  const size_t instances_size = sizeof(GraphicsMeshInstances::BufferData) * instance_count;
  void* instances_memory      = static_cast<std::byte*>(this->mapped_memory) + (instance_offset * sizeof(GraphicsMeshInstances::BufferData));
  memcpy(instances_memory, instances, instances_size);

  const VkDeviceSize instances_buffer_offset = static_cast<std::byte*>(instances_memory) - static_cast<std::byte*>(this->mapped_memory);

  VkBufferMemoryBarrier instances_copy_buffer_barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
  instances_copy_buffer_barrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
  instances_copy_buffer_barrier.dstAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
  instances_copy_buffer_barrier.buffer        = this->buffer;
  instances_copy_buffer_barrier.offset        = instances_buffer_offset;
  instances_copy_buffer_barrier.size          = instances_size;

  vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 1, &instances_copy_buffer_barrier, 0, nullptr);

  VkBufferCopy copy_info{ instances_buffer_offset, instances_buffer_offset, static_cast<VkDeviceSize>(instances_size) };
  vkCmdCopyBuffer(command_buffer, this->staging_buffer, this->buffer, 1, &copy_info);
}

void VkXrSwapchainContext::bind_render_target(const uint32_t image_index, VkRenderPassBeginInfo* const render_pass_begin_info) {
  if (framebuffers[image_index] == VK_NULL_HANDLE) {
    VkImageView attachments[3];

    attachments[0] = color_buffer_view;
    attachments[1] = depth_buffer_view;

    VkImageViewCreateInfo color_resolve_view_info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    color_resolve_view_info.image                           = images[image_index].image;
    color_resolve_view_info.viewType                        = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    color_resolve_view_info.format                          = image_color_format;
    color_resolve_view_info.components.r                    = VK_COMPONENT_SWIZZLE_R;
    color_resolve_view_info.components.g                    = VK_COMPONENT_SWIZZLE_G;
    color_resolve_view_info.components.b                    = VK_COMPONENT_SWIZZLE_B;
    color_resolve_view_info.components.a                    = VK_COMPONENT_SWIZZLE_A;
    color_resolve_view_info.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    color_resolve_view_info.subresourceRange.baseMipLevel   = 0;
    color_resolve_view_info.subresourceRange.levelCount     = 1;
    color_resolve_view_info.subresourceRange.baseArrayLayer = 0;
    color_resolve_view_info.subresourceRange.layerCount     = 2;
    vkCreateImageView(vulkan_logical_device, &color_resolve_view_info, nullptr, &image_views[image_index]);
    attachments[2] = image_views[image_index];

    VkFramebufferCreateInfo framebuffer_info{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
    framebuffer_info.renderPass      = render_pass_pattern;
    framebuffer_info.attachmentCount = static_cast<uint32_t>(std::size(attachments));
    framebuffer_info.pAttachments    = attachments;
    framebuffer_info.width           = image_size.width;
    framebuffer_info.height          = image_size.height;
    framebuffer_info.layers          = 1;
    vkCreateFramebuffer(vulkan_logical_device, &framebuffer_info, nullptr, &framebuffers[image_index]);
  }

  render_pass_begin_info->renderPass        = render_pass_pattern;
  render_pass_begin_info->framebuffer       = framebuffers[image_index];
  render_pass_begin_info->renderArea.offset = { 0, 0 };
  render_pass_begin_info->renderArea.extent = image_size;
}

void create_vk_texture_array(const VkFormat& texture_format, const uint32_t layered_image_width, const uint32_t layered_image_height, const uint32_t layered_image_mip_level_count, const uint32_t max_texture_count, VkTextureArray* const texture_array) {
  texture_array->layered_image_width           = layered_image_width;
  texture_array->layered_image_height          = layered_image_height;
  texture_array->layered_image_mip_level_count = layered_image_mip_level_count;
  texture_array->max_texture_count             = max_texture_count;
  texture_array->format                        = texture_format;
  
  uint32_t format_bytes_per_pixel;
  switch(texture_array->format) {
    case VK_FORMAT_R8G8B8A8_SRGB : {
      format_bytes_per_pixel = 4;
      break;
    }
  }
  texture_array->texture_size = layered_image_width * layered_image_height * format_bytes_per_pixel;

  assert(vulkan_physical_device_properties.limits.maxImageArrayLayers >= texture_array->max_texture_count);

  VkImageCreateInfo image_info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  image_info.imageType     = VK_IMAGE_TYPE_2D;
  image_info.extent.width  = texture_array->layered_image_width;
  image_info.extent.height = texture_array->layered_image_height;
  image_info.extent.depth  = 1;
  image_info.mipLevels     = texture_array->layered_image_mip_level_count;
  image_info.arrayLayers   = texture_array->max_texture_count;
  image_info.format        = texture_array->format;
  image_info.tiling        = VK_IMAGE_TILING_OPTIMAL;
  image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  image_info.usage         = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
  image_info.samples       = VK_SAMPLE_COUNT_1_BIT;
  image_info.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
  vkCreateImage(vulkan_logical_device, &image_info, nullptr, &texture_array->layered_image);

  VkMemoryRequirements image_memory_requirements;
  vkGetImageMemoryRequirements(vulkan_logical_device, texture_array->layered_image, &image_memory_requirements);
  vk_allocate_memory(image_memory_requirements, &texture_array->layered_image_memory, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  vkBindImageMemory(vulkan_logical_device, texture_array->layered_image, texture_array->layered_image_memory, 0);

  VkBufferCreateInfo   texture_staging_buffer_info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  VkMemoryRequirements texture_staging_memory_requirements = {};
  VkFlags              texture_staging_memory_type_flags   = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

  texture_staging_buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
  texture_staging_buffer_info.size  = texture_array->texture_size;

  vkCreateBuffer(vulkan_logical_device, &texture_staging_buffer_info, nullptr, &texture_array->texture_staging_buffer);
  vkGetBufferMemoryRequirements(vulkan_logical_device, texture_array->texture_staging_buffer, &texture_staging_memory_requirements);
  vk_allocate_memory(texture_staging_memory_requirements, &texture_array->texture_staging_buffer_memory, texture_staging_memory_type_flags);
  vkBindBufferMemory(vulkan_logical_device, texture_array->texture_staging_buffer, texture_array->texture_staging_buffer_memory, 0);
  vkMapMemory(vulkan_logical_device, texture_array->texture_staging_buffer_memory, 0, VK_WHOLE_SIZE, 0, &texture_array->texture_mapped_memory);

  VkImageViewCreateInfo image_view_info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  image_view_info.image                           = texture_array->layered_image;
  image_view_info.viewType                        = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
  image_view_info.format                          = texture_array->format;
  image_view_info.components.r                    = VK_COMPONENT_SWIZZLE_R;
  image_view_info.components.g                    = VK_COMPONENT_SWIZZLE_G;
  image_view_info.components.b                    = VK_COMPONENT_SWIZZLE_B;
  image_view_info.components.a                    = VK_COMPONENT_SWIZZLE_A;
  image_view_info.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
  image_view_info.subresourceRange.baseMipLevel   = 0;
  image_view_info.subresourceRange.levelCount     = texture_array->layered_image_mip_level_count;
  image_view_info.subresourceRange.baseArrayLayer = 0;
  image_view_info.subresourceRange.layerCount     = texture_array->max_texture_count;
  vkCreateImageView(vulkan_logical_device, &image_view_info, nullptr, &texture_array->layered_image_view);

  VkCommandBuffer tmp_command_buffer = vulkan_begin_temporary_command_buffer();

  VkImageMemoryBarrier texture_images_undefined_to_shader_read_only_barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  texture_images_undefined_to_shader_read_only_barrier.srcAccessMask    = 0;
  texture_images_undefined_to_shader_read_only_barrier.dstAccessMask    = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_HOST_WRITE_BIT;
  texture_images_undefined_to_shader_read_only_barrier.oldLayout        = VK_IMAGE_LAYOUT_UNDEFINED;
  texture_images_undefined_to_shader_read_only_barrier.newLayout        = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  texture_images_undefined_to_shader_read_only_barrier.image            = texture_array->layered_image;
  texture_images_undefined_to_shader_read_only_barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, texture_array->layered_image_mip_level_count, 0, max_texture_count };
  vkCmdPipelineBarrier(tmp_command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &texture_images_undefined_to_shader_read_only_barrier);

  vulkan_end_then_submit_temporary_command_buffer(tmp_command_buffer);
}

uint32_t calculate_mip_level_count(const uint32_t image_width, const uint32_t image_height) {
  return static_cast<uint32_t>( std::floor(std::log2(std::max(image_width, image_height))) ) + 1;
}

void VkTextureArray::upload_texture_from_file(const std::string& file_path, const uint32_t texture_index) {
  assert(texture_index < max_texture_count);

  // load texture
  int width;
  int height;
  int channel_count;
  stbi_uc* pixels = nullptr;
  if (this->format == VK_FORMAT_R8G8B8A8_SRGB) {
    pixels = stbi_load(file_path.c_str(), &width, &height, &channel_count, STBI_rgb_alpha);
  }
  assert(pixels);

  // resize texture if needed
  if ( (width != this->layered_image_width) || (height != this->layered_image_height) ) {
    stbi_uc* resized_pixels = (stbi_uc*) malloc(this->texture_size);
    stbir_resize_uint8(pixels, width, height, 0, resized_pixels, static_cast<int>(this->layered_image_width), static_cast<int>(this->layered_image_height), 0, 4);  // 4 is a hard-coded channel count for STBI_rgb_alpha (channel_count variable is actual number of channels in source image but we load it as 4 channels)
    stbi_image_free(pixels);
    pixels = resized_pixels;
  }

  // copy texture to staging buffer
  memcpy(this->texture_mapped_memory, pixels, static_cast<size_t>(this->texture_size));
  stbi_image_free(pixels);

  VkCommandBuffer tmp_command_buffer = vulkan_begin_temporary_command_buffer();

  VkImageMemoryBarrier texture_image_shader_read_only_to_transfer_dst_barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  texture_image_shader_read_only_to_transfer_dst_barrier.srcAccessMask    = VK_ACCESS_SHADER_READ_BIT;
  texture_image_shader_read_only_to_transfer_dst_barrier.dstAccessMask    = VK_ACCESS_TRANSFER_WRITE_BIT;
  texture_image_shader_read_only_to_transfer_dst_barrier.oldLayout        = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  texture_image_shader_read_only_to_transfer_dst_barrier.newLayout        = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  texture_image_shader_read_only_to_transfer_dst_barrier.image            = this->layered_image;
  texture_image_shader_read_only_to_transfer_dst_barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, this->layered_image_mip_level_count, texture_index, 1 };
  vkCmdPipelineBarrier(tmp_command_buffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &texture_image_shader_read_only_to_transfer_dst_barrier);

  // copy texture from staging buffer to texture array
  VkBufferImageCopy copy_info{};
  copy_info.bufferOffset                    = 0;
  copy_info.bufferRowLength                 = 0;
  copy_info.bufferImageHeight               = 0;
  copy_info.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
  copy_info.imageSubresource.mipLevel       = 0;
  copy_info.imageSubresource.baseArrayLayer = texture_index;
  copy_info.imageSubresource.layerCount     = 1;
  copy_info.imageOffset                     = { 0, 0, 0 };
  copy_info.imageExtent                     = { this->layered_image_width, this->layered_image_height, 1 };
  vkCmdCopyBufferToImage(tmp_command_buffer, this->texture_staging_buffer, this->layered_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy_info);

  // generate mip maps
  VkImageMemoryBarrier mip_transfer_dst_to_transfer_src_barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  mip_transfer_dst_to_transfer_src_barrier.srcAccessMask                   = VK_ACCESS_TRANSFER_WRITE_BIT;
  mip_transfer_dst_to_transfer_src_barrier.dstAccessMask                   = VK_ACCESS_TRANSFER_READ_BIT;
  mip_transfer_dst_to_transfer_src_barrier.oldLayout                       = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  mip_transfer_dst_to_transfer_src_barrier.newLayout                       = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  mip_transfer_dst_to_transfer_src_barrier.image                           = this->layered_image;
  mip_transfer_dst_to_transfer_src_barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
  mip_transfer_dst_to_transfer_src_barrier.subresourceRange.baseArrayLayer = texture_index;
  mip_transfer_dst_to_transfer_src_barrier.subresourceRange.layerCount     = 1;
  mip_transfer_dst_to_transfer_src_barrier.subresourceRange.levelCount     = 1;

  VkImageMemoryBarrier mip_transfer_src_to_shader_read_only_barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  mip_transfer_src_to_shader_read_only_barrier.srcAccessMask                   = VK_ACCESS_TRANSFER_READ_BIT;
  mip_transfer_src_to_shader_read_only_barrier.dstAccessMask                   = VK_ACCESS_SHADER_READ_BIT;
  mip_transfer_src_to_shader_read_only_barrier.oldLayout                       = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  mip_transfer_src_to_shader_read_only_barrier.newLayout                       = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  mip_transfer_src_to_shader_read_only_barrier.image                           = this->layered_image;
  mip_transfer_src_to_shader_read_only_barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
  mip_transfer_src_to_shader_read_only_barrier.subresourceRange.baseArrayLayer = texture_index;
  mip_transfer_src_to_shader_read_only_barrier.subresourceRange.layerCount     = 1;
  mip_transfer_src_to_shader_read_only_barrier.subresourceRange.levelCount     = 1;

  int32_t current_mip_width  = this->layered_image_width;
  int32_t current_mip_height = this->layered_image_height;
  for (uint32_t mip_level=1; mip_level < this->layered_image_mip_level_count; ++mip_level) {
    mip_transfer_dst_to_transfer_src_barrier.subresourceRange.baseMipLevel     = mip_level - 1;
    mip_transfer_src_to_shader_read_only_barrier.subresourceRange.baseMipLevel = mip_level - 1;

    vkCmdPipelineBarrier(tmp_command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &mip_transfer_dst_to_transfer_src_barrier);

    VkImageBlit blit{};
    blit.srcOffsets[0]                 = { 0, 0, 0 };
    blit.srcOffsets[1]                 = { current_mip_width, current_mip_height, 1 };
    blit.srcSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.srcSubresource.mipLevel       = mip_level - 1;
    blit.srcSubresource.baseArrayLayer = texture_index;
    blit.srcSubresource.layerCount     = 1;
    blit.dstOffsets[0]                 = { 0, 0, 0 };
    blit.dstOffsets[1]                 = { current_mip_width > 1 ? current_mip_width / 2 : 1, current_mip_height > 1 ? current_mip_height / 2 : 1, 1 };
    blit.dstSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.dstSubresource.mipLevel       = mip_level;
    blit.dstSubresource.baseArrayLayer = texture_index;
    blit.dstSubresource.layerCount     = 1;
    vkCmdBlitImage(tmp_command_buffer, this->layered_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, this->layered_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

    vkCmdPipelineBarrier(tmp_command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &mip_transfer_src_to_shader_read_only_barrier);

    if (current_mip_width  > 1) current_mip_width  /= 2;
    if (current_mip_height > 1) current_mip_height /= 2;
  }

  VkImageMemoryBarrier mip_transfer_dst_to_shader_read_only_barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  mip_transfer_dst_to_shader_read_only_barrier.srcAccessMask                   = VK_ACCESS_TRANSFER_WRITE_BIT;
  mip_transfer_dst_to_shader_read_only_barrier.dstAccessMask                   = VK_ACCESS_SHADER_READ_BIT;
  mip_transfer_dst_to_shader_read_only_barrier.oldLayout                       = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  mip_transfer_dst_to_shader_read_only_barrier.newLayout                       = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  mip_transfer_dst_to_shader_read_only_barrier.image                           = this->layered_image;
  mip_transfer_dst_to_shader_read_only_barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
  mip_transfer_dst_to_shader_read_only_barrier.subresourceRange.baseArrayLayer = texture_index;
  mip_transfer_dst_to_shader_read_only_barrier.subresourceRange.layerCount     = 1;
  mip_transfer_dst_to_shader_read_only_barrier.subresourceRange.levelCount     = 1;
  mip_transfer_dst_to_shader_read_only_barrier.subresourceRange.baseMipLevel   = this->layered_image_mip_level_count - 1;
  vkCmdPipelineBarrier(tmp_command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &mip_transfer_dst_to_shader_read_only_barrier);

  vulkan_end_then_submit_temporary_command_buffer(tmp_command_buffer);
}

void destroy_vk_texture_array(VkTextureArray* const texture_array) {
  vkUnmapMemory(vulkan_logical_device, texture_array->texture_staging_buffer_memory);
  vkDestroyBuffer(vulkan_logical_device, texture_array->texture_staging_buffer, nullptr);
  vkFreeMemory(vulkan_logical_device, texture_array->texture_staging_buffer_memory, nullptr);

  vkDestroyImageView(vulkan_logical_device, texture_array->layered_image_view, nullptr);
  vkDestroyImage(vulkan_logical_device, texture_array->layered_image, nullptr);
  vkFreeMemory(vulkan_logical_device, texture_array->layered_image_memory, nullptr);
}

void graphics_load_glb_model_from_file(const char* file_path, GraphicsModel& model) {
  cgltf_options options{};
  cgltf_data* data = NULL;
  cgltf_result result;

  result = cgltf_parse_file(&options, file_path, &data);
  assert(result == cgltf_result_success);

  result = cgltf_load_buffers(&options, data, file_path);
  assert(result == cgltf_result_success);

  assert( cgltf_validate(data) == cgltf_result_success );

  cgltf_node* nodes = data->nodes;
  assert(data->nodes_count < std::numeric_limits<GraphicsSkin::MeshInstanceHierarchyNode::Index>::max());
  const GraphicsSkin::MeshInstanceHierarchyNode::Index node_count = static_cast<GraphicsSkin::MeshInstanceHierarchyNode::Index>(data->nodes_count);

  size_t model_mesh_count = 0;
  for (size_t i=0; i < data->meshes_count; ++i) {
    model_mesh_count += data->meshes[i].primitives_count;
  }
  std::vector<GraphicsSkin::MeshInstanceHierarchyNode::Index> node_index_to_mesh_index(node_count, -1);
  model.meshes.resize(model_mesh_count);
  model.mesh_hierarchy.resize(model_mesh_count);
  model.animations.resize(data->animations_count);
  GraphicsSkin::MeshInstanceHierarchyNode::Index mesh_index = 0;
  std::vector<Vector3f> inverse_auto_centered_translation(model_mesh_count);
  std::vector<Vector3f> skin_index_to_auto_pivot_translation(data->skins_count);
  std::unordered_map<cgltf_node*, GraphicsSkeleton::JointHierarchyNode::Index> node_to_joint_index{};
  node_to_joint_index.reserve(node_count);

  /* load meshes */
  for (GraphicsSkin::MeshInstanceHierarchyNode::Index node_index=0; node_index < node_count; ++node_index) {
    cgltf_node* node = &nodes[node_index];

    if (node->mesh) {
      node_index_to_mesh_index[node_index] = mesh_index;

      for (size_t primitive_index=0; primitive_index < node->mesh->primitives_count; ++primitive_index) {
        cgltf_primitive* primitive = &node->mesh->primitives[primitive_index];
        assert(primitive->type == cgltf_primitive_type_triangles);  // only supporting triangle list topology

        assert(mesh_index < model_mesh_count);
        GraphicsMesh& mesh = model.meshes[mesh_index];

        mesh.vertices.resize(primitive->attributes[0].data->count);  // gltf spec states "All attribute accessors for a given primitive MUST have the same count"
        float smallest_x_position = std::numeric_limits<float>::max();
        float largest_x_position  = std::numeric_limits<float>::min();
        float smallest_y_position = std::numeric_limits<float>::max();
        float largest_y_position  = std::numeric_limits<float>::min();
        float smallest_z_position = std::numeric_limits<float>::max();
        float largest_z_position  = std::numeric_limits<float>::min();

        for (size_t attribute_index=0; attribute_index < primitive->attributes_count; ++attribute_index) {
          cgltf_attribute* attribute = &primitive->attributes[attribute_index];
          cgltf_accessor* accessor   = attribute->data;

          size_t attribute_value_dimension_count = cgltf_num_components(accessor->type);
          std::vector<float> attribute_values(accessor->count * cgltf_num_components(accessor->type));
          for (size_t i=0; i < accessor->count; ++i) {
            cgltf_accessor_read_float(accessor, i, &attribute_values[i * attribute_value_dimension_count], attribute_value_dimension_count);
          }

          switch (attribute->type) {
            case cgltf_attribute_type_position : {
              for (size_t i=0; i < accessor->count; ++i) {
                size_t attribute_value_index = i * attribute_value_dimension_count;
                mesh.vertices[i].position = Vector3f{attribute_values[attribute_value_index], attribute_values[attribute_value_index + 1], attribute_values[attribute_value_index + 2]};

                if ( mesh.vertices[i].position.x > largest_x_position ) {
                  largest_x_position = mesh.vertices[i].position.x;
                }
                if ( mesh.vertices[i].position.x < smallest_x_position ) {
                  smallest_x_position = mesh.vertices[i].position.x;
                }
                if ( mesh.vertices[i].position.y > largest_y_position ) {
                  largest_y_position = mesh.vertices[i].position.y;
                }
                if ( mesh.vertices[i].position.y < smallest_y_position ) {
                  smallest_y_position = mesh.vertices[i].position.y;
                }
                if ( mesh.vertices[i].position.z > largest_z_position ) {
                  largest_z_position = mesh.vertices[i].position.z;
                }
                if ( mesh.vertices[i].position.z < smallest_z_position ) {
                  smallest_z_position = mesh.vertices[i].position.z;
                }
              }
              break;
            }
            case cgltf_attribute_type_texcoord : {
              for (size_t i=0; i < accessor->count; ++i) {
                size_t attribute_value_index = i * attribute_value_dimension_count;
                mesh.vertices[i].texture_coordinates = Vector2f{attribute_values[attribute_value_index], attribute_values[attribute_value_index + 1]};
              }
              break;
            }
            case cgltf_attribute_type_normal : {
              for (size_t i=0; i < accessor->count; ++i) {
                size_t attribute_value_index = i * attribute_value_dimension_count;
                mesh.vertices[i].normal = Vector3f{attribute_values[attribute_value_index], attribute_values[attribute_value_index + 1], attribute_values[attribute_value_index + 2]};
              }
              break;
            }
            case cgltf_attribute_type_joints : {
              for (size_t i=0; i < accessor->count; ++i) {
                size_t attribute_value_index = i * attribute_value_dimension_count;
                mesh.vertices[i].joint_indices = Vector4f{attribute_values[attribute_value_index], attribute_values[attribute_value_index + 1], attribute_values[attribute_value_index + 2], attribute_values[attribute_value_index + 3]};
              }
              break;
            }
            case cgltf_attribute_type_weights : {
              for (size_t i=0; i < accessor->count; ++i) {
                size_t attribute_value_index = i * attribute_value_dimension_count;
                mesh.vertices[i].joint_weights = Vector4f{attribute_values[attribute_value_index], attribute_values[attribute_value_index + 1], attribute_values[attribute_value_index + 2], attribute_values[attribute_value_index + 3]};
              }
              break;
            }
          }
        }

        const float x_midpoint = lerp(smallest_x_position, largest_x_position, 0.5f);
        const float y_midpoint = lerp(smallest_y_position, largest_y_position, 0.5f);
        const float z_midpoint = lerp(smallest_z_position, largest_z_position, 0.5f);

        if (node->skin) {
          Matrix4x4f root_transform;
          if (node->skin->skeleton) {
            cgltf_node_transform_world(node->skin->skeleton, root_transform.m);
          } else {
            cgltf_node_transform_world(node, root_transform.m);
          }
          invert_transform(root_transform);

          Transform pivot_transform; 
          create_transform(root_transform, pivot_transform);
          for (GraphicsVertex& vertex : mesh.vertices) {
            translate_position_global(vertex.position, pivot_transform.translation);
          }

          for (size_t skin_index=0; skin_index < data->skins_count; ++skin_index) {
            if (node->skin == &(data->skins[skin_index])) {
              skin_index_to_auto_pivot_translation[skin_index] = pivot_transform.translation;
              break;
            }
          }

          inverse_auto_centered_translation[mesh_index] = pivot_transform.translation;
        } else {
          inverse_auto_centered_translation[mesh_index] = { x_midpoint, y_midpoint, z_midpoint };
          Vector3f center_pivot_translation = Vector3f{x_midpoint * -1.0f, y_midpoint * -1.0f, z_midpoint * -1.0f};
          for (GraphicsVertex& vertex : mesh.vertices) {
            translate_position_global(vertex.position, center_pivot_translation);
          }
        }

        const float x_length = largest_x_position - smallest_x_position;
        const float y_length = largest_y_position - smallest_y_position;
        const float z_length = largest_z_position - smallest_z_position;

        // conditional assignment optimization
        float max_length = x_length;
        max_length = ( y_length * static_cast<float>(y_length > max_length) ) + ( max_length * static_cast<float>(y_length <= max_length) );
        max_length = ( z_length * static_cast<float>(z_length > max_length) ) + ( max_length * static_cast<float>(z_length <= max_length) );

        mesh.sphere_volume_radius = max_length; // TODO: should optimize this

        if (primitive->indices) {
          mesh.indices.resize(primitive->indices->count);
          for(size_t indices_index=0; indices_index < primitive->indices->count; ++indices_index) {
            mesh.indices[indices_index] = static_cast<GraphicsIndex>( cgltf_accessor_read_index(primitive->indices, indices_index) );
          }
        }

        ++mesh_index;
      }
    }
  }

  /* build mesh hierarchy */
  for (GraphicsSkin::MeshInstanceHierarchyNode::Index node_index=0; node_index < node_count; ++node_index) {
    cgltf_node* current_node = &nodes[node_index];

    if (current_node->mesh) {
      const GraphicsSkin::MeshInstanceHierarchyNode::Index node_mesh_index = node_index_to_mesh_index[node_index];
      Transform local_transform = identity_transform;
      GraphicsSkin::MeshInstanceHierarchyNode::Index parent_index = -1;

      if (current_node->has_matrix) {
        Matrix4x4f transform_matrix;
        create_matrix(&current_node->matrix[0], transform_matrix);
        create_transform(transform_matrix, local_transform);
      } else {
        if (current_node->has_translation) {
          local_transform.position = { current_node->translation[0], current_node->translation[1], current_node->translation[2] };
        }
        if (current_node->has_rotation) {
          local_transform.orientation = { current_node->rotation[0], current_node->rotation[1], current_node->rotation[2], current_node->rotation[3] };
        }
        if (current_node->has_scale) {
          local_transform.scale = { current_node->scale[0], current_node->scale[1], current_node->scale[2] };
        }
      }

      cgltf_node* parent_node = current_node->parent;
      while (parent_node) {
        if (parent_node->mesh) {
          GraphicsSkin::MeshInstanceHierarchyNode::Index parent_node_index;
          for (parent_node_index=0; parent_node_index < node_count; ++parent_node_index) {
            cgltf_node* current_node = &nodes[parent_node_index];
            if (current_node == parent_node) break;
          }

          parent_index = node_index_to_mesh_index[parent_node_index];
          break;
        } else {
          if (parent_node->has_matrix || parent_node->has_translation || parent_node->has_rotation || parent_node->has_scale) {
            Transform parent_local_transform = identity_transform;

            if (parent_node->has_matrix) {
              Matrix4x4f transform_matrix;
              create_matrix(&parent_node->matrix[0], transform_matrix);
              create_transform(transform_matrix, parent_local_transform);
            } else {
              if (parent_node->has_translation) {
                parent_local_transform.position = { parent_node->translation[0], parent_node->translation[1], parent_node->translation[2] };
              }
              if (parent_node->has_rotation) {
                parent_local_transform.orientation = { parent_node->rotation[0], parent_node->rotation[1], parent_node->rotation[2], parent_node->rotation[3] };
              }
              if (parent_node->has_scale) {
                parent_local_transform.scale = { parent_node->scale[0], parent_node->scale[1], parent_node->scale[2] };
              }
            }
            combine_transform(parent_local_transform, local_transform);
          }
          parent_node = parent_node->parent;
        }
      }

      std::function<GraphicsSkin::MeshInstanceHierarchyNode::Index(cgltf_node*)> calculate_mesh_child_count;
      calculate_mesh_child_count = [&calculate_mesh_child_count](cgltf_node* node) -> GraphicsSkin::MeshInstanceHierarchyNode::Index {
        if ( !(node->children_count > 0) ) return 0;

        GraphicsSkin::MeshInstanceHierarchyNode::Index mesh_child_count = 0;
        for (size_t i=0; i < node->children_count; ++i) {
          cgltf_node* child_node = node->children[i];

          if (child_node->mesh) {
            ++mesh_child_count;
          } else {
            mesh_child_count += calculate_mesh_child_count(child_node);
          }
        }

        return mesh_child_count;
      };

      for (size_t primitive_index=0; primitive_index < current_node->mesh->primitives_count; ++primitive_index) {
        model.mesh_hierarchy[node_mesh_index].children_count = 0;
      }

      model.mesh_hierarchy[node_mesh_index].children_count = calculate_mesh_child_count(current_node);  // current_node can only have single parent so use first primitive

      for (size_t primitive_index=0; primitive_index < current_node->mesh->primitives_count; ++primitive_index) {
        Transform current_mesh_transform   = identity_transform;
        current_mesh_transform.translation = inverse_auto_centered_translation[node_mesh_index + primitive_index];
        combine_transform(local_transform, current_mesh_transform);

        model.mesh_hierarchy[node_mesh_index + primitive_index].parent_index    = parent_index;
        model.mesh_hierarchy[node_mesh_index + primitive_index].local_transform = current_mesh_transform;
      }
    }
  }

  /* build skeletons */
  const size_t skin_count = data->skins_count;
  model.skeletons.resize(skin_count);

  auto calculate_node_index = [](const cgltf_node* const target_node, const cgltf_node* const all_nodes, const size_t all_nodes_count) -> size_t {
    for (size_t i=0; i < all_nodes_count; ++i) {
      if (target_node == &all_nodes[i]) {
        return i;
      }
    }
    return -1;
  };
 
  for (size_t i=0; i < skin_count; ++i) {
    cgltf_skin* skin = &(data->skins[i]);
    model.skeletons[i].joint_hierarchy.resize(skin->joints_count);
    model.skeletons[i].inverse_bind_matrices.resize(skin->joints_count);
    model.skeletons[i].names.resize(skin->joints_count);

    std::vector<bool> is_node_a_joint(node_count, false);
    for (size_t joint_index=0; joint_index < skin->joints_count; ++joint_index) {
      is_node_a_joint[ calculate_node_index(skin->joints[joint_index], nodes, node_count) ] = true;
    }

    for (size_t joint_index=0; joint_index < skin->joints_count; ++joint_index) {
      cgltf_node* current_joint = skin->joints[joint_index];

      // load name
      if (current_joint->name) {
        model.skeletons[i].names[joint_index] = std::string(current_joint->name);
      } else {
        model.skeletons[i].names[joint_index] = std::to_string(i) + "-" + std::to_string(joint_index);
      }

      // calculate inverse bind matrix
      Matrix4x4f& inverse_bind_matrix = model.skeletons[i].inverse_bind_matrices[joint_index];
      if (skin->inverse_bind_matrices) {
        cgltf_accessor_read_float(skin->inverse_bind_matrices, joint_index, inverse_bind_matrix.m, 16);

        invert_transform(inverse_bind_matrix);
        Transform global_transform; 
        create_transform(inverse_bind_matrix, global_transform);
        translate_position_global(global_transform.position, skin_index_to_auto_pivot_translation[i]);
        create_transform_matrix(global_transform, inverse_bind_matrix);
        invert_transform(inverse_bind_matrix);
      } else {
        assert(false);  // TODO: handle case where inverse_bind_matrices are not provided
      }

      // calculate joint hierarchy
      GraphicsSkeleton::JointHierarchyNode::Index parent_index   = GraphicsSkeleton::JointHierarchyNode::Index(-1);
      GraphicsSkeleton::JointHierarchyNode::Index children_count = 0;

      cgltf_node* parent_node = current_joint->parent;
      while (parent_node) {
        const size_t node_index = calculate_node_index(parent_node, nodes, node_count);
        if ( is_node_a_joint[node_index] ) {
          for (size_t current_parent_joint_index=0; current_parent_joint_index < skin->joints_count; ++current_parent_joint_index) {
            if (skin->joints[current_parent_joint_index] == parent_node) {
              assert(current_parent_joint_index < std::numeric_limits<GraphicsSkeleton::JointHierarchyNode::Index>::max());
              parent_index = static_cast<GraphicsSkeleton::JointHierarchyNode::Index>(current_parent_joint_index);
              break;
            }
          }
          break;
        } else {
          parent_node = parent_node->parent;
        }
      }

      std::function<GraphicsSkeleton::JointHierarchyNode::Index(cgltf_node*)> calculate_joint_child_count;
      calculate_joint_child_count = [&calculate_joint_child_count,&calculate_node_index,&nodes,&is_node_a_joint,&node_count](cgltf_node* node) -> GraphicsSkeleton::JointHierarchyNode::Index {
        if ( !(node->children_count > 0) ) return 0;

        GraphicsSkeleton::JointHierarchyNode::Index joint_child_count = 0;
        for (size_t child_index=0; child_index < node->children_count; ++child_index) {
          cgltf_node* child_node = node->children[child_index];
          const size_t node_index = calculate_node_index(child_node, nodes, node_count);

          if (is_node_a_joint[node_index]) {
            ++joint_child_count;
          } else {
            joint_child_count += calculate_joint_child_count(child_node);
          }
        }

        return joint_child_count;
      };

      children_count = calculate_joint_child_count(current_joint);

      model.skeletons[i].joint_hierarchy[joint_index] = { parent_index, children_count, identity_transform };
      node_to_joint_index[current_joint] = static_cast<GraphicsSkeleton::JointHierarchyNode::Index>(joint_index);
    }

    // calculate local transforms
    for (size_t joint_index=0; joint_index < skin->joints_count; ++joint_index) {
      GraphicsSkeleton::JointHierarchyNode& joint_node = model.skeletons[i].joint_hierarchy[joint_index];

      // joint_node assumed to have local_transform as identity transform by default
      if (joint_node.parent_index != GraphicsSkeleton::JointHierarchyNode::Index(-1)) {
        Matrix4x4f& parent_inverse_bind_matrix = model.skeletons[i].inverse_bind_matrices[joint_node.parent_index];
        Matrix4x4f  bind_matrix = model.skeletons[i].inverse_bind_matrices[joint_index];
        invert_transform(bind_matrix);

        Transform parent_inverse_transform;
        create_transform(parent_inverse_bind_matrix, parent_inverse_transform);
        create_transform(bind_matrix, joint_node.local_transform);
        combine_transform(parent_inverse_transform, joint_node.local_transform);
      } else {
        Matrix4x4f bind_matrix = model.skeletons[i].inverse_bind_matrices[joint_index];
        invert_transform(bind_matrix);

        create_transform(bind_matrix, joint_node.local_transform);
      }
    }
  }

  /* load animations */
  for (size_t i=0; i < data->animations_count; ++i) {
    Animation& current_animation = model.animations[i];

    if (data->animations[i].name) {
      current_animation.name = std::string(data->animations[i].name);
    } else {
      current_animation.name = std::string(file_path) + "-" + std::to_string(i);
    }

    const size_t sampler_count = data->animations[i].samplers_count;
    current_animation.samplers.resize(sampler_count);
    for (size_t sampler_index=0; sampler_index < sampler_count; ++sampler_index) {
      cgltf_animation_sampler& sampler = data->animations[i].samplers[sampler_index];

      switch (sampler.interpolation) {
        case cgltf_interpolation_type_linear : {
          current_animation.samplers[sampler_index].interpolation = Animation::Sampler::Interpolation::Linear;
          break;
        }
        case cgltf_interpolation_type_step : {
          current_animation.samplers[sampler_index].interpolation = Animation::Sampler::Interpolation::Step;
          break;
        }
        case cgltf_interpolation_type_cubic_spline : {
          current_animation.samplers[sampler_index].interpolation = Animation::Sampler::Interpolation::CubicSpline;
          break;
        }
      }

      current_animation.samplers[sampler_index].inputs.resize(sampler.input->count);
      for (size_t sampler_input_index=0; sampler_input_index < sampler.input->count; ++sampler_input_index) {
        cgltf_accessor_read_float(sampler.input, sampler_input_index, &current_animation.samplers[sampler_index].inputs[sampler_input_index], 1);
      }

      const size_t path_component_count = cgltf_num_components(sampler.output->type);
      current_animation.samplers[sampler_index].outputs.resize(sampler.output->count * path_component_count);
      for (size_t sampler_output_index=0; sampler_output_index < sampler.output->count; ++sampler_output_index) {
        cgltf_accessor_read_float(sampler.output, sampler_output_index, &current_animation.samplers[sampler_index].outputs[sampler_output_index * path_component_count], path_component_count);
      }
    }

    const size_t channel_count = data->animations[i].channels_count;
    current_animation.channels.resize(channel_count);
    for (size_t channel_index=0; channel_index < channel_count; ++channel_index) {
      cgltf_animation_channel& channel = data->animations[i].channels[channel_index];
      cgltf_node* target               = channel.target_node;
      
      const GraphicsSkeleton::JointHierarchyNode::Index target_joint_index = node_to_joint_index[target];
      current_animation.channels[channel_index].target.joint_index = target_joint_index;

      switch (channel.target_path) {
        case cgltf_animation_path_type_translation : {
          current_animation.channels[channel_index].target.path = Animation::Channel::Path::Translation;
          break;
        }
        case cgltf_animation_path_type_rotation : {
          current_animation.channels[channel_index].target.path = Animation::Channel::Path::Rotation;
          break;
        }
        case cgltf_animation_path_type_scale : {
          current_animation.channels[channel_index].target.path = Animation::Channel::Path::Scale;
          break;
        }
      }

      for (size_t sampler_index=0; sampler_index < sampler_count; ++sampler_index) {
        if (&data->animations[i].samplers[sampler_index] == channel.sampler) {
          assert(sampler_index < std::numeric_limits<Animation::Sampler::Index>::max());
          current_animation.channels[channel_index].sampler_index = static_cast<Animation::Sampler::Index>(sampler_index);
          break;
        }
      }
    }

    float start_time = std::numeric_limits<float>::max();
    float end_time   = std::numeric_limits<float>::min();
    for (size_t sampler_index=0; sampler_index < sampler_count; ++sampler_index) {
      for (auto& input : current_animation.samplers[sampler_index].inputs) {
        if (input < start_time) {
          start_time = input;
        }
        if (input > end_time) {
          end_time = input;
        }
      }
    }
    current_animation.start_time_seconds = start_time;
    current_animation.end_time_seconds   = end_time;
  }

  cgltf_free(data);
}

void graphics_deduplicate_vertices(const GraphicsMesh& mesh, GraphicsMesh& deduplicated_mesh) {
  std::unordered_map<GraphicsVertex, GraphicsIndex> vertex_to_index{};
  GraphicsIndex current_graphics_index = 0;

  if (mesh.indices.size() > 0) {
    for (uint32_t indices_index=0; indices_index < mesh.indices.size(); ++indices_index) {
      const GraphicsVertex& current_vertex = mesh.vertices[ mesh.indices[indices_index] ];
      if (vertex_to_index.count(current_vertex) == 0) {
        deduplicated_mesh.vertices.push_back(current_vertex);
        vertex_to_index[current_vertex] = current_graphics_index;
        ++current_graphics_index;
      }
      deduplicated_mesh.indices.push_back( vertex_to_index[current_vertex] );
    }
  } else {
    for (uint32_t vertex_index=0; vertex_index < mesh.vertices.size(); ++vertex_index) {
      const GraphicsVertex& current_vertex = mesh.vertices[vertex_index];
      if (vertex_to_index.count(current_vertex) == 0) {
        deduplicated_mesh.vertices.push_back(current_vertex);
        vertex_to_index[current_vertex] = current_graphics_index;
        ++current_graphics_index;
      }
      deduplicated_mesh.indices.push_back( vertex_to_index[current_vertex] );
    }
  }

  deduplicated_mesh.sphere_volume_radius = mesh.sphere_volume_radius;
}

VkMeshes::SourceFileIdData VkMeshes::insert_from_glb_source_file(const char* file_path, GraphicsModel* model_file_cache) {
  GraphicsModel* model_file_ptr;
  GraphicsModel  model_file_stack;
  if (model_file_cache) {
    model_file_ptr = model_file_cache;
  } else {
    model_file_ptr = &model_file_stack;
    graphics_load_glb_model_from_file(file_path, *model_file_ptr);
  }
  GraphicsModel& model_file = *model_file_ptr;

  const VkMeshes::Index first_mesh_id = this->current_available_mesh_id;
  assert( (first_mesh_id + model_file.meshes.size()) < VkMeshes::max_count );
  this->current_available_mesh_id += static_cast<VkMeshes::Index>(model_file.meshes.size());

  SourceFileIdData id_data = { first_mesh_id, static_cast<VkMeshes::Index>(model_file.meshes.size()) };
  source_file_path_to_id_data.insert({ std::string(file_path), id_data });

  for (size_t i=0; i < model_file.meshes.size(); ++i) {
    GraphicsMesh optimized_mesh;
    graphics_deduplicate_vertices(model_file.meshes[i], optimized_mesh);

    vulkan_all_meshes.all_hot_draw_data[first_mesh_id + i].index_count   = static_cast<uint32_t>(optimized_mesh.indices.size());
    vulkan_all_meshes.all_hot_draw_data[first_mesh_id + i].first_index   = static_cast<uint32_t>(this->current_draw_buffer_index_count);
    vulkan_all_meshes.all_hot_draw_data[first_mesh_id + i].first_vertex  = static_cast<uint32_t>(this->current_draw_buffer_vertex_count);
    vulkan_all_meshes.all_cold_draw_data[first_mesh_id + i].vertex_count = static_cast<uint32_t>(optimized_mesh.vertices.size());
    vulkan_all_meshes.all_radii[first_mesh_id + i]                       = optimized_mesh.sphere_volume_radius;

    this->draw_buffer->update(optimized_mesh.indices.data(), optimized_mesh.indices.size(), optimized_mesh.vertices.data(), optimized_mesh.vertices.size(), this->current_draw_buffer_index_count, this->current_draw_buffer_vertex_count);

    this->current_draw_buffer_index_count  += optimized_mesh.indices.size();
    this->current_draw_buffer_vertex_count += optimized_mesh.vertices.size();
  }

  return id_data;
}

uint32_t upload_base_color_map_from_file(const char* file_path) {
  static tom::Semaphore semaphore{1};
  static uint32_t current_texture_index = 0;

  semaphore.wait();
  const uint32_t texture_index = current_texture_index;
  vulkan_base_color_map_array->upload_texture_from_file(file_path, current_texture_index);
  current_texture_index += 1;
  semaphore.signal();

  return texture_index;
}

void graphics_upload_meshes_from_glb_file(const char* file_path, GraphicsModel* model_file_cache) {
  VkMeshes::SourceFileIdData source_file_id_data = vulkan_all_meshes.get_id_data_from_source_file_path(file_path);
  if (source_file_id_data.first_mesh_id == VkMeshes::Index(-1)) {
    vulkan_all_meshes.insert_from_glb_source_file(file_path, model_file_cache);
  }
}

void graphics_load_skeletons_from_glb_file(const char* file_path, GraphicsModel* model_file_cache) {
  VkSkeletons::SourceFileIdData source_file_id_data = vulkan_all_skeletons.get_id_data_from_source_file_path(file_path);
  if (source_file_id_data.first_skeleton_id == VkSkeletons::Index(-1)) {
    vulkan_all_skeletons.insert_from_glb_source_file(file_path, model_file_cache);
  }
}

AnimationArray load_animations_from_glb_file(const char* file_path, GraphicsModel* model_file_cache) {
  VkAnimations::SourceFileIdData source_file_id_data = vulkan_all_animations.get_id_data_from_source_file_path(file_path);
  if (source_file_id_data.first_animation_id == VkAnimations::Index(-1)) {
    vulkan_all_animations.insert_from_glb_source_file(file_path, model_file_cache);
  }

  source_file_id_data = vulkan_all_animations.get_id_data_from_source_file_path(file_path);
  return { source_file_id_data.first_animation_id, source_file_id_data.animation_count };
}

void create_graphics_mesh_instance_array_from_glb(const char* file_path, GraphicsMeshInstanceArray& mesh_instance_array, GraphicsModel* model_file_cache) {
  VkMeshes::SourceFileIdData source_file_id_data = vulkan_all_meshes.get_id_data_from_source_file_path(file_path);
  if (source_file_id_data.first_mesh_id == VkMeshes::Index(-1)) {
    source_file_id_data = vulkan_all_meshes.insert_from_glb_source_file(file_path);
  }

  // HACK: assuming model has all unique meshes (no instancing) and if eventually handle instancing case then make sure mesh_hierarchy works
  mesh_instance_array.first_mesh_instance = graphics_all_mesh_instances.allocate_mesh_instances(source_file_id_data.mesh_count);
  mesh_instance_array.size                = source_file_id_data.mesh_count;

  uint32_t current_mesh_instance_index = mesh_instance_array.first_mesh_instance;
  for (uint32_t i=0; i < source_file_id_data.mesh_count; ++i) {
    graphics_all_mesh_instances.mesh_ids[current_mesh_instance_index] = source_file_id_data.first_mesh_id + i;
    ++current_mesh_instance_index;
  }
}

void update_graphics_mesh_instance_array(const GraphicsMeshInstanceArray& mesh_instance_array, const Transform& transform, const uint32_t material_id, const uint32_t joint_index, const uint32_t index) {
  GraphicsMeshInstances::BufferData instance(transform, material_id, joint_index);

  const float mesh_radius = vulkan_all_meshes.all_radii[ graphics_all_mesh_instances.mesh_ids[mesh_instance_array.first_mesh_instance + index] ];
  float largest_scale_magnitude = abs(transform.scale.x);
  float tmp_radius;
  if ((tmp_radius=abs(transform.scale.y)) > largest_scale_magnitude) {
    largest_scale_magnitude = tmp_radius;
  }
  if ((tmp_radius=abs(transform.scale.z)) > largest_scale_magnitude) {
    largest_scale_magnitude = tmp_radius;
  }

  const float scale = (joint_index == uint32_t(-1)) ? largest_scale_magnitude * mesh_radius : largest_scale_magnitude * graphics_all_mesh_instances.skeleton_radius_overrides[mesh_instance_array.first_mesh_instance + index];

  graphics_all_mesh_instances.draw_data[mesh_instance_array.first_mesh_instance + index] = { instance, {transform.position, scale} };
}

void destroy_graphics_mesh_instance_array(GraphicsMeshInstanceArray& mesh_instance_array) {
  uint32_t current_mesh_instance_index = mesh_instance_array.first_mesh_instance;
  for (uint32_t i=0; i < mesh_instance_array.size; ++i) {
    graphics_all_mesh_instances.mesh_ids[current_mesh_instance_index] = -1;
    ++current_mesh_instance_index;
  }

  mesh_instance_array.first_mesh_instance = -1;
}

void graphics_upload_materials(GraphicsMaterial* const materials, const uint32_t count, const uint32_t start_index) {
  memcpy(static_cast<GraphicsMaterial*>(vulkan_material_uniform_buffer->mapped_memory) + start_index, materials, sizeof(GraphicsMaterial) * count);
}

uint32_t create_graphics_material(const Vector4f& base_color_factor, const Vector3f& emissive_factor, const float metallic_factor, const float roughness_factor, const char* base_color_map_file_path) {
  static tom::Semaphore semaphore{1};
  static uint32_t current_material_index = 0;

  semaphore.wait();
  assert(current_material_index < MaterialUniformBufferObject::max_count);
  const uint32_t material_id = current_material_index;
  ++current_material_index;

  GraphicsMaterial material{ base_color_factor, emissive_factor, metallic_factor, roughness_factor, static_cast<uint32_t>(-1) };
  if (base_color_map_file_path != nullptr) {
    material.base_color_map_index = upload_base_color_map_from_file(base_color_map_file_path);
  }
  graphics_upload_materials(&material, 1, material_id);
  semaphore.signal(); // maybe signal after incrementing material_index?

  return material_id;
}

uint32_t create_graphics_material(const float metallic_factor, const float roughness_factor, const char* base_color_map_file_path) {
  return create_graphics_material(Vector4f{1.0f, 1.0f, 1.0f, 1.0f}, Vector3f{0.0f, 0.0f, 0.0f}, metallic_factor, roughness_factor, base_color_map_file_path);
}

uint32_t create_graphics_material(const Vector4f& base_color_factor, const float metallic_factor, const float roughness_factor) {
  return create_graphics_material(base_color_factor, Vector3f{0.0f, 0.0f, 0.0f}, metallic_factor, roughness_factor, nullptr);
}

VkSkeletons::SourceFileIdData VkSkeletons::insert_from_glb_source_file(const char* file_path, GraphicsModel* model_file_cache) {
  GraphicsModel* model_file_ptr;
  GraphicsModel  model_file_stack;
  if (model_file_cache) {
    model_file_ptr = model_file_cache;
  } else {
    model_file_ptr = &model_file_stack;
    graphics_load_glb_model_from_file(file_path, *model_file_ptr);
  }
  GraphicsModel& model_file = *model_file_ptr;

  if (model_file.skeletons.size() == 0) {
    return { (VkSkeletons::Index)-1, 0 };
  }

  const VkSkeletons::Index first_skeleton_id = this->current_available_skeleton_id;
  assert( (first_skeleton_id + model_file.skeletons.size()) < VkSkeletons::max_count );
  this->current_available_skeleton_id += static_cast<VkSkeletons::Index>(model_file.skeletons.size());

  SourceFileIdData id_data = { first_skeleton_id, static_cast<VkSkeletons::Index>(model_file.skeletons.size()) };
  source_file_path_to_id_data.insert({ std::string(file_path), id_data });

  for (size_t i=0; i < model_file.skeletons.size(); ++i) {
    this->skeletons[first_skeleton_id + i] = model_file.skeletons[i];
  }

  return id_data;
}

VkAnimations::SourceFileIdData VkAnimations::insert_from_glb_source_file(const char* file_path, GraphicsModel* model_file_cache) {
  GraphicsModel* model_file_ptr;
  GraphicsModel  model_file_stack;
  if (model_file_cache) {
    model_file_ptr = model_file_cache;
  } else {
    model_file_ptr = &model_file_stack;
    graphics_load_glb_model_from_file(file_path, *model_file_ptr);
  }
  GraphicsModel& model_file = *model_file_ptr;

  if (model_file.animations.size() == 0) {
    return { (VkAnimations::Index)-1, 0 };
  }

  const VkAnimations::Index first_animation_id = this->current_available_animation_id;
  assert( (first_animation_id + model_file.skeletons.size()) < VkAnimations::max_count );
  this->current_available_animation_id += static_cast<VkAnimations::Index>(model_file.animations.size());

  SourceFileIdData id_data = { first_animation_id, static_cast<VkAnimations::Index>(model_file.animations.size()) };
  source_file_path_to_id_data.insert({ std::string(file_path), id_data });

  for (size_t i=0; i < model_file.animations.size(); ++i) {
    this->animations[first_animation_id + i] = model_file.animations[i];
  }

  return id_data;
}

void create_graphics_skeleton_instance_array_from_glb(const char* file_path, GraphicsSkeletonInstanceArray& skeleton_instance_array, GraphicsModel* model_file_cache) {
  VkSkeletons::SourceFileIdData source_file_id_data = vulkan_all_skeletons.get_id_data_from_source_file_path(file_path);
  if (source_file_id_data.first_skeleton_id == VkSkeletons::Index(-1)) {
    source_file_id_data = vulkan_all_skeletons.insert_from_glb_source_file(file_path, model_file_cache);
  }

  if (source_file_id_data.skeleton_count == 0) {
    skeleton_instance_array.first_skeleton_instance = GraphicsSkeletonInstances::Index(-1);
    skeleton_instance_array.size = 0;
    return;
  }

  std::vector<uint32_t> joint_counts(source_file_id_data.skeleton_count);
  uint32_t total_joint_count = 0;
  for (uint32_t i=0; i < source_file_id_data.skeleton_count; ++i) {
    joint_counts[i]    = (uint32_t) vulkan_all_skeletons.skeletons[ static_cast<uint32_t>(source_file_id_data.first_skeleton_id) + i ].joint_count();
    total_joint_count += joint_counts[i];
  }

  skeleton_instance_array.first_skeleton_instance = graphics_all_skeleton_instances.allocate_skeleton_instances(total_joint_count);
  skeleton_instance_array.size                    = source_file_id_data.skeleton_count;

  uint32_t current_skeleton_instance_index = skeleton_instance_array.first_skeleton_instance;
  for (uint32_t i=0; i < source_file_id_data.skeleton_count; ++i) {
    const VkSkeletons::Index skeleton_id = source_file_id_data.first_skeleton_id + i;
    graphics_all_skeleton_instances.skeleton_ids[current_skeleton_instance_index] = skeleton_id;

    current_skeleton_instance_index += joint_counts[i];
  }
}

void destroy_graphics_skeleton_instance_array(GraphicsSkeletonInstanceArray& skeleton_instance_array) {
  uint32_t total_joint_count = 0;
  uint32_t current_skeleton_instance_index = skeleton_instance_array.first_skeleton_instance;
  for (uint32_t i=0; i < skeleton_instance_array.size; ++i) {
    const uint16_t skeleton_id = graphics_all_skeleton_instances.skeleton_ids[skeleton_instance_array.first_skeleton_instance + i];
    total_joint_count += static_cast<uint32_t>(vulkan_all_skeletons.skeletons[skeleton_id].joint_count());
  }

  memset(&graphics_all_skeleton_instances.skeleton_ids[skeleton_instance_array.first_skeleton_instance], VkSkeletons::Index(-1), total_joint_count * sizeof(graphics_all_skeleton_instances.skeleton_ids[0]));
  skeleton_instance_array.first_skeleton_instance = -1;
}

void create_graphics_skin_from_glb(const char* file_path, GraphicsSkin& skin) {
  GraphicsModel model;
  graphics_load_glb_model_from_file(file_path, model);
  GraphicsSkeletonInstanceArray skeleton_instance_array;

  create_graphics_mesh_instance_array_from_glb(file_path, skin.mesh_instance_array, &model);
  create_graphics_skeleton_instance_array_from_glb(file_path, skeleton_instance_array, &model);
  skin.mesh_instance_hierarchy = model.mesh_hierarchy;
  skin.skeleton_instance_id    = skeleton_instance_array.first_skeleton_instance;

  if (skin.mesh_instance_array.size > 1) {
    GraphicsSkin::MeshInstanceHierarchyNode::Index root_index = -1;

    for (GraphicsSkin::MeshInstanceHierarchyNode::Index i=0; i < skin.mesh_instance_array.size; ++i) {
      if (skin.mesh_instance_hierarchy[i].parent_index == GraphicsSkin::MeshInstanceHierarchyNode::Index(-1)) {
        root_index = i;
        break;
      }
    }

    for (GraphicsSkin::MeshInstanceHierarchyNode::Index i = root_index + 1; i < skin.mesh_instance_array.size; ++i) {
      if (skin.mesh_instance_hierarchy[i].parent_index == GraphicsSkin::MeshInstanceHierarchyNode::Index(-1)) {
        skin.mesh_instance_hierarchy[i].parent_index = root_index;
        skin.mesh_instance_hierarchy[root_index].children_count += 1;
      }
    }
  }

  if (skin.skeleton_instance_id != GraphicsSkeletonInstances::Index(-1)) {
    float largest_radius = 0.0f;
    for (uint32_t i=0; i < skin.mesh_instance_array.size; ++i) {
      const uint32_t mesh_id = graphics_all_mesh_instances.mesh_ids[skin.mesh_instance_array.first_mesh_instance + i];
      largest_radius = ( vulkan_all_meshes.all_radii[mesh_id] * static_cast<float>(vulkan_all_meshes.all_radii[mesh_id] > largest_radius) ) + ( largest_radius * static_cast<float>(vulkan_all_meshes.all_radii[mesh_id] <= largest_radius) );
    }

    for (uint32_t i=0; i < skin.mesh_instance_array.size; ++i) {
      const Vector3f& model_space_position = skin.mesh_instance_hierarchy[i].global_transform.position;

      float largest_transform_component = model_space_position.x;
      largest_transform_component = ( model_space_position.y * static_cast<float>(model_space_position.y > largest_transform_component) ) + ( largest_transform_component * static_cast<float>(model_space_position.y <= largest_transform_component) );
      largest_transform_component = ( model_space_position.z * static_cast<float>(model_space_position.z > largest_transform_component) ) + ( largest_transform_component * static_cast<float>(model_space_position.z <= largest_transform_component) );

      graphics_all_mesh_instances.skeleton_radius_overrides[skin.mesh_instance_array.first_mesh_instance + i] = largest_radius + largest_transform_component;
    }
  }
}

void GraphicsSkin::update(const GraphicsSkin::MeshInstanceHierarchyNode::Index index, const Transform& global_transform, uint32_t material_id) {
  this->mesh_instance_hierarchy[index].global_transform = global_transform;
  this->mesh_instance_hierarchy[index].material_id      = material_id;
  const uint32_t mesh_count = this->mesh_instance_array.size;
  update_graphics_mesh_instance_array(this->mesh_instance_array, global_transform, material_id, this->skeleton_instance_id, index);

  std::function<void(std::vector<GraphicsSkin::MeshInstanceHierarchyNode>&, GraphicsSkin::MeshInstanceHierarchyNode::Index,GraphicsMeshInstanceArray&,uint32_t)> update_all_children_transforms;
  update_all_children_transforms = [&update_all_children_transforms,&mesh_count](std::vector<GraphicsSkin::MeshInstanceHierarchyNode>& hierarchy, GraphicsSkin::MeshInstanceHierarchyNode::Index node_index, GraphicsMeshInstanceArray& mesh_instances, uint32_t skeleton_instance_index) {
    GraphicsSkin::MeshInstanceHierarchyNode& node = hierarchy[node_index];
    if ( !(node.children_count > 0) ) return;

    GraphicsSkin::MeshInstanceHierarchyNode::Index current_child_count = 0;
    for (GraphicsSkin::MeshInstanceHierarchyNode::Index i=0; (i < mesh_count) && (current_child_count < node.children_count); ++i) {
      if (hierarchy[i].parent_index == node_index) {
        hierarchy[i].global_transform = hierarchy[i].local_transform;
        combine_transform(node.global_transform, hierarchy[i].global_transform);
        update_graphics_mesh_instance_array(mesh_instances, hierarchy[i].global_transform, hierarchy[i].material_id, skeleton_instance_index, i);
        update_all_children_transforms(hierarchy, i, mesh_instances, skeleton_instance_index);
        ++current_child_count;
      }
    }
  };

  update_all_children_transforms(this->mesh_instance_hierarchy, index, this->mesh_instance_array, this->skeleton_instance_id);
}

void GraphicsSkin::update(const GraphicsSkin::MeshInstanceHierarchyNode::Index index, uint32_t material_id) {
  this->mesh_instance_hierarchy[index].material_id = material_id;
  update_graphics_mesh_instance_array(this->mesh_instance_array, this->mesh_instance_hierarchy[index].global_transform, material_id, this->skeleton_instance_id, index);
}

void GraphicsSkin::update_all(uint32_t material_id) {
  for (GraphicsSkin::MeshInstanceHierarchyNode::Index i=0; i < this->mesh_instance_array.size; ++i) {
    this->update(i, material_id);
  }
}

void GraphicsSkin::play_animation(const uint32_t animation_id, const bool is_looping) {
  if (this->skeleton_instance_id != GraphicsSkeletonInstances::Index(-1)) {
    graphics_all_skeleton_instances.animation_states[this->skeleton_instance_id] = { animation_id, vulkan_all_animations.animations[animation_id].start_time_seconds, is_looping };
  }
}

void GraphicsSkin::stop_animating() {
  if (this->skeleton_instance_id != GraphicsSkeletonInstances::Index(-1)) {
    graphics_all_skeleton_instances.animation_states[this->skeleton_instance_id].animation_id = uint32_t(-1);
  }
}

#pragma optimize( "", off )
bool init_graphics() {
  VkResult current_vk_result;
  {
    std::vector<const char*> validation_layers;
    std::vector<const char*> instance_extensions;

    #ifndef NDEBUG
    uint32_t layer_count;
    vkEnumerateInstanceLayerProperties(&layer_count, nullptr);
    std::vector<VkLayerProperties> available_layers(layer_count);
    vkEnumerateInstanceLayerProperties(&layer_count, available_layers.data());

    const char* const validation_layer_names[] = { "VK_LAYER_KHRONOS_validation", "VK_LAYER_LUNARG_standard_validation" };

    // try selecting VK_LAYER_KHRONOS_validation first then VK_LAYER_LUNARG_standard_validation
    const char* validation_layer_name = nullptr;
    for (auto& current_validation_layer_name : validation_layer_names) {
      for (const auto& layer_properties : available_layers) {
        if (strcmp(current_validation_layer_name, layer_properties.layerName) == 0) {
          validation_layer_name = current_validation_layer_name;
          goto found_validation_layer_name;
        }
      }
    }
    
    found_validation_layer_name:

    if (validation_layer_name) {
      validation_layers.push_back(validation_layer_name);
    } else {
      DEBUG_LOG("No validation layers found\n");
      return false;
    }

    instance_extensions.push_back("VK_EXT_debug_utils");
    #endif
    
    instance_extensions.push_back("VK_KHR_device_group_creation");
    instance_extensions.push_back("VK_KHR_get_physical_device_properties2");
    instance_extensions.push_back("VK_KHR_surface");
    instance_extensions.push_back("VK_KHR_win32_surface");

    VkApplicationInfo app_info{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app_info.pApplicationName   = APPLICATION_NAME;
    app_info.applicationVersion = APPLICATION_VERSION;
    app_info.pEngineName        = ENGINE_NAME;
    app_info.engineVersion      = ENGINE_VERSION;
    app_info.apiVersion         = VK_API_VERSION_1_3;

    VkInstanceCreateInfo instance_info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    instance_info.pApplicationInfo        = &app_info;
    instance_info.enabledLayerCount       = (uint32_t)validation_layers.size();
    instance_info.ppEnabledLayerNames     = validation_layers.empty() ? nullptr : validation_layers.data();
    instance_info.enabledExtensionCount   = (uint32_t)instance_extensions.size();
    instance_info.ppEnabledExtensionNames = instance_extensions.empty() ? nullptr : instance_extensions.data();

    XrVulkanInstanceCreateInfoKHR xr_vulkan_instance_create_info{XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR};
    xr_vulkan_instance_create_info.systemId               = platform_xr_system_id;
    xr_vulkan_instance_create_info.pfnGetInstanceProcAddr = &vkGetInstanceProcAddr;
    xr_vulkan_instance_create_info.vulkanCreateInfo       = &instance_info;
    xr_vulkan_instance_create_info.vulkanAllocator        = nullptr;

    PFN_xrCreateVulkanInstanceKHR xrCreateVulkanInstanceKHR = nullptr;
    xrGetInstanceProcAddr(platform_xr_instance, "xrCreateVulkanInstanceKHR", reinterpret_cast<PFN_xrVoidFunction*>(&xrCreateVulkanInstanceKHR));
    xrCreateVulkanInstanceKHR(platform_xr_instance, &xr_vulkan_instance_create_info, &vulkan_instance, &current_vk_result);
  }

  #ifndef NDEBUG
  {
    PFN_vkCreateDebugUtilsMessengerEXT vkCreateDebugUtilsMessengerEXT = nullptr;
    vkCreateDebugUtilsMessengerEXT = (PFN_vkCreateDebugUtilsMessengerEXT) vkGetInstanceProcAddr(vulkan_instance, "vkCreateDebugUtilsMessengerEXT");
    VkDebugUtilsMessengerCreateInfoEXT debug_utils_create_info = {VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
    debug_utils_create_info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
    debug_utils_create_info.messageType     = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT;
    debug_utils_create_info.pfnUserCallback = [](VkDebugUtilsMessageSeverityFlagBitsEXT message_severity, VkDebugUtilsMessageTypeFlagsEXT message_type, const VkDebugUtilsMessengerCallbackDataEXT *callback_data, void *user_data) {
      if (message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
      {
        DEBUG_LOG("Warning %s:%s:%s\n", std::to_string(callback_data->messageIdNumber).data(), callback_data->pMessageIdName, callback_data->pMessage);
      }
      else if (message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
      {
        DEBUG_LOG("Error %s:%s:%s\n", std::to_string(callback_data->messageIdNumber).data(), callback_data->pMessageIdName, callback_data->pMessage);
      }
      return VK_FALSE;
    };
    vkCreateDebugUtilsMessengerEXT(vulkan_instance, &debug_utils_create_info, nullptr, &vulkan_debug_utils_messenger);
  }
  #endif

  {
    XrVulkanGraphicsDeviceGetInfoKHR device_get_info{XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR};
    device_get_info.systemId       = platform_xr_system_id;
    device_get_info.vulkanInstance = vulkan_instance;
    PFN_xrGetVulkanGraphicsDevice2KHR xrGetVulkanGraphicsDevice2KHR = nullptr;
    xrGetInstanceProcAddr(platform_xr_instance, "xrGetVulkanGraphicsDevice2KHR", reinterpret_cast<PFN_xrVoidFunction*>(&xrGetVulkanGraphicsDevice2KHR));
    xrGetVulkanGraphicsDevice2KHR(platform_xr_instance, &device_get_info, &vulkan_physical_device);

    float queue_priorities = 0.0f;
    VkDeviceQueueCreateInfo queue_info{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queue_info.queueCount       = 1;
    queue_info.pQueuePriorities = &queue_priorities;

    uint32_t queue_family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(vulkan_physical_device, &queue_family_count, nullptr);
    std::vector<VkQueueFamilyProperties> queue_family_props(queue_family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(vulkan_physical_device, &queue_family_count, queue_family_props.data());

    vulkan_queue_family_index = 0;
    for (uint32_t i=0; i < queue_family_count; ++i) {
      if (queue_family_props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
        queue_info.queueFamilyIndex = i;
        vulkan_queue_family_index   = i;
        break;
      }
    }

    std::vector<const char*> device_extensions;
    device_extensions.push_back("VK_KHR_device_group");
    device_extensions.push_back("VK_KHR_get_memory_requirements2");
    device_extensions.push_back("VK_KHR_dedicated_allocation");
    device_extensions.push_back("VK_KHR_multiview");
    device_extensions.push_back("VK_KHR_swapchain");

    VkPhysicalDeviceFeatures features{};
    features.shaderStorageImageMultisample = VK_TRUE;
    features.sampleRateShading             = VK_TRUE;
    features.samplerAnisotropy             = VK_TRUE;

    VkPhysicalDeviceMultiviewFeaturesKHR multiview_features{};
    VkPhysicalDeviceFeatures2KHR features_query{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2_KHR};
    multiview_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES_KHR;
    features_query.pNext     = &multiview_features;
    PFN_vkGetPhysicalDeviceFeatures2KHR vkGetPhysicalDeviceFeatures2KHR = reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2KHR>(vkGetInstanceProcAddr(vulkan_instance, "vkGetPhysicalDeviceFeatures2KHR"));
    vkGetPhysicalDeviceFeatures2KHR(vulkan_physical_device, &features_query);

    VkPhysicalDeviceProperties2KHR properties_query{};
    VkPhysicalDeviceMultiviewPropertiesKHR multiview_properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_PROPERTIES_KHR};
    properties_query.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2_KHR;
    properties_query.pNext = &multiview_properties;
    PFN_vkGetPhysicalDeviceProperties2KHR vkGetPhysicalDeviceProperties2KHR = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2KHR>(vkGetInstanceProcAddr(vulkan_instance, "vkGetPhysicalDeviceProperties2KHR"));
    vkGetPhysicalDeviceProperties2KHR(vulkan_physical_device, &properties_query);
    vulkan_physical_device_properties = properties_query.properties;

    assert(vulkan_texture_sampler_max_anisotropy <= vulkan_physical_device_properties.limits.maxSamplerAnisotropy);

    VkDeviceCreateInfo device_info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    device_info.pNext                   = &multiview_features;
    device_info.queueCreateInfoCount    = 1;
    device_info.pQueueCreateInfos       = &queue_info;
    device_info.enabledExtensionCount   = (uint32_t)device_extensions.size();
    device_info.ppEnabledExtensionNames = device_extensions.empty() ? nullptr : device_extensions.data();
    device_info.pEnabledFeatures        = &features;

    XrVulkanDeviceCreateInfoKHR device_create_info{XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR};
    device_create_info.systemId               = platform_xr_system_id;
    device_create_info.pfnGetInstanceProcAddr = &vkGetInstanceProcAddr;
    device_create_info.vulkanCreateInfo       = &device_info;
    device_create_info.vulkanPhysicalDevice   = vulkan_physical_device;
    device_create_info.vulkanAllocator        = nullptr;

    PFN_xrCreateVulkanDeviceKHR xrCreateVulkanDeviceKHR = nullptr;
    xrGetInstanceProcAddr(platform_xr_instance, "xrCreateVulkanDeviceKHR", reinterpret_cast<PFN_xrVoidFunction*>(&xrCreateVulkanDeviceKHR));
    xrCreateVulkanDeviceKHR(platform_xr_instance, &device_create_info, &vulkan_logical_device, &current_vk_result);

    vkGetDeviceQueue(vulkan_logical_device, queue_info.queueFamilyIndex, 0, &vulkan_queue);

    vkGetPhysicalDeviceMemoryProperties(vulkan_physical_device, &vulkan_physical_device_memory_properties);
    assert(vulkan_physical_device_properties.limits.minMemoryMapAlignment >= alignof(std::max_align_t)); // minMemoryMapAlignment is guaranteed to be a power of 2

    VkFormatProperties texture_format_properties;
    vkGetPhysicalDeviceFormatProperties(vulkan_physical_device, VK_FORMAT_R8G8B8A8_SRGB, &texture_format_properties);
    assert(texture_format_properties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT);  // linear blitting check for mip maps

    VkCommandPoolCreateInfo tmp_command_pool_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    tmp_command_pool_info.flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    tmp_command_pool_info.queueFamilyIndex = vulkan_queue_family_index;
    vkCreateCommandPool(vulkan_logical_device, &tmp_command_pool_info, nullptr, &vulkan_temporary_command_pool);
  }

  const size_t draw_buffer_indices_count  = 131072;
  const size_t draw_buffer_vertices_count = 131072;
  VkVertexBuffer* vertex_buffer           = new VkVertexBuffer;
  vulkan_all_meshes.draw_buffer = create_vk_vertex_buffer(draw_buffer_indices_count, draw_buffer_vertices_count, vertex_buffer);
  vulkan_all_meshes.source_file_path_to_id_data.reserve(VkMeshes::max_count);
  memset(&graphics_all_mesh_instances.mesh_ids, VkMeshes::Index(-1), GraphicsMeshInstances::max_count * sizeof(graphics_all_mesh_instances.mesh_ids[0]));

  vulkan_all_skeletons.source_file_path_to_id_data.reserve(VkSkeletons::max_count);
  memset(&graphics_all_skeleton_instances.skeleton_ids, uint16_t(-1), GraphicsSkeletonInstances::max_buffer_data_joint_count * sizeof(graphics_all_skeleton_instances.skeleton_ids[0]));

  vulkan_all_animations.source_file_path_to_id_data.reserve(VkAnimations::max_count);
  for (GraphicsSkeletonInstances::Index i=0; i < (GraphicsSkeletonInstances::Index)std::size(graphics_render_thread_sim_state.animation_states); ++i) {
    graphics_render_thread_sim_state.animation_states[i] = {uint32_t(-1), 0.0f, false};
    graphics_all_skeleton_instances.animation_states[i]  = {uint32_t(-1), 0.0f, false};
  }

  for (uint32_t i=0; i < vulkan_max_frames_in_flight; ++i) {
    VkInstanceBuffer* instance_buffer = new VkInstanceBuffer;
    vulkan_instance_buffers[i] = create_vk_instance_buffer(GraphicsMeshInstances::max_count, instance_buffer);
  }

  // if cannot use SRGB format for base color then need to handle gamma correction in fragment shaders
  vulkan_base_color_map_array = new VkTextureArray;
  create_vk_texture_array(VK_FORMAT_R8G8B8A8_SRGB, 1024, 1024, calculate_mip_level_count(1024,1024), 8, vulkan_base_color_map_array);

  {
    VkSamplerCreateInfo sampler_info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler_info.magFilter               = VK_FILTER_LINEAR;
    sampler_info.minFilter               = VK_FILTER_LINEAR;
    sampler_info.addressModeU            = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_info.addressModeV            = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_info.addressModeW            = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_info.anisotropyEnable        = VK_TRUE;
    sampler_info.maxAnisotropy           = vulkan_texture_sampler_max_anisotropy;
    sampler_info.borderColor             = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    sampler_info.unnormalizedCoordinates = VK_FALSE;
    sampler_info.compareEnable           = VK_FALSE;
    sampler_info.compareOp               = VK_COMPARE_OP_ALWAYS;
    sampler_info.mipmapMode              = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler_info.minLod                  = 0.0f;
    sampler_info.maxLod                  = static_cast<float>(vulkan_base_color_map_array->layered_image_mip_level_count);
    sampler_info.mipLodBias              = 0.0f;
    vkCreateSampler(vulkan_logical_device, &sampler_info, nullptr, &vulkan_texture_sampler);

    for (uint32_t i=0; i < vulkan_max_frames_in_flight; ++i) {
      vulkan_global_uniform_buffers[i] = new VkUniformBuffer;
      create_vk_uniform_buffer<GlobalUniformBufferObject>(vulkan_global_uniform_buffers[i]);

      vulkan_skeleton_instance_uniform_buffers[i] = new VkUniformBuffer;
      create_vk_uniform_buffer<SkeletonInstanceUniformBufferObject>(vulkan_skeleton_instance_uniform_buffers[i]);
    }

    vulkan_material_uniform_buffer = new VkUniformBuffer;
    create_vk_uniform_buffer<MaterialUniformBufferObject>(vulkan_material_uniform_buffer);
  }

  {
    VkDescriptorSetLayoutBinding sampler_layout_binding{};
    sampler_layout_binding.binding            = 0;
    sampler_layout_binding.descriptorCount    = 1;
    sampler_layout_binding.descriptorType     = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    sampler_layout_binding.pImmutableSamplers = nullptr;
    sampler_layout_binding.stageFlags         = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding global_uniform_buffer_layout_binding{};
    global_uniform_buffer_layout_binding.binding            = 1;
    global_uniform_buffer_layout_binding.descriptorCount    = 1;
    global_uniform_buffer_layout_binding.descriptorType     = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    global_uniform_buffer_layout_binding.pImmutableSamplers = nullptr;
    global_uniform_buffer_layout_binding.stageFlags         = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding material_uniform_buffer_layout_binding{};
    material_uniform_buffer_layout_binding.binding            = 2;
    material_uniform_buffer_layout_binding.descriptorCount    = 1;
    material_uniform_buffer_layout_binding.descriptorType     = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    material_uniform_buffer_layout_binding.pImmutableSamplers = nullptr;
    material_uniform_buffer_layout_binding.stageFlags         = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding joint_uniform_buffer_layout_binding{};
    joint_uniform_buffer_layout_binding.binding            = 3;
    joint_uniform_buffer_layout_binding.descriptorCount    = 1;
    joint_uniform_buffer_layout_binding.descriptorType     = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    joint_uniform_buffer_layout_binding.pImmutableSamplers = nullptr;
    joint_uniform_buffer_layout_binding.stageFlags         = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutBinding descriptor_set_bindings[] = { sampler_layout_binding, global_uniform_buffer_layout_binding, material_uniform_buffer_layout_binding, joint_uniform_buffer_layout_binding };

    VkDescriptorSetLayoutCreateInfo descriptor_set_layout_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    descriptor_set_layout_info.bindingCount = static_cast<uint32_t>(std::size(descriptor_set_bindings));
    descriptor_set_layout_info.pBindings    = descriptor_set_bindings;
    vkCreateDescriptorSetLayout(vulkan_logical_device, &descriptor_set_layout_info, nullptr, &vulkan_descriptor_set_layout);

    VkDescriptorPoolSize combined_image_sampler_descriptor_pool_size{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2 }; // one sampler per frame in flight
    VkDescriptorPoolSize uniform_buffer_descriptor_pool_size{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 6 }; // (material buffer + global buffer + joint buffer) per frame in flight
    VkDescriptorPoolSize descriptor_pool_sizes[] = { combined_image_sampler_descriptor_pool_size, uniform_buffer_descriptor_pool_size };

    VkDescriptorPoolCreateInfo descriptor_pool_info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    descriptor_pool_info.poolSizeCount = static_cast<uint32_t>(std::size(descriptor_pool_sizes));
    descriptor_pool_info.pPoolSizes    = descriptor_pool_sizes;
    descriptor_pool_info.maxSets       = vulkan_max_frames_in_flight;
    vkCreateDescriptorPool(vulkan_logical_device, &descriptor_pool_info, nullptr, &vulkan_descriptor_pool);

    VkDescriptorSetLayout descriptor_set_layouts[vulkan_max_frames_in_flight];
    for(uint32_t i=0; i < vulkan_max_frames_in_flight; ++i) {
      descriptor_set_layouts[i] = vulkan_descriptor_set_layout;
    }

    VkDescriptorSetAllocateInfo descriptor_set_allocate_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    descriptor_set_allocate_info.descriptorPool     = vulkan_descriptor_pool;
    descriptor_set_allocate_info.descriptorSetCount = vulkan_max_frames_in_flight;
    descriptor_set_allocate_info.pSetLayouts        = descriptor_set_layouts;
    vkAllocateDescriptorSets(vulkan_logical_device, &descriptor_set_allocate_info, vulkan_descriptor_sets);
  }

  for (uint32_t i=0; i < vulkan_max_frames_in_flight; ++i) {
    VkDescriptorImageInfo descriptor_image_info{};
    descriptor_image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    descriptor_image_info.imageView   = vulkan_base_color_map_array->layered_image_view;
    descriptor_image_info.sampler     = vulkan_texture_sampler;

    VkDescriptorBufferInfo descriptor_global_uniform_buffer_info{};
    descriptor_global_uniform_buffer_info.buffer = vulkan_global_uniform_buffers[i]->buffer;
    descriptor_global_uniform_buffer_info.offset = 0;
    descriptor_global_uniform_buffer_info.range  = sizeof(GlobalUniformBufferObject);

    VkDescriptorBufferInfo descriptor_material_uniform_buffer_info{};
    descriptor_material_uniform_buffer_info.buffer = vulkan_material_uniform_buffer->buffer;
    descriptor_material_uniform_buffer_info.offset = 0;
    descriptor_material_uniform_buffer_info.range  = sizeof(MaterialUniformBufferObject);

    VkDescriptorBufferInfo descriptor_joint_uniform_buffer_info{};
    descriptor_joint_uniform_buffer_info.buffer = vulkan_skeleton_instance_uniform_buffers[i]->buffer;
    descriptor_joint_uniform_buffer_info.offset = 0;
    descriptor_joint_uniform_buffer_info.range  = sizeof(SkeletonInstanceUniformBufferObject);

    VkWriteDescriptorSet texture_sampler_descriptor_write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    texture_sampler_descriptor_write.dstSet          = vulkan_descriptor_sets[i];
    texture_sampler_descriptor_write.dstBinding      = 0;
    texture_sampler_descriptor_write.dstArrayElement = 0;
    texture_sampler_descriptor_write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    texture_sampler_descriptor_write.descriptorCount = 1;
    texture_sampler_descriptor_write.pImageInfo      = &descriptor_image_info;

    VkWriteDescriptorSet global_uniform_buffer_descriptor_write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    global_uniform_buffer_descriptor_write.dstSet          = vulkan_descriptor_sets[i];
    global_uniform_buffer_descriptor_write.dstBinding      = 1;
    global_uniform_buffer_descriptor_write.dstArrayElement = 0;
    global_uniform_buffer_descriptor_write.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    global_uniform_buffer_descriptor_write.descriptorCount = 1;
    global_uniform_buffer_descriptor_write.pBufferInfo     = &descriptor_global_uniform_buffer_info;

    VkWriteDescriptorSet material_uniform_buffer_descriptor_write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    material_uniform_buffer_descriptor_write.dstSet          = vulkan_descriptor_sets[i];
    material_uniform_buffer_descriptor_write.dstBinding      = 2;
    material_uniform_buffer_descriptor_write.dstArrayElement = 0;
    material_uniform_buffer_descriptor_write.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    material_uniform_buffer_descriptor_write.descriptorCount = 1;
    material_uniform_buffer_descriptor_write.pBufferInfo     = &descriptor_material_uniform_buffer_info;

    VkWriteDescriptorSet joint_uniform_buffer_descriptor_write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    joint_uniform_buffer_descriptor_write.dstSet          = vulkan_descriptor_sets[i];
    joint_uniform_buffer_descriptor_write.dstBinding      = 3;
    joint_uniform_buffer_descriptor_write.dstArrayElement = 0;
    joint_uniform_buffer_descriptor_write.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    joint_uniform_buffer_descriptor_write.descriptorCount = 1;
    joint_uniform_buffer_descriptor_write.pBufferInfo     = &descriptor_joint_uniform_buffer_info;

    VkWriteDescriptorSet descriptor_writes[] = { texture_sampler_descriptor_write, global_uniform_buffer_descriptor_write, material_uniform_buffer_descriptor_write, joint_uniform_buffer_descriptor_write };

    vkUpdateDescriptorSets(vulkan_logical_device, static_cast<uint32_t>(std::size(descriptor_writes)), descriptor_writes, 0, nullptr);
  }

  return true;
}

void deactivate_graphics() {
  vkDeviceWaitIdle(vulkan_logical_device);

  destroy_vk_xr_swapchain_image_context(vulkan_xr_swapchain_context);

  for (uint32_t i=0; i < vulkan_max_frames_in_flight; ++i) {
    destroy_vk_uniform_buffer(vulkan_global_uniform_buffers[i]);
    destroy_vk_uniform_buffer(vulkan_skeleton_instance_uniform_buffers[i]);
  }
  destroy_vk_uniform_buffer(vulkan_material_uniform_buffer);

  vkDestroyDescriptorPool(vulkan_logical_device, vulkan_descriptor_pool, nullptr);

  vkDestroySampler(vulkan_logical_device, vulkan_texture_sampler, nullptr);

  destroy_vk_texture_array(vulkan_base_color_map_array);

  vkDestroyCommandPool(vulkan_logical_device, vulkan_temporary_command_pool, nullptr);

  vkDestroyDescriptorSetLayout(vulkan_logical_device, vulkan_descriptor_set_layout, nullptr);

  for (uint32_t i=0; i < vulkan_max_frames_in_flight; ++i) {
    destroy_vk_instance_buffer(vulkan_instance_buffers[i]);
  }

  destroy_vk_vertex_buffer(vulkan_all_meshes.draw_buffer);

  vkDestroyDevice(vulkan_logical_device, nullptr);

  #ifndef NDEBUG
  PFN_vkDestroyDebugUtilsMessengerEXT pfnVkDestoryDebugUtilsMessengerEXT = nullptr;
  pfnVkDestoryDebugUtilsMessengerEXT = (PFN_vkDestroyDebugUtilsMessengerEXT) vkGetInstanceProcAddr(vulkan_instance, "vkDestroyDebugUtilsMessengerEXT");
  pfnVkDestoryDebugUtilsMessengerEXT(vulkan_instance, vulkan_debug_utils_messenger, nullptr);
  #endif

  vkDestroyInstance(vulkan_instance, nullptr);
}

int64_t vulkan_select_color_swapchain_format(const std::vector<int64_t>& runtime_formats) {
  constexpr int64_t supported_color_swapchain_formats[] = { VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_R8G8B8A8_SRGB }; // if need to add VK_FORMAT_B8G8R8A8_UNORM and/or VK_FORMAT_R8G8B8A8_UNORM then need to handle gamma correction in fragment shaders

  auto swapchain_format_iterator = std::find_first_of(runtime_formats.begin(), runtime_formats.end(), std::begin(supported_color_swapchain_formats), std::end(supported_color_swapchain_formats));
  if (swapchain_format_iterator == runtime_formats.end()) {
    DEBUG_LOG("No runtime swapchain format supported for color swapchain\n");
  }
  
  return *swapchain_format_iterator;
}

void vulkan_allocate_xr_swapchain_images(const uint32_t image_count, const XrSwapchainCreateInfo& swapchain_create_info, XrSwapchainImageBaseHeader*& xr_swapchain_images) {
  vulkan_xr_swapchain_context = new VkXrSwapchainContext;
  create_vk_xr_swapchain_context(image_count, swapchain_create_info, vulkan_xr_swapchain_context);
  xr_swapchain_images = reinterpret_cast<XrSwapchainImageBaseHeader*>(vulkan_xr_swapchain_context->images.data());
}

void vulkan_render_xr_views(const std::vector<XrCompositionLayerProjectionView>& xr_views, const uint32_t swapchain_image_index) {
  const uint32_t current_frame = vulkan_xr_swapchain_context->current_frame;
  vulkan_xr_swapchain_context->current_frame = (vulkan_xr_swapchain_context->current_frame + 1) % vulkan_max_frames_in_flight;

  vkWaitForFences(vulkan_logical_device, 1, &vulkan_xr_swapchain_context->frame_in_flight_fences[current_frame], VK_TRUE, UINT64_MAX);
  vkResetFences(vulkan_logical_device, 1, &vulkan_xr_swapchain_context->frame_in_flight_fences[current_frame]);

  static Frustum camera_frustums[2];  // maybe combine frusta (currently using short-circuit optimization so only culling the right-eye-only instances twice)
  {
    GlobalUniformBufferObject current_global_uniform_buffer_object{};
    for (uint32_t view_index=0; view_index < 2; ++view_index) {
      const auto& pose = xr_views[view_index].pose;

      Matrix4x4f projection_matrix;
      create_projection_matrix(xr_views[view_index].fov, 0.05f, 100.0f, projection_matrix);

      Matrix4x4f view_matrix;
      create_view_matrix(pose.position, pose.orientation, view_matrix);

      matrix_4x4f_multiply(projection_matrix, view_matrix, current_global_uniform_buffer_object.view_projection_matrices[view_index]);

      create_frustum_from_view_projection_matrix(current_global_uniform_buffer_object.view_projection_matrices[view_index], &camera_frustums[view_index]);
    }
    current_global_uniform_buffer_object.left_eye_position       = xr_views[0].pose.position;
    current_global_uniform_buffer_object.directional_light       = directional_light.surfaces_to_light_direction;
    current_global_uniform_buffer_object.directional_light_color = directional_light.color;
    current_global_uniform_buffer_object.ambient_light_factor    = ambient_light_intensity;
    memcpy(vulkan_global_uniform_buffers[current_frame]->mapped_memory, &current_global_uniform_buffer_object, sizeof(GlobalUniformBufferObject));
  }

  static Transform skeleton_instances_joint_local_transforms[GraphicsSkeletonInstances::max_buffer_data_joint_count];
  static Transform skeleton_instances_joint_global_transforms[GraphicsSkeletonInstances::max_buffer_data_joint_count];

  // TODO: currently forced to play an animation to render a skinned mesh; should render T-pose by default if not playing an animation

  // update skeleton_instances_joint_local_transforms
  for (GraphicsSkeletonInstances::Index skeleton_instance_index=0; skeleton_instance_index < (GraphicsSkeletonInstances::Index)std::size(graphics_render_thread_sim_state.animation_states); ++skeleton_instance_index) {
    AnimationState& animation_state = graphics_render_thread_sim_state.animation_states[skeleton_instance_index];

    if (animation_state.animation_id != uint32_t(-1)) {
      const Animation& animation = vulkan_all_animations.animations[animation_state.animation_id];

      const uint16_t skeleton_id = graphics_all_skeleton_instances.skeleton_ids[skeleton_instance_index];
      std::vector<GraphicsSkeleton::JointHierarchyNode>& joint_hierarchy = vulkan_all_skeletons.skeletons[skeleton_id].joint_hierarchy;
      for (GraphicsSkeleton::JointHierarchyNode::Index joint_index=0; joint_index < GraphicsSkeleton::JointHierarchyNode::Index(joint_hierarchy.size()); ++joint_index) {
        skeleton_instances_joint_local_transforms[GraphicsSkeleton::JointHierarchyNode::Index(skeleton_instance_index) + joint_index] = joint_hierarchy[joint_index].local_transform;
      }

       for (auto& channel : animation.channels) {
        const Animation::Sampler& sampler = animation.samplers[channel.sampler_index];

        for (size_t sampler_input_index=0; sampler_input_index < sampler.inputs.size() - 1; ++sampler_input_index) {
          if (!( (animation_state.time_seconds >= sampler.inputs[sampler_input_index]) && (animation_state.time_seconds <= sampler.inputs[sampler_input_index + 1]) )) {
            continue;
          }
          
          Transform& target_joint_transform = skeleton_instances_joint_local_transforms[GraphicsSkeleton::JointHierarchyNode::Index(skeleton_instance_index) + channel.target.joint_index];
          assert(sampler.interpolation != Animation::Sampler::Interpolation::CubicSpline);
          switch (sampler.interpolation) {
            case Animation::Sampler::Interpolation::Linear : {
              float interpolation_amount = (animation_state.time_seconds - sampler.inputs[sampler_input_index]) / (sampler.inputs[sampler_input_index + 1] - sampler.inputs[sampler_input_index]);

              switch (channel.target.path) {
                case Animation::Channel::Path::Translation : {
                  const size_t sampler_output_index = sampler_input_index * 3;
                  Vector3f output_start = { sampler.outputs[sampler_output_index], sampler.outputs[sampler_output_index + 1], sampler.outputs[sampler_output_index + 2] };
                  Vector3f output_end   = { sampler.outputs[sampler_output_index + 3], sampler.outputs[sampler_output_index + 4], sampler.outputs[sampler_output_index + 5] };
                  target_joint_transform.translation = lerp(output_start, output_end, interpolation_amount);
                  break;
                }
                case Animation::Channel::Path::Rotation : {
                  const size_t sampler_output_index = sampler_input_index * 4;
                  Quaternionf output_start        = { sampler.outputs[sampler_output_index], sampler.outputs[sampler_output_index + 1], sampler.outputs[sampler_output_index + 2], sampler.outputs[sampler_output_index + 3] };
                  Quaternionf output_end          = { sampler.outputs[sampler_output_index + 4], sampler.outputs[sampler_output_index + 5], sampler.outputs[sampler_output_index + 6], sampler.outputs[sampler_output_index + 7] };
                  target_joint_transform.rotation = normalize(slerp(output_start, output_end, interpolation_amount));
                  break;
                }
                case Animation::Channel::Path::Scale : {
                  const size_t sampler_output_index = sampler_input_index * 3;
                  Vector3f output_start        = { sampler.outputs[sampler_output_index], sampler.outputs[sampler_output_index + 1], sampler.outputs[sampler_output_index + 2] };
                  Vector3f output_end          = { sampler.outputs[sampler_output_index + 3], sampler.outputs[sampler_output_index + 4], sampler.outputs[sampler_output_index + 5] };
                  target_joint_transform.scale = lerp(output_start, output_end, interpolation_amount);
                  break;
                }
              }
              break;
            }
            case Animation::Sampler::Interpolation::Step : {
              switch (channel.target.path) {
                case Animation::Channel::Path::Translation : {
                  const size_t sampler_output_index = sampler_input_index * 3;
                  if (animation_state.time_seconds != sampler.inputs[sampler_input_index + 1]) {
                    target_joint_transform.translation = { sampler.outputs[sampler_output_index], sampler.outputs[sampler_output_index + 1], sampler.outputs[sampler_output_index + 2] };
                  } else {
                    target_joint_transform.translation = { sampler.outputs[sampler_output_index + 3], sampler.outputs[sampler_output_index + 4], sampler.outputs[sampler_output_index + 5] };
                  }
                  break;
                }
                case Animation::Channel::Path::Rotation : {
                  const size_t sampler_output_index = sampler_input_index * 4;
                  if (animation_state.time_seconds != sampler.inputs[sampler_input_index + 1]) {
                    target_joint_transform.rotation = { sampler.outputs[sampler_output_index], sampler.outputs[sampler_output_index + 1], sampler.outputs[sampler_output_index + 2], sampler.outputs[sampler_output_index + 3] };
                  } else {
                    target_joint_transform.rotation = { sampler.outputs[sampler_output_index + 4], sampler.outputs[sampler_output_index + 5], sampler.outputs[sampler_output_index + 6] };
                  }
                  break;
                }
                case Animation::Channel::Path::Scale : {
                  const size_t sampler_output_index = sampler_input_index * 3;
                  if (animation_state.time_seconds != sampler.inputs[sampler_input_index + 1]) {
                    target_joint_transform.scale = { sampler.outputs[sampler_output_index], sampler.outputs[sampler_output_index + 1], sampler.outputs[sampler_output_index + 2] };
                  } else {
                    target_joint_transform.scale = { sampler.outputs[sampler_output_index + 3], sampler.outputs[sampler_output_index + 4], sampler.outputs[sampler_output_index + 5] };
                  }
                  break;
                }
              }
              break;
            }
          }
          break;
        }
      }
    }
  }

  // update skeleton_instances_joint_global_transforms using skeleton_instances_joint_local_transforms
  for (GraphicsSkeletonInstances::Index skeleton_instance_index=0; skeleton_instance_index < (GraphicsSkeletonInstances::Index)std::size(graphics_render_thread_sim_state.animation_states); ++skeleton_instance_index) {
    AnimationState& animation_state = graphics_render_thread_sim_state.animation_states[skeleton_instance_index];

    if (animation_state.animation_id != uint32_t(-1)) {
      // graphics_all_skeleton_instances skeleton_ids assumed not to be changed in simulation thread
      const uint16_t skeleton_id = graphics_all_skeleton_instances.skeleton_ids[skeleton_instance_index];
      std::vector<GraphicsSkeleton::JointHierarchyNode>& joint_hierarchy = vulkan_all_skeletons.skeletons[skeleton_id].joint_hierarchy;

      auto update_global_transforms = [&skeleton_instance_index](std::vector<GraphicsSkeleton::JointHierarchyNode>& hierarchy, GraphicsSkeleton::JointHierarchyNode::Index node_index) {
        Transform& global_transform = skeleton_instances_joint_global_transforms[ GraphicsSkeleton::JointHierarchyNode::Index(skeleton_instance_index) + node_index];
        global_transform = skeleton_instances_joint_local_transforms[GraphicsSkeleton::JointHierarchyNode::Index(skeleton_instance_index) + node_index];

        GraphicsSkeleton::JointHierarchyNode::Index parent_index = hierarchy[node_index].parent_index;
        while (parent_index != GraphicsSkeleton::JointHierarchyNode::Index(-1)) {
          combine_transform(skeleton_instances_joint_local_transforms[GraphicsSkeleton::JointHierarchyNode::Index(skeleton_instance_index) + parent_index], global_transform);
          parent_index = hierarchy[parent_index].parent_index;
        }
      };

      for (GraphicsSkeleton::JointHierarchyNode::Index current_joint_index=0; current_joint_index < joint_hierarchy.size(); ++current_joint_index) {
        update_global_transforms(joint_hierarchy, current_joint_index);
      }
    }
  }

  // update joint_matrices for uniform buffer
  for (GraphicsSkeletonInstances::Index skeleton_instance_index=0; skeleton_instance_index < (GraphicsSkeletonInstances::Index)std::size(graphics_render_thread_sim_state.animation_states); ++skeleton_instance_index) {
    AnimationState& animation_state = graphics_render_thread_sim_state.animation_states[skeleton_instance_index];

    if (animation_state.animation_id != uint32_t(-1)) {
      // graphics_all_skeleton_instances skeleton_ids assumed not to be changed in simulation thread
      const uint16_t skeleton_id = graphics_all_skeleton_instances.skeleton_ids[skeleton_instance_index];
      GraphicsSkeleton& skeleton = vulkan_all_skeletons.skeletons[skeleton_id];

      for (GraphicsSkeletonInstances::Index joint_index=0; joint_index < GraphicsSkeletonInstances::Index(skeleton.joint_count()); ++joint_index) {
        Matrix4x4f target_joint_transform_matrix;
        create_transform_matrix(skeleton_instances_joint_global_transforms[GraphicsSkeletonInstances::Index(skeleton_instance_index) + joint_index], target_joint_transform_matrix);
        matrix_4x4f_multiply(target_joint_transform_matrix, skeleton.inverse_bind_matrices[joint_index], current_render_views_skeleton_instances_ubo.skeleton_instances.joint_matrices[GraphicsSkeletonInstances::Index(skeleton_instance_index) + joint_index]);
      }
    }
  }
  memcpy(vulkan_skeleton_instance_uniform_buffers[current_frame]->mapped_memory, &current_render_views_skeleton_instances_ubo, sizeof(SkeletonInstanceUniformBufferObject));

  uint32_t current_instance_offset = 0;
  size_t   current_draw_call_count = 0;
  std::bitset<VkMeshes::max_count> is_mesh_draw_call_created_bits;
  VkIndexedDrawCall current_draw_call;

  vkResetCommandBuffer(vulkan_xr_swapchain_context->command_buffers[current_frame], 0);
  VkCommandBufferBeginInfo command_begin_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  vkBeginCommandBuffer(vulkan_xr_swapchain_context->command_buffers[current_frame], &command_begin_info);

  for (uint32_t i=0; i < GraphicsMeshInstances::max_count; ++i) {
    const uint32_t current_mesh_id = graphics_render_thread_sim_state.mesh_instances.mesh_ids[i];
    if (current_mesh_id == VkMeshes::Index(-1)) {
      continue;
    }

    if (!is_mesh_draw_call_created_bits[current_mesh_id]) {
      VkMeshes::HotDrawData current_mesh_hot_data = vulkan_all_meshes.all_hot_draw_data[current_mesh_id];
      const size_t current_draw_call_index        = current_draw_call_count;

      current_draw_call.index_count    = current_mesh_hot_data.index_count;
      current_draw_call.instance_count = 0;
      current_draw_call.first_index    = current_mesh_hot_data.first_index;
      current_draw_call.vertex_offset  = current_mesh_hot_data.first_vertex;
      current_draw_call.first_instance = current_instance_offset;

      for (uint32_t current_mesh_instance_index=i; current_mesh_instance_index < GraphicsMeshInstances::max_count; ++current_mesh_instance_index) {
        if (current_mesh_id == graphics_render_thread_sim_state.mesh_instances.mesh_ids[current_mesh_instance_index]) {
          const GraphicsMeshInstances::DrawData& draw_data = graphics_render_thread_sim_state.mesh_instances.draw_data[current_mesh_instance_index];

          if (is_sphere_volume_visible(draw_data.cull_data.position,draw_data.cull_data.radius,camera_frustums[0]) || is_sphere_volume_visible(draw_data.cull_data.position,draw_data.cull_data.radius,camera_frustums[1])) {
            vulkan_instance_buffers[current_frame]->update(vulkan_xr_swapchain_context->command_buffers[current_frame], &draw_data.buffer_data, 1, current_draw_call.first_instance + current_draw_call.instance_count);
            current_draw_call.instance_count += 1;
          }
        }
      }

      if (current_draw_call.instance_count > 0) {
        vulkan_all_meshes.all_indexed_draw_calls[current_draw_call_index] = current_draw_call;
        ++current_draw_call_count;
        current_instance_offset += current_draw_call.instance_count;
      }
      is_mesh_draw_call_created_bits[current_mesh_id] = true;
    }
  }

  static VkClearValue clear_values[2];
  clear_values[0].color        = { {0.0f, 0.0f, 0.0f, 0.0f} };
  clear_values[1].depthStencil = { 1.0f, 0 };

  VkRenderPassBeginInfo render_pass_begin_info{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
  render_pass_begin_info.clearValueCount = 2;
  render_pass_begin_info.pClearValues    = clear_values;

  vulkan_xr_swapchain_context->bind_render_target(swapchain_image_index, &render_pass_begin_info);

  VkDeviceSize vertex_buffer_offsets[2] = { vulkan_all_meshes.draw_buffer->vertices_partition_offset, 0 };
  VkBuffer vertex_buffers[2]            = { vulkan_all_meshes.draw_buffer->buffer, vulkan_instance_buffers[current_frame]->buffer };
  vkCmdBeginRenderPass(vulkan_xr_swapchain_context->command_buffers[current_frame], &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);
  vkCmdBindDescriptorSets(vulkan_xr_swapchain_context->command_buffers[current_frame], VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_xr_swapchain_context->pipeline_layout, 0, 1, &vulkan_descriptor_sets[current_frame], 0, nullptr);
  vkCmdBindPipeline(vulkan_xr_swapchain_context->command_buffers[current_frame], VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_xr_swapchain_context->pipeline);
  vkCmdBindVertexBuffers(vulkan_xr_swapchain_context->command_buffers[current_frame], 0, static_cast<uint32_t>(std::size(vertex_buffers)), vertex_buffers, vertex_buffer_offsets);
  vkCmdBindIndexBuffer(vulkan_xr_swapchain_context->command_buffers[current_frame], vulkan_all_meshes.draw_buffer->buffer, 0, get_vulkan_index_type());

  assert(xr_views.size() == 2); // one per eye

  for(size_t i=0; i < current_draw_call_count; ++i) {
    //vkCmdPushConstants(vulkan_xr_swapchain_context->command_buffers[current_frame], vulkan_xr_swapchain_context->pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(mvp_matrices), &mvp_matrices);
    const VkIndexedDrawCall& draw_call = vulkan_all_meshes.all_indexed_draw_calls[i];
    vkCmdDrawIndexed(vulkan_xr_swapchain_context->command_buffers[current_frame], draw_call.index_count, draw_call.instance_count, draw_call.first_index, draw_call.vertex_offset, draw_call.first_instance);
  }

  vkCmdEndRenderPass(vulkan_xr_swapchain_context->command_buffers[current_frame]);
  vkEndCommandBuffer(vulkan_xr_swapchain_context->command_buffers[current_frame]);

  VkSubmitInfo submit_info{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  submit_info.signalSemaphoreCount = 1;
  submit_info.pSignalSemaphores    = &vulkan_xr_swapchain_context->render_finished_semaphores[current_frame];
  submit_info.commandBufferCount   = 1;
  submit_info.pCommandBuffers      = &vulkan_xr_swapchain_context->command_buffers[current_frame];

  XrSwapchainImageWaitInfo wait_info{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
  wait_info.timeout = XR_INFINITE_DURATION;
  xrWaitSwapchainImage(xr_views[0].subImage.swapchain, &wait_info); // both xr_views have same swapchain handle

  vkQueueSubmit(vulkan_queue, 1, &submit_info, vulkan_xr_swapchain_context->frame_in_flight_fences[current_frame]);
}

uint32_t get_max_material_count() {
  return MaterialUniformBufferObject::max_count;
}

void update_ambient_light_intensity(const float intensity) {
  ambient_light_intensity = intensity;
}

void AnimationState::tick_time() {
  this->time_seconds += delta_time_seconds * static_cast<float>(this->animation_id != uint32_t(-1));

  const Animation& animation   = vulkan_all_animations.animations[this->animation_id];
  const bool is_animation_over = this->time_seconds > animation.end_time_seconds;
  const float duration         = animation.end_time_seconds - animation.start_time_seconds;
  const float time_passed      = this->time_seconds - animation.start_time_seconds;

  this->time_seconds =
    ( this->time_seconds * static_cast<float>(!is_animation_over) ) + 
    ( (fmod(time_passed, duration) + animation.start_time_seconds) * static_cast<float>(is_animation_over && this->is_looping) ) +
    ( animation.end_time_seconds * static_cast<float>(is_animation_over && !this->is_looping) );
}

bool vulkan_init_win32_mirror_window_swapchain() {
  vulkan_mirror_swapchain_context = new VkMirrorSwapchainContext;

  VkWin32SurfaceCreateInfoKHR surface_info{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
  surface_info.hinstance = platform_win32_instance;
  surface_info.hwnd      = platform_win32_mirror_window;
  vkCreateWin32SurfaceKHR(vulkan_instance, &surface_info, nullptr, &vulkan_mirror_swapchain_context->surface);

  VkBool32 is_present_supported = false;
  vkGetPhysicalDeviceSurfaceSupportKHR(vulkan_physical_device, vulkan_queue_family_index, vulkan_mirror_swapchain_context->surface, &is_present_supported);
  assert(is_present_supported); // this queue doesn't support presenting to this surface
  if (!is_present_supported) {
    vkDestroySurfaceKHR(vulkan_instance, vulkan_mirror_swapchain_context->surface, nullptr);
    return false;
  }

  VkSurfaceCapabilitiesKHR surface_capabilities;
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vulkan_physical_device, vulkan_mirror_swapchain_context->surface, &surface_capabilities);
  vulkan_mirror_swapchain_context->image_size = surface_capabilities.minImageExtent;

  uint32_t surface_format_count = 0;
  vkGetPhysicalDeviceSurfaceFormatsKHR(vulkan_physical_device, vulkan_mirror_swapchain_context->surface, &surface_format_count, nullptr);
  std::vector<VkSurfaceFormatKHR> surface_formats(surface_format_count);
  vkGetPhysicalDeviceSurfaceFormatsKHR(vulkan_physical_device, vulkan_mirror_swapchain_context->surface, &surface_format_count, surface_formats.data());
  vulkan_mirror_swapchain_context->surface_format = {VK_FORMAT_B8G8R8A8_UNORM};
  uint32_t selected_surface_format;
  for (selected_surface_format=0; selected_surface_format < surface_format_count; ++selected_surface_format) {
    if (surface_formats[selected_surface_format].format == vulkan_mirror_swapchain_context->surface_format) break;
  }

  uint32_t present_mode_count = 0;
  vkGetPhysicalDeviceSurfacePresentModesKHR(vulkan_physical_device, vulkan_mirror_swapchain_context->surface, &present_mode_count, nullptr);
  std::vector<VkPresentModeKHR> present_modes(present_mode_count);
  vkGetPhysicalDeviceSurfacePresentModesKHR(vulkan_physical_device, vulkan_mirror_swapchain_context->surface, &present_mode_count, present_modes.data());

  VkPresentModeKHR selected_present_mode = VK_PRESENT_MODE_IMMEDIATE_KHR;
  for (uint32_t i=0; i < present_mode_count; ++i) {
    if ( (present_modes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR) || (present_modes[i] == VK_PRESENT_MODE_MAILBOX_KHR) ) {
      selected_present_mode = present_modes[i];
      break;
    }
  }

  VkSwapchainCreateInfoKHR mirror_swapchain_info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
  mirror_swapchain_info.flags                 = 0;
  mirror_swapchain_info.surface               = vulkan_mirror_swapchain_context->surface;
  mirror_swapchain_info.minImageCount         = surface_capabilities.minImageCount;
  mirror_swapchain_info.imageFormat           = vulkan_mirror_swapchain_context->surface_format;
  mirror_swapchain_info.imageColorSpace       = surface_formats[selected_surface_format].colorSpace;
  mirror_swapchain_info.imageExtent           = vulkan_mirror_swapchain_context->image_size;
  mirror_swapchain_info.imageArrayLayers      = 1;
  mirror_swapchain_info.imageUsage            = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  mirror_swapchain_info.imageSharingMode      = VK_SHARING_MODE_EXCLUSIVE;
  mirror_swapchain_info.queueFamilyIndexCount = 0;
  mirror_swapchain_info.pQueueFamilyIndices   = nullptr;
  mirror_swapchain_info.preTransform          = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
  mirror_swapchain_info.compositeAlpha        = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  mirror_swapchain_info.presentMode           = selected_present_mode;
  mirror_swapchain_info.clipped               = true;
  mirror_swapchain_info.oldSwapchain          = VK_NULL_HANDLE;
  vkCreateSwapchainKHR(vulkan_logical_device, &mirror_swapchain_info, nullptr, &vulkan_mirror_swapchain_context->swapchain);

  vulkan_mirror_swapchain_context->image_count = 0;
  vkGetSwapchainImagesKHR(vulkan_logical_device, vulkan_mirror_swapchain_context->swapchain, &vulkan_mirror_swapchain_context->image_count, nullptr);
  vkGetSwapchainImagesKHR(vulkan_logical_device, vulkan_mirror_swapchain_context->swapchain, &vulkan_mirror_swapchain_context->image_count, vulkan_mirror_swapchain_context->images);
  if (vulkan_mirror_swapchain_context->image_count > VkMirrorSwapchainContext::max_image_count) {
    vulkan_mirror_swapchain_context->image_count = VkMirrorSwapchainContext::max_image_count;
  }

  VkSemaphoreCreateInfo semaphore_info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
  VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
  fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
  for (uint32_t i=0; i < vulkan_max_frames_in_flight; ++i) {
    vkCreateSemaphore(vulkan_logical_device, &semaphore_info, nullptr, &vulkan_mirror_swapchain_context->image_available_semaphores[i]);
    vkCreateSemaphore(vulkan_logical_device, &semaphore_info, nullptr, &vulkan_mirror_swapchain_context->render_finished_semaphores[i]);
    vkCreateFence(vulkan_logical_device, &fence_info, nullptr, &vulkan_mirror_swapchain_context->frame_in_flight_fences[i]);
  }

  VkCommandPoolCreateInfo command_pool_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  command_pool_info.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  command_pool_info.queueFamilyIndex = vulkan_queue_family_index;
  vkCreateCommandPool(vulkan_logical_device, &command_pool_info, nullptr, &vulkan_mirror_swapchain_context->command_pool);

  vulkan_mirror_swapchain_context->command_buffers.resize(vulkan_max_frames_in_flight);
  VkCommandBufferAllocateInfo command_buffer_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  command_buffer_info.commandPool        = vulkan_mirror_swapchain_context->command_pool;
  command_buffer_info.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  command_buffer_info.commandBufferCount = vulkan_max_frames_in_flight;
  vkAllocateCommandBuffers(vulkan_logical_device, &command_buffer_info, vulkan_mirror_swapchain_context->command_buffers.data());

  const uint32_t crop_width  = (vulkan_xr_swapchain_context->image_size.width < vulkan_mirror_swapchain_context->image_size.width) ? vulkan_xr_swapchain_context->image_size.width : vulkan_mirror_swapchain_context->image_size.width;
  const uint32_t crop_height = static_cast<uint32_t>(  ( static_cast<float>(vulkan_mirror_swapchain_context->image_size.height) / static_cast<float>(vulkan_mirror_swapchain_context->image_size.width) ) * static_cast<float>(crop_width)  );

  vulkan_mirror_swapchain_context->crop_width_src_offset_0  = (vulkan_xr_swapchain_context->image_size.width - crop_width) / 2;
  vulkan_mirror_swapchain_context->crop_height_src_offset_0 = (vulkan_xr_swapchain_context->image_size.height - crop_height) / 2;
  vulkan_mirror_swapchain_context->crop_width_src_offset_1  = vulkan_mirror_swapchain_context->crop_width_src_offset_0 + crop_width;
  vulkan_mirror_swapchain_context->crop_height_src_offset_1 = vulkan_mirror_swapchain_context->crop_height_src_offset_0 + crop_height;

  return true;
}

void vulkan_render_win32_mirror_window(const uint32_t xr_image_index) {
  const uint32_t current_frame = vulkan_mirror_swapchain_context->current_frame;
  vulkan_mirror_swapchain_context->current_frame = (vulkan_mirror_swapchain_context->current_frame + 1) % vulkan_max_frames_in_flight;

  vkWaitForFences(vulkan_logical_device, 1, &vulkan_mirror_swapchain_context->frame_in_flight_fences[current_frame], VK_TRUE, UINT64_MAX);
  vkResetFences(vulkan_logical_device, 1, &vulkan_mirror_swapchain_context->frame_in_flight_fences[current_frame]);

  uint32_t mirror_image_index = 0;
  vkAcquireNextImageKHR(vulkan_logical_device, vulkan_mirror_swapchain_context->swapchain, UINT64_MAX, vulkan_mirror_swapchain_context->image_available_semaphores[current_frame], VK_NULL_HANDLE, &mirror_image_index);

  vkResetCommandBuffer(vulkan_mirror_swapchain_context->command_buffers[current_frame], 0);
  VkCommandBufferBeginInfo command_begin_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  vkBeginCommandBuffer(vulkan_mirror_swapchain_context->command_buffers[current_frame], &command_begin_info);

  VkImageBlit image_blit{};
  image_blit.srcSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
  image_blit.srcSubresource.layerCount     = 1;
  image_blit.srcSubresource.baseArrayLayer = 0;
  image_blit.srcSubresource.mipLevel       = 0;
  image_blit.srcOffsets[0].x               = vulkan_mirror_swapchain_context->crop_width_src_offset_0;
  image_blit.srcOffsets[0].y               = vulkan_mirror_swapchain_context->crop_height_src_offset_0;
  image_blit.srcOffsets[1].x               = vulkan_mirror_swapchain_context->crop_width_src_offset_1;
  image_blit.srcOffsets[1].y               = vulkan_mirror_swapchain_context->crop_height_src_offset_1;
  image_blit.srcOffsets[1].z               = 1;
  image_blit.dstSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
  image_blit.dstSubresource.layerCount     = 1;
  image_blit.srcSubresource.baseArrayLayer = 0;
  image_blit.dstSubresource.mipLevel       = 0;
  image_blit.dstOffsets[1].x               = vulkan_mirror_swapchain_context->image_size.width;
  image_blit.dstOffsets[1].y               = vulkan_mirror_swapchain_context->image_size.height;
  image_blit.dstOffsets[1].z               = 1;

  // transition mirror image layout: undefined (anything) -> transfer destination
  VkImageMemoryBarrier mirror_image_undefined_to_transfer_dst_barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  mirror_image_undefined_to_transfer_dst_barrier.srcAccessMask    = VK_ACCESS_MEMORY_READ_BIT;
  mirror_image_undefined_to_transfer_dst_barrier.dstAccessMask    = VK_ACCESS_TRANSFER_WRITE_BIT;
  mirror_image_undefined_to_transfer_dst_barrier.oldLayout        = VK_IMAGE_LAYOUT_UNDEFINED;
  mirror_image_undefined_to_transfer_dst_barrier.newLayout        = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  mirror_image_undefined_to_transfer_dst_barrier.image            = vulkan_mirror_swapchain_context->images[mirror_image_index];
  mirror_image_undefined_to_transfer_dst_barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
  vkCmdPipelineBarrier(vulkan_mirror_swapchain_context->command_buffers[current_frame], VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &mirror_image_undefined_to_transfer_dst_barrier);

  // transition xr swapchain image layout: color optimal -> transfer source
  VkImageMemoryBarrier xr_swapchain_image_color_optimal_to_transfer_src_barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  xr_swapchain_image_color_optimal_to_transfer_src_barrier.srcAccessMask       = VK_ACCESS_MEMORY_READ_BIT;
  xr_swapchain_image_color_optimal_to_transfer_src_barrier.dstAccessMask       = VK_ACCESS_TRANSFER_READ_BIT;
  xr_swapchain_image_color_optimal_to_transfer_src_barrier.oldLayout           = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  xr_swapchain_image_color_optimal_to_transfer_src_barrier.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  xr_swapchain_image_color_optimal_to_transfer_src_barrier.image               = vulkan_xr_swapchain_context->images[xr_image_index].image;
  xr_swapchain_image_color_optimal_to_transfer_src_barrier.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
  vkCmdPipelineBarrier(vulkan_mirror_swapchain_context->command_buffers[current_frame], VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &xr_swapchain_image_color_optimal_to_transfer_src_barrier);

  vkCmdBlitImage(vulkan_mirror_swapchain_context->command_buffers[current_frame], vulkan_xr_swapchain_context->images[xr_image_index].image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, vulkan_mirror_swapchain_context->images[mirror_image_index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &image_blit, VK_FILTER_LINEAR);

  // transition mirror image layout: transfer destination -> present source
  VkImageMemoryBarrier mirror_image_transfer_dst_to_present_src_barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  mirror_image_transfer_dst_to_present_src_barrier.srcAccessMask    = VK_ACCESS_TRANSFER_WRITE_BIT;
  mirror_image_transfer_dst_to_present_src_barrier.dstAccessMask    = VK_ACCESS_MEMORY_READ_BIT;
  mirror_image_transfer_dst_to_present_src_barrier.oldLayout        = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  mirror_image_transfer_dst_to_present_src_barrier.newLayout        = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  mirror_image_transfer_dst_to_present_src_barrier.image            = vulkan_mirror_swapchain_context->images[mirror_image_index];
  mirror_image_transfer_dst_to_present_src_barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
  vkCmdPipelineBarrier(vulkan_mirror_swapchain_context->command_buffers[current_frame], VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &mirror_image_transfer_dst_to_present_src_barrier);

  // transition xr swapchain image layout: transfer source -> color optimal
  VkImageMemoryBarrier xr_swapchain_image_transfer_src_to_color_optimal_barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  xr_swapchain_image_transfer_src_to_color_optimal_barrier.srcAccessMask       = VK_ACCESS_TRANSFER_READ_BIT;
  xr_swapchain_image_transfer_src_to_color_optimal_barrier.dstAccessMask       = VK_ACCESS_MEMORY_READ_BIT;
  xr_swapchain_image_transfer_src_to_color_optimal_barrier.oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  xr_swapchain_image_transfer_src_to_color_optimal_barrier.newLayout           = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  xr_swapchain_image_transfer_src_to_color_optimal_barrier.image               = vulkan_xr_swapchain_context->images[xr_image_index].image;
  xr_swapchain_image_transfer_src_to_color_optimal_barrier.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
  vkCmdPipelineBarrier(vulkan_mirror_swapchain_context->command_buffers[current_frame], VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &xr_swapchain_image_transfer_src_to_color_optimal_barrier);

  vkEndCommandBuffer(vulkan_mirror_swapchain_context->command_buffers[current_frame]);

  VkSemaphore wait_semaphores[]      = { vulkan_xr_swapchain_context->render_finished_semaphores[current_frame], vulkan_mirror_swapchain_context->image_available_semaphores[current_frame] };
  VkPipelineStageFlags wait_stages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
  VkSubmitInfo submit_info{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  submit_info.waitSemaphoreCount   = 2;
  submit_info.pWaitSemaphores      = wait_semaphores;
  submit_info.pWaitDstStageMask    = wait_stages;
  submit_info.signalSemaphoreCount = 1;
  submit_info.pSignalSemaphores    = &vulkan_mirror_swapchain_context->render_finished_semaphores[current_frame];
  submit_info.commandBufferCount   = 1;
  submit_info.pCommandBuffers      = &vulkan_mirror_swapchain_context->command_buffers[current_frame];

  vkQueueSubmit(vulkan_queue, 1, &submit_info, vulkan_mirror_swapchain_context->frame_in_flight_fences[current_frame]);

  VkPresentInfoKHR present_info{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
  present_info.waitSemaphoreCount = 1;
  present_info.pWaitSemaphores    = &vulkan_mirror_swapchain_context->render_finished_semaphores[current_frame];
  present_info.swapchainCount     = 1;
  present_info.pSwapchains        = &vulkan_mirror_swapchain_context->swapchain;
  present_info.pImageIndices      = &mirror_image_index;
  vkQueuePresentKHR(vulkan_queue, &present_info);
}

void vulkan_deactivate_win32_mirror_window_swapchain() {
  vkWaitForFences(vulkan_logical_device, vulkan_max_frames_in_flight, vulkan_mirror_swapchain_context->frame_in_flight_fences, VK_TRUE, UINT64_MAX);

  vkDestroySwapchainKHR(vulkan_logical_device, vulkan_mirror_swapchain_context->swapchain, nullptr);
  for (uint32_t i=0; i < vulkan_max_frames_in_flight; ++i) {
    vkDestroySemaphore(vulkan_logical_device, vulkan_mirror_swapchain_context->image_available_semaphores[i], nullptr);
    vkDestroySemaphore(vulkan_logical_device, vulkan_mirror_swapchain_context->render_finished_semaphores[i], nullptr);
    vkDestroyFence(vulkan_logical_device, vulkan_mirror_swapchain_context->frame_in_flight_fences[i], nullptr);
  }

  vkDestroyCommandPool(vulkan_logical_device, vulkan_mirror_swapchain_context->command_pool, nullptr);

  vkDestroySurfaceKHR(vulkan_instance, vulkan_mirror_swapchain_context->surface, nullptr);
}
