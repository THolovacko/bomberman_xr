#ifndef INCLUDE_TOM_ENGINE_MATHS_H
#define INCLUDE_TOM_ENGINE_MATHS_H

#include <xr_linear.h>

typedef XrVector2f    Vector2f;
typedef XrVector3f    Vector3f;
typedef XrVector4f    Vector4f;
typedef XrMatrix4x4f  Matrix4x4f;
typedef XrQuaternionf Quaternionf;
typedef XrFovf        FieldOfViewf;

constexpr Vector3f global_space_forward_vector  = {  0.0f,  0.0f, -1.0f };
constexpr Vector3f global_space_backward_vector = {  0.0f,  0.0f,  1.0f };
constexpr Vector3f global_space_right_vector    = {  1.0f,  0.0f,  0.0f };
constexpr Vector3f global_space_left_vector     = { -1.0f,  0.0f,  0.0f };
constexpr Vector3f global_space_up_vector       = {  0.0f,  1.0f,  0.0f };
constexpr Vector3f global_space_down_vector     = {  0.0f, -1.0f,  0.0f };

constexpr Quaternionf identity_rotation    = { 0.0f, 0.0f, 0.0f, 1.0f };
constexpr Quaternionf identity_orientation = { 0.0f, 0.0f, 0.0f, 1.0f };

constexpr Vector3f default_scale = { 1.0f, 1.0f, 1.0f };

constexpr float PI = 3.14159265359f;

struct Transform {
  union {
    Vector3f position;
    Vector3f translation;
  };
  union {
    Quaternionf orientation;
    Quaternionf rotation;
  };
  Vector3f scale;
};

constexpr Transform identity_transform = { {0.0f,0.0f,0.0f}, identity_rotation, default_scale };

struct Frustum {
  typedef Vector4f Plane;

  Plane planes[6];
};

void create_transform_matrix(const Transform& transform, Matrix4x4f& transform_matrix);
void create_view_matrix(const Vector3f camera_position, const Quaternionf camera_orientation, Matrix4x4f& view_matrix);
void create_projection_matrix(const FieldOfViewf& fov, const float near_clipping_plane, const float far_clipping_plane, Matrix4x4f& projection_matrix);
void matrix_4x4f_multiply(const Matrix4x4f& matrix_a, const Matrix4x4f& matrix_b, Matrix4x4f& product);
void create_frustum_from_view_projection_matrix(const Matrix4x4f& projection_view_matrix, Frustum* const frustum);
bool is_sphere_volume_visible(const Vector3f& position, const float radius, const Frustum& frustum);
void rotate_transform_global(Transform& transform, const float angle_degrees, Vector3f& axis);
void rotate_transform_global(Transform& transform, Quaternionf& rotation);
void rotate_transform_local(Transform& transform, const float angle_degrees, Vector3f& local_axis);
void inverse_quaternion(Quaternionf& quat);
void translate_position_global(Vector3f& position, Vector3f& translation);
void translate_transform_global(Transform& transform, Vector3f& translation);
void translate_transform_local(Transform& transform, Vector3f& local_translation);
void scale_transform(Transform& transform, Vector3f& scale);
void scale_transform(Transform& transform, float scale_factor);
float lerp(const float start, const float end, const float interpolate_amount);
void create_matrix(float* array_of_16_floats, Matrix4x4f& matrix);
void create_transform(Matrix4x4f& matrix, Transform& transform);
void combine_transform(const Transform& transformation, Transform& current);
void invert_transform(Matrix4x4f& transform);
Vector3f lerp(Vector3f& start, Vector3f& end, const float interpolate_amount);
Quaternionf slerp(Quaternionf& start, Quaternionf& end, const float interpolate_amount);
Quaternionf normalize(Quaternionf quaternion);
Vector3f calculate_forward_vector(Quaternionf orientation);
Vector3f calculate_up_vector(Quaternionf orientation);
Vector3f calculate_right_vector(Quaternionf orientation);
Vector3f add_vectors(const Vector3f& a, const Vector3f& b);

#endif  // INCLUDE_TOM_ENGINE_MATHS_H

//////////////////////////////////////////////////

#ifdef  TOM_ENGINE_MATHS_IMPLEMENTATION
#ifndef TOM_ENGINE_MATHS_IMPLEMENTATION_SINGLE
#define TOM_ENGINE_MATHS_IMPLEMENTATION_SINGLE

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_INLINE
#define GLM_FORCE_INTRINSICS
#define GLM_ENABLE_EXPERIMENTAL

