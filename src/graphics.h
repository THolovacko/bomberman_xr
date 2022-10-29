#ifndef INCLUDE_TOM_ENGINE_GRAPHICS_H
#define INCLUDE_TOM_ENGINE_GRAPHICS_H

#include "maths.h"
#include <vector>

typedef uint32_t GraphicsIndex;

struct GraphicsVertex {
  Vector3f position;
  Vector3f normal;
  Vector2f texture_coordinates;
  Vector4f joint_indices; // max 4 joints
  Vector4f joint_weights; // one weight per joint

  GraphicsVertex() = default;
  GraphicsVertex(Vector3f p_position, Vector3f p_normal) : position(p_position), normal(p_normal) {}

  bool operator==(const GraphicsVertex& other) const {
    return ( memcmp(&position, &other.position, sizeof(Vector3f) ) == 0 ) &&
           ( memcmp(&normal, &other.normal, sizeof(Vector3f) ) == 0 )     &&
           ( memcmp(&texture_coordinates, &other.texture_coordinates, sizeof(Vector2f) ) == 0 );
  }
};

struct GraphicsMesh {
  std::vector<GraphicsVertex> vertices;
  std::vector<GraphicsIndex>  indices;
  float sphere_volume_radius;
};

struct GraphicsMeshInstances {
  static constexpr uint32_t max_count = 2048;

  struct BufferData {
    Vector3f model_matrix_column_0;
    Vector3f model_matrix_column_1;
    Vector3f model_matrix_column_2;
    Vector3f model_matrix_column_3;
    uint32_t material_id;
    uint32_t joint_index;

    BufferData() = default;
    BufferData(const Transform& current_transform, const uint32_t material_id, const uint32_t joint_index) {
      Matrix4x4f model_matrix;
      create_transform_matrix(current_transform, model_matrix);

      this->model_matrix_column_0 = { model_matrix.m[0],  model_matrix.m[1],  model_matrix.m[2]  };
      this->model_matrix_column_1 = { model_matrix.m[4],  model_matrix.m[5],  model_matrix.m[6]  };
      this->model_matrix_column_2 = { model_matrix.m[8],  model_matrix.m[9],  model_matrix.m[10] };
      this->model_matrix_column_3 = { model_matrix.m[12], model_matrix.m[13], model_matrix.m[14] };

      this->material_id = material_id;
      this->joint_index = joint_index;
    }
  };

  struct CullData {
    Vector3f position;
    float    radius;
  };

  struct DrawData {
    BufferData buffer_data;
    CullData   cull_data;
  };

  uint32_t mesh_ids[max_count];
  DrawData draw_data[max_count];
  float    skeleton_radius_overrides[max_count];

  uint32_t allocate_mesh_instances(const uint32_t instance_count) {
    static uint32_t current_mesh_instance_index = 0;

    assert( (current_mesh_instance_index + (instance_count - 1)) < max_count );
    const uint32_t result = current_mesh_instance_index;
    current_mesh_instance_index += instance_count;

    return result;
  };
};

struct GraphicsMeshInstanceArray {
  uint32_t first_mesh_instance;
  uint32_t size;
};

struct GraphicsMaterial {
  alignas(16) Vector4f base_color_factor;
  alignas(16) Vector3f emissive_factor;
  alignas(4)  float    metallic_factor;
  alignas(4)  float    roughness_factor;
  alignas(4)  uint32_t base_color_map_index;
};

struct DirectionalLight {
  Vector3f surfaces_to_light_direction;
  Vector3f color;

  void update_direction(const Vector3f& direction) {
    surfaces_to_light_direction.x = direction.x * -1.0f;
    surfaces_to_light_direction.y = direction.y * -1.0f;
    surfaces_to_light_direction.z = direction.z * -1.0f;
  };

  void update_color(const Vector3f& color) {
    this->color = color;
  }
};

struct GraphicsSkeleton {
  struct JointHierarchyNode {
    typedef uint32_t Index;

    Index parent_index;
    Index children_count;
    Transform local_transform;
  };

  std::vector<JointHierarchyNode> joint_hierarchy;
  std::vector<Matrix4x4f> inverse_bind_matrices;
  std::vector<std::string> names;

  size_t joint_count() { return joint_hierarchy.size(); }
};

struct AnimationState {
  uint32_t animation_id;
  float    time_seconds;
  float    speed_factor;
  bool     is_looping;

  void tick_time();
};

struct GraphicsSkeletonInstances {
  typedef uint32_t Index;
  static constexpr uint32_t max_buffer_data_joint_count = 256;  // this is also hardcoded in vertex shader

  struct BufferData {
    alignas(16) Matrix4x4f joint_matrices[max_buffer_data_joint_count]; // TODO: use a 3x4 matrix (remove redundant 0,0,0,1)
  };

  uint16_t skeleton_ids[max_buffer_data_joint_count]; // assumed not to be manually changed in simulation thread
  AnimationState animation_states[max_buffer_data_joint_count];

  Index allocate_skeleton_instances(const Index joint_count) {
    static Index current_skeleton_instance_index = 0;

    assert( (current_skeleton_instance_index + (joint_count - 1)) < max_buffer_data_joint_count );
    const Index result = current_skeleton_instance_index;
    current_skeleton_instance_index += joint_count;

    return result;
  }
};

struct GraphicsSkeletonInstanceArray {
  uint32_t first_skeleton_instance;
  uint32_t size;
};

struct Animation {
  struct Sampler {
    enum class Interpolation { Linear, Step, CubicSpline };
    typedef uint32_t Index;

    Interpolation interpolation;
    std::vector<float> inputs;
    std::vector<float> outputs;
  };

  struct Channel {
    enum class Path { Translation, Rotation, Scale };
    struct Target {
      Path path;
      GraphicsSkeleton::JointHierarchyNode::Index joint_index;
    };

