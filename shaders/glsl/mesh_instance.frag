#version 450
#extension GL_ARB_separate_shader_objects  : enable
#extension GL_ARB_shading_language_420pack : enable
#extension GL_EXT_multiview : enable

#pragma fragment

layout(early_fragment_tests) in;

layout(location = 0) in vec3 normal;
layout(location = 1) in vec2 texture_coordinates;
layout(location = 2) in vec3 global_position;
layout(location = 3) flat in uint material_index;

layout(binding = 0) uniform sampler2DArray base_color_map_array;

layout(location = 0) out vec4 out_frag_color;

const uint  max_material_count = 8;
const float minimum_roughness  = 0.025;
const vec3  F0                 = vec3(0.04);
const float PI                 = 3.141592653589;

struct Material {
  vec4  base_color_factor;
  vec3  emissive_factor;
  float metallic_factor;
  float roughness_factor;
  uint  base_color_map_index;
};

layout (std140, binding = 1) uniform GlobalUniformBuffer {
  mat4  view_projection_matrices[2];
  vec3  directional_light;
  vec3  directional_light_color;
  vec3  left_eye_position;
  float ambient_light_factor;
} global_uniform_buffer;

layout (std140, binding = 2) uniform MaterialUniformBuffer {
  Material materials[max_material_count];
} material_uniform_buffer;


void main() {
  const Material material = material_uniform_buffer.materials[material_index];

  // base color textures are automatically converted to linear space due to using _SRGB suffix image format (Vulkan)
  vec4 base_color;
  if (material.base_color_map_index != -1) {
    base_color = texture(base_color_map_array, vec3(texture_coordinates, material.base_color_map_index)) * material.base_color_factor;
  } else {
    base_color = material.base_color_factor;
  }

  const float metalness         = clamp(material.metallic_factor, 0.0, 1.0);
  const float roughness         = pow( clamp(material.roughness_factor, minimum_roughness, 1.0), 2.0 );
  const float roughness_squared = roughness * roughness;
  const vec3  diffuse_color     = (base_color.rgb * (vec3(1.0) - F0)) * (1.0 - metalness);
  const vec3  specular_color    = mix(F0, base_color.rgb, metalness);
  const float reflectance       = max(max(specular_color.r, specular_color.g), specular_color.b);

  const vec3 surface_to_view_direction  = normalize(global_uniform_buffer.left_eye_position - global_position);
  const vec3 surface_to_light_direction = normalize(global_uniform_buffer.directional_light);
  const vec3 half_vector                = normalize(surface_to_light_direction + surface_to_view_direction);

  // n=normal l=surface_to_light_direction v=surface_to_view_direction h=half_vector
  const float n_dot_l = clamp(dot(normal, surface_to_light_direction), 0.001, 1.0);
  const float n_dot_v = clamp(abs(dot(normal, surface_to_view_direction)), 0.001, 1.0);
  const float n_dot_h = clamp(dot(normal, half_vector), 0.0, 1.0);
  const float v_dot_h = clamp(dot(surface_to_view_direction, half_vector), 0.0, 1.0);

  const vec3 fresnel_effect         = specular_color.rgb + (clamp(reflectance * 25.0, 0.0, 1.0) - specular_color.rgb) * pow(clamp(1.0 - v_dot_h, 0.0, 1.0), 5.0);
  const float geometric_shadowing   = ( 2.0 * n_dot_l / (n_dot_l + sqrt(roughness_squared + (1.0 - roughness_squared) * (n_dot_l * n_dot_l))) ) * ( 2.0 * n_dot_v / (n_dot_v + sqrt(roughness_squared + (1.0 - roughness_squared) * (n_dot_v * n_dot_v))) );
  const float specular_distribution = roughness_squared / ( PI * pow((n_dot_h * roughness_squared - n_dot_h) * n_dot_h + 1.0, 2.0) );

  vec3 color = n_dot_l * global_uniform_buffer.directional_light_color * ( ((1.0 - fresnel_effect) * ( diffuse_color / PI )) + (fresnel_effect * geometric_shadowing * specular_distribution / (4.0 * n_dot_l * n_dot_v)) );
  color     += base_color.xyz * global_uniform_buffer.ambient_light_factor;
  color     += material.emissive_factor;

  out_frag_color = vec4(color, base_color.a);
}