#include <math.h> 

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-volatile"  // compound assignment to object of volatile-qualified type 'volatile float' is deprecated
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/hash.hpp>
#include <glm/gtc/type_ptr.hpp>
#pragma clang diagnostic pop

void create_projection_matrix(const FieldOfViewf& fov, const float near_clipping_plane, const float far_clipping_plane, Matrix4x4f& projection_matrix) {
  #if defined(OCULUS_PC) || defined(OCULUS_QUEST_2)
  XrMatrix4x4f_CreateProjectionFov(&projection_matrix, GRAPHICS_VULKAN, fov, near_clipping_plane, far_clipping_plane);
  #endif
}

void create_transform_matrix(const Transform& transform, Matrix4x4f& transform_matrix) {
  XrMatrix4x4f_CreateTranslationRotationScale(&transform_matrix, &transform.position, &transform.orientation, &transform.scale);
}

void create_view_matrix(const Vector3f camera_position, const Quaternionf camera_orientation, Matrix4x4f& view_matrix) {
  Matrix4x4f camera_translation_rotation_scale_matrix;
  Transform  camera_transform;

  camera_transform.position    = camera_position;
  camera_transform.orientation = camera_orientation;
  camera_transform.scale       = default_scale;

  create_transform_matrix(camera_transform, camera_translation_rotation_scale_matrix);

  XrMatrix4x4f_InvertRigidBody(&view_matrix, &camera_translation_rotation_scale_matrix);
}

void invert_transform(Matrix4x4f& transform) {
  Matrix4x4f copy = transform;
  XrMatrix4x4f_InvertRigidBody(&transform, &copy);
}

void matrix_4x4f_multiply(const Matrix4x4f& matrix_a, const Matrix4x4f& matrix_b, Matrix4x4f& product) {
  XrMatrix4x4f_Multiply(&product, &matrix_a, &matrix_b);
}

void create_frustum_from_view_projection_matrix(const Matrix4x4f& view_projection_matrix, Frustum* const frustum) {
  glm::mat4 matrix = glm::make_mat4(view_projection_matrix.m);

  // left plane
  frustum->planes[0].x = matrix[0].w + matrix[0].x;
  frustum->planes[0].y = matrix[1].w + matrix[1].x;
  frustum->planes[0].z = matrix[2].w + matrix[2].x;
  frustum->planes[0].w = matrix[3].w + matrix[3].x;

  // right plane
  frustum->planes[1].x = matrix[0].w - matrix[0].x;
  frustum->planes[1].y = matrix[1].w - matrix[1].x;
  frustum->planes[1].z = matrix[2].w - matrix[2].x;
  frustum->planes[1].w = matrix[3].w - matrix[3].x;

  // top plane
  frustum->planes[2].x = matrix[0].w - matrix[0].y;
  frustum->planes[2].y = matrix[1].w - matrix[1].y;
  frustum->planes[2].z = matrix[2].w - matrix[2].y;
  frustum->planes[2].w = matrix[3].w - matrix[3].y;

  // bottom plane
  frustum->planes[3].x = matrix[0].w + matrix[0].y;
  frustum->planes[3].y = matrix[1].w + matrix[1].y;
  frustum->planes[3].z = matrix[2].w + matrix[2].y;
  frustum->planes[3].w = matrix[3].w + matrix[3].y;

  // back plane
  frustum->planes[4].x = matrix[0].w + matrix[0].z;
  frustum->planes[4].y = matrix[1].w + matrix[1].z;
  frustum->planes[4].z = matrix[2].w + matrix[2].z;
  frustum->planes[4].w = matrix[3].w + matrix[3].z;

  // front plane
  frustum->planes[5].x = matrix[0].w - matrix[0].z;
  frustum->planes[5].y = matrix[1].w - matrix[1].z;
  frustum->planes[5].z = matrix[2].w - matrix[2].z;
  frustum->planes[5].w = matrix[3].w - matrix[3].z;

  for (uint32_t i=0; i < 6; ++i) {  // 6 planes in a frustum
    glm::vec4 plane(frustum->planes[i].x, frustum->planes[i].y, frustum->planes[i].z, frustum->planes[i].w);
    const float length = sqrtf(plane.x * plane.x + plane.y * plane.y + plane.z * plane.z);
    plane /= length;
    frustum->planes[i] = { plane.x, plane.y, plane.z, plane.w };
  }
}

