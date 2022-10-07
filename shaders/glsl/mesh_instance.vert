#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable
#extension GL_EXT_multiview : enable

#pragma vertex

layout(location = 0)  in vec3 position;
layout(location = 1)  in vec3 normal;
layout(location = 2)  in vec2 texture_coordinates;
layout(location = 3)  in vec4 joint_indices;
layout(location = 4)  in vec4 joint_weights;
layout(location = 5)  in vec3 instance_model_matrix_column_0;
layout(location = 6)  in vec3 instance_model_matrix_column_1;
layout(location = 7)  in vec3 instance_model_matrix_column_2;
layout(location = 8)  in vec3 instance_model_matrix_column_3;
layout(location = 9)  in uint material_index;
layout(location = 10) in uint joint_index;

layout(location = 0) out vec3 out_normal;
layout(location = 1) out vec2 out_texture_coordinates;
layout(location = 2) out vec3 out_global_position;
layout(location = 3) out uint out_material_index;

#define MAX_JOINT_COUNT 256

/*
layout (std140, push_constant) uniform PushBuffer
{
} push_buffer;
*/

layout (std140, binding = 1) uniform GlobalUniformBuffer {
  mat4  view_projection_matrices[2];
  vec3  directional_light;
  vec3  directional_light_color;
  vec3  left_eye_position;
  float ambient_light_factor;
} global_uniform_buffer;

layout (std140, binding = 3) uniform SkeletonInstanceUniformBuffer {
  mat4 joint_matrices[MAX_JOINT_COUNT];
} skeleton_instance_uniform_buffer;

out gl_PerVertex
{
  vec4 gl_Position;
};

void main() {
  mat4 instance_model_matrix;
  instance_model_matrix[0] = vec4(instance_model_matrix_column_0, 0.0);
  instance_model_matrix[1] = vec4(instance_model_matrix_column_1, 0.0);
  instance_model_matrix[2] = vec4(instance_model_matrix_column_2, 0.0);
  instance_model_matrix[3] = vec4(instance_model_matrix_column_3, 1.0);

  vec4 local_position;
  if (joint_index != -1) {
    mat4 skin_matrix =
      ( joint_weights.x * skeleton_instance_uniform_buffer.joint_matrices[uint(joint_indices.x) + joint_index] ) +
      ( joint_weights.y * skeleton_instance_uniform_buffer.joint_matrices[uint(joint_indices.y) + joint_index] ) +
      ( joint_weights.z * skeleton_instance_uniform_buffer.joint_matrices[uint(joint_indices.z) + joint_index] ) +
      ( joint_weights.w * skeleton_instance_uniform_buffer.joint_matrices[uint(joint_indices.w) + joint_index] );

    local_position = skin_matrix * vec4(position, 1);
    out_normal     = normalize(transpose(inverse(mat3(instance_model_matrix * skin_matrix))) * normal);
  } else {
    local_position = vec4(position, 1);
    out_normal     = normalize(transpose(inverse(mat3(instance_model_matrix))) * normal);
  }

  out_texture_coordinates = texture_coordinates;
  out_material_index      = material_index;

  vec4 global_position = instance_model_matrix * local_position;
  out_global_position  = global_position.xyz;

  gl_Position = global_uniform_buffer.view_projection_matrices[gl_ViewIndex] * global_position; 
}