    Target target;
    Sampler::Index sampler_index;
  };

  std::string name;
  std::vector<Channel> channels;
  std::vector<Sampler> samplers;
  float start_time_seconds;
  float end_time_seconds;
};

struct GraphicsSkin {
  struct MeshInstanceHierarchyNode {
    typedef uint16_t Index;

    Index     parent_index;
    Index     children_count;
    Transform local_transform;
    Transform global_transform;
    uint32_t  material_id;
  };

  GraphicsMeshInstanceArray mesh_instance_array;
  std::vector<MeshInstanceHierarchyNode> mesh_instance_hierarchy; // index is same as mesh_instance_array index
  GraphicsSkeletonInstances::Index skeleton_instance_id = -1;

  void update(const uint16_t index, const Transform& transform, uint32_t material_id);
  void update(const uint16_t index, uint32_t material_id);
  void update_all(uint32_t material_id);
  void play_animation(const uint32_t animation_id, const float speed_factor=1.0f, const bool is_looping=false);
  void stop_animating();
};

struct AnimationArray {
  uint32_t first_animation;
  uint32_t size;
};

struct GraphicsModel {
  std::vector<GraphicsMesh> meshes;
  std::vector<GraphicsSkin::MeshInstanceHierarchyNode> mesh_hierarchy;  // index is same as meshes array index
  std::vector<GraphicsSkeleton> skeletons;
  std::vector<Animation> animations;

  size_t calculate_indices_count() const {
    size_t indices_count = 0;
    for (uint32_t i=0; i < meshes.size(); ++i) {
      indices_count += meshes[i].indices.size();
    }
    return indices_count;
  }
  size_t calculate_vertices_count() const {
    size_t vertices_count = 0;
    for (uint32_t i=0; i < meshes.size(); ++i) {
      vertices_count += meshes[i].vertices.size();
    }
    return vertices_count;
  }
};

struct RenderThreadSimulationState {
  GraphicsMeshInstances mesh_instances;
  AnimationState        animation_states[GraphicsSkeletonInstances::max_buffer_data_joint_count];
};


namespace std {
  // a hash function for GraphicsVertex by specifying a template specialization for std::hash<T>
  template<> struct hash<GraphicsVertex> {
    size_t operator()(GraphicsVertex const& vertex) const {
      const string vertex_to_str( reinterpret_cast<const char*>(&vertex), sizeof(GraphicsVertex) );
      return hash<string>()(vertex_to_str);
    }
  };
}

bool init_graphics();
void deactivate_graphics();
void load_glb_graphics_model_from_file(const char* file_path, GraphicsModel& model);
void deduplicate_graphics_mesh_vertices(const GraphicsMesh& mesh, GraphicsMesh& deduplicated_mesh);
void upload_graphics_meshes_from_glb_file(const char* file_path, GraphicsModel* model_file_cache=nullptr);
void create_graphics_mesh_instance_array_from_glb(const char* file_path, GraphicsMeshInstanceArray& mesh_instance_array, GraphicsModel* model_file_cache=nullptr);
void update_graphics_mesh_instance_array(const GraphicsMeshInstanceArray& mesh_instance_array, const Transform& transform, const uint32_t material_id, const uint32_t joint_index, const uint32_t index);
void destroy_graphics_mesh_instance_array(GraphicsMeshInstanceArray& mesh_instance_array);
void upload_graphics_materials(GraphicsMaterial* const materials, const uint32_t count, const uint32_t start_index);
void update_ambient_light_intensity(const float intensity);
void load_graphics_skeletons_from_glb_file(const char* file_path, GraphicsModel* model_file_cache=nullptr);
void create_graphics_skeleton_instance_array_from_glb(const char* file_path, GraphicsSkeletonInstanceArray& skeleton_instance_array, GraphicsModel* model_file_cache=nullptr);
void destroy_graphics_skeleton_instance_array(GraphicsSkeletonInstanceArray& skeleton_instance_array);
void create_graphics_skin_from_glb(const char* file_path, GraphicsSkin& skin);
uint32_t create_graphics_material(const Vector4f& base_color_factor, const Vector3f& emissive_factor, const float metallic_factor, const float roughness_factor, const char* base_color_map_file_path);
uint32_t create_graphics_material(const float metallic_factor, const float roughness_factor, const char* base_color_map_file_path);
uint32_t create_graphics_material(const Vector4f& base_color_factor, const float metallic_factor, const float roughness_factor);
uint32_t get_max_material_count();
uint32_t upload_base_color_map_from_file(const char* file_path);
AnimationArray load_animations_from_glb_file(const char* file_path, GraphicsModel* model_file_cache=nullptr);

extern RenderThreadSimulationState graphics_render_thread_sim_state;
extern GraphicsMeshInstances graphics_all_mesh_instances;
extern GraphicsSkeletonInstances graphics_all_skeleton_instances;
extern DirectionalLight directional_light;

#endif  // INCLUDE_TOM_ENGINE_GRAPHICS_H


//////////////////////////////////////////////////


#ifdef TOM_ENGINE_GRAPHICS_IMPLEMENTATION
#ifndef TOM_ENGINE_GRAPHICS_IMPLEMENTATION_SINGLE
#define TOM_ENGINE_GRAPHICS_IMPLEMENTATION_SINGLE

#if defined(OCULUS_PC)
  #include "graphics_oculus_pc.cpp"
#elif defined(OCULUS_QUEST_2)
  #include "graphics_oculus_quest_2.cpp"
#endif

#endif  // TOM_ENGINE_GRAPHICS_IMPLEMENTATION_SINGLE
#endif  // TOM_ENGINE_GRAPHICS_IMPLEMENTATION