bool is_sphere_volume_visible(const Vector3f& position, const float radius, const Frustum& frustum) {
  for (uint32_t i=0; i < 6; ++i) {  // 6 planes in a frustum
    const Frustum::Plane& plane = frustum.planes[i];
    if ( (plane.x * position.x) + (plane.y * position.y) + (plane.z * position.z) + plane.w <= -radius ) return false;
  }

  return true;
}

void translate_position_global(Vector3f& position, Vector3f& translation) {
  glm::vec3* glm_position    = reinterpret_cast<glm::vec3*>(&position);
  glm::vec3* glm_translation = reinterpret_cast<glm::vec3*>(&translation);

  *glm_position = (*glm_position) + (*glm_translation);
}

void translate_transform_global(Transform& transform, Vector3f& translation) {
  translate_position_global(transform.position, translation);
}

void translate_transform_local(Transform& transform, Vector3f& local_translation) {
  glm::vec3* glm_local_translation = reinterpret_cast<glm::vec3*>(&local_translation);
  glm::quat* glm_orientation       = reinterpret_cast<glm::quat*>(&transform.orientation);

  glm::vec3 glm_global_translation = (*glm_orientation) * (*glm_local_translation);
  Vector3f* global_translation     = reinterpret_cast<Vector3f*>(&glm_global_translation);

  translate_transform_global(transform, *global_translation);
}

void rotate_transform_global(Transform& transform, Quaternionf& rotation) {
  glm::quat* glm_rotation    = reinterpret_cast<glm::quat*>(&rotation);
  glm::quat* glm_orientation = reinterpret_cast<glm::quat*>(&transform.orientation);

  *glm_orientation = (*glm_rotation) * (*glm_orientation);
}

void rotate_transform_global(Transform& transform, const float angle_degrees, Vector3f& axis) {
  glm::vec3* glm_axis    = reinterpret_cast<glm::vec3*>(&axis);

  glm::quat glm_rotation = glm::angleAxis(glm::radians(angle_degrees), *glm_axis);
  Quaternionf* rotation  = reinterpret_cast<Quaternionf*>(&glm_rotation);

  return rotate_transform_global(transform, *rotation);
}

void rotate_transform_local(Transform& transform, const float angle_degrees, Vector3f& local_axis) {
  glm::vec3* glm_local_axis  = reinterpret_cast<glm::vec3*>(&local_axis);
  glm::quat* glm_orientation = reinterpret_cast<glm::quat*>(&transform.orientation);

  glm::vec3 glm_global_axis = (*glm_orientation) * (*glm_local_axis);
  Vector3f* global_axis = reinterpret_cast<Vector3f*>(&glm_global_axis);

  rotate_transform_global(transform, angle_degrees, *global_axis);
}

void scale_transform(Transform& transform, Vector3f& scale) {
  assert( (scale.x > 0.0f) && (scale.y > 0.0f) && (scale.z > 0.0f) );
  glm::vec3* glm_transform_scale = reinterpret_cast<glm::vec3*>(&transform.scale);
  glm::vec3* glm_scale           = reinterpret_cast<glm::vec3*>(&scale);

  *glm_transform_scale = (*glm_transform_scale) * (*glm_scale);
}

void scale_transform(Transform& transform, const float scale_factor) {
  Vector3f scale_vector = {scale_factor,scale_factor,scale_factor};

  scale_transform(transform, scale_vector);
}

void inverse_quaternion(Quaternionf& quat) {
  glm::quat* glm_quat = reinterpret_cast<glm::quat*>(&quat);

  *glm_quat = glm::inverse(*glm_quat);
}

float lerp(const float start, const float end, const float interpolate_amount) {
  return glm::mix(start, end, interpolate_amount);
}

Vector3f lerp(Vector3f& start, Vector3f& end, const float interpolate_amount) {
  glm::vec3* glm_start = reinterpret_cast<glm::vec3*>(&start);
  glm::vec3* glm_end   = reinterpret_cast<glm::vec3*>(&end);

  glm::vec3 glm_result = glm::mix(*glm_start, *glm_end, interpolate_amount);
  Vector3f* result     = reinterpret_cast<Vector3f*>(&glm_result);

  return *result;
}

