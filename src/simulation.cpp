#include "tom_engine.h"


struct HandControllers {
  GraphicsMeshInstanceArray mesh_instance_array;
  uint32_t material_id;

  void init() {
    this->material_id = create_graphics_material(Vector4f{1.0f, 1.0f, 1.0f, 1.0f}, Vector3f{0.0f, 0.0f, 0.0f}, 0.0f, 0.53f, "assets/textures/quest_2_controllers.png");
    create_graphics_mesh_instance_array_from_glb("assets/models/quest_2_controllers.glb", this->mesh_instance_array);
  }

  void update() {
    Transform left_hand_transform  = input_state.left_hand_transform;
    Transform right_hand_transform = input_state.right_hand_transform;

    Vector3f y_axis = { 0.0f,1.0f,0.0f };
    Vector3f x_axis = { 1.0f,0.0f,0.0f };

    left_hand_transform.orientation  = identity_orientation;
    right_hand_transform.orientation = identity_orientation;
    rotate_transform_global(right_hand_transform, 180.0f, y_axis);
    rotate_transform_global(right_hand_transform, -60.0f, x_axis);
    rotate_transform_global(right_hand_transform, input_state.right_hand_transform.orientation);

    rotate_transform_global(left_hand_transform, 180.0f, y_axis);
    rotate_transform_global(left_hand_transform, -60.0f, x_axis);
    rotate_transform_global(left_hand_transform, input_state.left_hand_transform.orientation);

    const float scale = 0.011f;
    left_hand_transform.scale  = {scale, scale, scale};
    right_hand_transform.scale = {scale, scale, scale};

    update_graphics_mesh_instance_array(this->mesh_instance_array, left_hand_transform, material_id, uint32_t(-1), 0);
    update_graphics_mesh_instance_array(this->mesh_instance_array, right_hand_transform, material_id, uint32_t(-1), 1);
  };
};

struct BomberMan {
  Vector3f position;
  Quaternionf orientation;
  uint32_t material_id;
  uint32_t sound_id;
  GraphicsSkin skin;
  AnimationArray animations;

  void init() {
    this->orientation = identity_orientation;
    this->position = {0.0f,0.0f,0.0f};

    this->material_id = create_graphics_material(Vector4f{1.0f, 1.0f, 1.0f, 1.0f}, Vector3f{0.0f, 0.0f, 0.0f}, 0.0f, 0.8f, "assets/textures/bomberman.png");
    create_graphics_skin_from_glb("assets/models/bomberman.glb", this->skin);

    this->sound_id = create_audio_source("assets/sounds/white_bomberman_hurt.wav", 4.0f, 16.0f, 0.0f, 1.0f);
    this->skin.update_all(material_id);
    this->animations = load_animations_from_glb_file("assets/models/bomberman.glb");
  }

  void update() {
    //Transform transform = identity_transform;
    //transform.scale     = {0.001f,0.001f,0.001f};
    Transform transform = {this->position,this->orientation,{0.001f,0.001f,0.001f}};

    Vector3f axis = { 1.0f, 0.0f, 0.0f };
    rotate_transform_global(transform, 90.0f, axis);

    update_audio_source(sound_id, position);

    this->skin.update(0, transform, material_id);
  }
};


HandControllers* hands = new HandControllers();
BomberMan* test_man = new BomberMan();

void head_pose_dependent_sim() {
  hands->update();
}

void SimulationState::init() {
  hands->init();

  directional_light.update_direction({0.0f, -1.0f, 0.0f});
  directional_light.update_color({1.0f, 1.0f, 1.0f});
  update_ambient_light_intensity(0.08f);

  test_man->init();
  test_man->skin.play_animation(test_man->animations.first_animation + 3, true);
}

void SimulationState::update() {
  PROFILE_FUNCTION();

  hands->update();
  test_man->update();

  if (input_state.left_hand_select) {
    DEBUG_LOG("left hand select\n");

    play_audio_source(test_man->sound_id);
  }

  if (input_state.right_hand_select) {
    DEBUG_LOG("right hand select\n");
    platform_request_exit();
  }

  if (input_state.gamepad_select) {
    DEBUG_LOG("gamepad select\n");
  }

}

void SimulationState::exit() {
}