void create_matrix(float* array_of_16_floats, Matrix4x4f& matrix) {
  glm::mat4 glm_matrix = glm::make_mat4(array_of_16_floats);
  matrix = *(reinterpret_cast<Matrix4x4f*>(&glm_matrix));
}

void create_transform(Matrix4x4f& matrix, Transform& transform) {
  glm::mat4 glm_matrix = *(reinterpret_cast<glm::mat4*>(&matrix));

  glm::vec3 position = glm_matrix[3];

  glm::vec3 scale;
  scale[0] = glm::length(glm::vec3(glm_matrix[0]));
  scale[1] = glm::length(glm::vec3(glm_matrix[1]));
  scale[2] = glm::length(glm::vec3(glm_matrix[2]));

  glm::mat3 rotation_matrix( glm::vec3(glm_matrix[0]) / scale[0], glm::vec3(glm_matrix[1]) / scale[1], glm::vec3(glm_matrix[2]) / scale[2] );
  glm::quat orientation = glm::quat_cast(rotation_matrix);

  transform.position    = *(reinterpret_cast<Vector3f*>(&position));
  transform.orientation = *(reinterpret_cast<Quaternionf*>(&orientation));
  transform.scale       = *(reinterpret_cast<Vector3f*>(&scale));
}

void combine_transform(const Transform& transformation, Transform& current) { // TODO: shouldn't need to convert to matrices and back
  Matrix4x4f matrix_a;
  Matrix4x4f matrix_b;
  Matrix4x4f result_matrix;
  create_transform_matrix(transformation, matrix_a);
  create_transform_matrix(current, matrix_b);

  matrix_4x4f_multiply(matrix_a, matrix_b, result_matrix);

  create_transform(result_matrix, current);
}

Quaternionf slerp(Quaternionf& start, Quaternionf& end, const float interpolate_amount) {
  glm::quat* glm_start = reinterpret_cast<glm::quat*>(&start);
  glm::quat* glm_end   = reinterpret_cast<glm::quat*>(&end);

  glm::quat glm_result = glm::slerp(*glm_start, *glm_end, interpolate_amount);
  Quaternionf* result  = reinterpret_cast<Quaternionf*>(&glm_result);

  return *result;
}

Quaternionf normalize(Quaternionf quaternion) {
  glm::quat* glm_quaternion = reinterpret_cast<glm::quat*>(&quaternion);

  glm::quat glm_result = glm::normalize(*glm_quaternion);
  Quaternionf* result  = reinterpret_cast<Quaternionf*>(&glm_result);

  return *result;
}

Vector3f calculate_forward_vector(Quaternionf orientation) {
  Vector3f forward_vector = global_space_forward_vector;
  glm::quat* glm_orienation = reinterpret_cast<glm::quat*>(&orientation);
  glm::vec3* glm_forward_vector = reinterpret_cast<glm::vec3*>(&forward_vector);

  glm::vec3 glm_result = (*glm_orienation) * (*glm_forward_vector);
  Vector3f* result     = reinterpret_cast<Vector3f*>(&glm_result);

  return *result;
}

Vector3f calculate_up_vector(Quaternionf orientation) {
  Vector3f up_vector = global_space_up_vector;
  glm::quat* glm_orienation = reinterpret_cast<glm::quat*>(&orientation);
  glm::vec3* glm_up_vector = reinterpret_cast<glm::vec3*>(&up_vector);

  glm::vec3 glm_result = (*glm_orienation) * (*glm_up_vector);
  Vector3f* result     = reinterpret_cast<Vector3f*>(&glm_result);

  return *result;
}

Vector3f calculate_right_vector(Quaternionf orientation) {
  Vector3f right_vector = global_space_right_vector;
  glm::quat* glm_orienation = reinterpret_cast<glm::quat*>(&orientation);
  glm::vec3* glm_right_vector = reinterpret_cast<glm::vec3*>(&right_vector);

  glm::vec3 glm_result = (*glm_orienation) * (*glm_right_vector);
  Vector3f* result     = reinterpret_cast<Vector3f*>(&glm_result);

  return *result;
}

Vector3f add_vectors(const Vector3f& a, const Vector3f& b) {
  return { a.x + b.x, a.y + b.y, a.z + b.z };
}

#endif  //TOM_ENGINE_MATHS_IMPLEMENTATION_SINGLE
#endif  // TOM_ENGINE_MATHS_IMPLEMENTATION
