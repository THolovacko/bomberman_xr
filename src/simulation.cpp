#include "tom_engine.h"


struct HandControllers {
  GraphicsMeshInstanceArray mesh_instance_array;
  uint32_t material_id;
  uint32_t sound_id;

  void init() {
    this->material_id = create_graphics_material(Vector4f{1.0f, 1.0f, 1.0f, 1.0f}, Vector3f{0.0f, 0.0f, 0.0f}, 0.0f, 0.53f, "assets/textures/quest_2_controllers.png");
    create_graphics_mesh_instance_array_from_glb("assets/models/quest_2_controllers.glb", this->mesh_instance_array);

    this->sound_id = create_audio_source("assets/sounds/faucet_water_run.wav", 4.0f, 16.0f, 0.0f, 1.0f);
    play_audio_source(sound_id, true);
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

    update_audio_source(sound_id, right_hand_transform.position);
  };
};

struct TestMan {
  Vector3f position;
  Quaternionf orientation;
  uint32_t material_id;
  uint32_t sound_id;
  GraphicsSkin skin;
  AnimationArray animations;

  void init() {
    this->orientation = identity_orientation;
    this->position = {0.0f,0.0f,-1.5f};

    //this->material_id = create_graphics_material(Vector4f{1.0f, 1.0f, 1.0f, 1.0f}, Vector3f{0.0f, 0.0f, 0.0f}, 0.0f, 0.8f, "assets/textures/fox.png");
    //create_graphics_skin_from_glb("assets/models/rigged_figure.glb", this->skin);
    //create_graphics_skin_from_glb("assets/models/fox.glb", this->skin);

    this->material_id = create_graphics_material(Vector4f{1.0f, 1.0f, 1.0f, 1.0f}, Vector3f{0.0f, 0.0f, 0.0f}, 0.0f, 0.8f, "assets/textures/cesium_man.jpg");
    create_graphics_skin_from_glb("assets/models/cesium_man.glb", this->skin);

    this->sound_id = create_audio_source("assets/sounds/male_laugh.wav", 4.0f, 16.0f, 0.0f, 1.0f);

    this->skin.update_all(material_id);

    //this->animations = graphics_load_animations_from_glb_file("assets/models/rigged_figure.glb");
    //this->animations = graphics_load_animations_from_glb_file("assets/models/fox.glb");
    this->animations = load_animations_from_glb_file("assets/models/cesium_man.glb");
  }

  void update() {
    Transform transform = identity_transform;
    //transform.scale = {0.015f,0.015f,0.015f};
    //Transform transform = {this->position,this->orientation,{0.015f,0.015f,0.015f}};
    Vector3f x_axis = {1.0f, 0.0f, 0.0f};
    Vector3f y_axis = {0.0f, 1.0f, 0.0f};
    rotate_transform_global(transform, -90.0f, x_axis);
    rotate_transform_global(transform, -90.0f, y_axis);
    Vector3f forward = global_space_forward_vector;
    translate_transform_global(transform, forward);

    update_audio_source(sound_id, position);

    this->skin.update(0, transform, material_id);
  }
};

struct CompanionCube {
  Vector3f position;
  Quaternionf orientation;
  uint32_t material_id;
  GraphicsSkin skin;

  void init() {
    this->orientation = identity_orientation;
    this->material_id = create_graphics_material(Vector4f{1.0f, 1.0f, 1.0f, 1.0f}, Vector3f{0.0f, 0.0f, 0.0f}, 0.0f, 0.8f, "assets/textures/companion_cube.jpeg");
    create_graphics_skin_from_glb("assets/models/companion_cube.glb", this->skin);
    this->skin.update_all(material_id);
  }

  void update() {
    static Transform transform = {this->position,this->orientation,{0.5f,0.5f,0.5f}};
    Vector3f right = { 1.0f * delta_time_seconds, 0.0f,  0.0f };
    translate_transform_global(transform, right);
    this->skin.update(0, transform, material_id);
  }
};


HandControllers* hands = new HandControllers();
TestMan* test_man = new TestMan();
CompanionCube* companion_cube = new CompanionCube();

void head_pose_dependent_sim() {
  hands->update();
}

void SimulationState::init() {
  hands->init();

  directional_light.update_direction({0.0f, -1.0f, 0.0f});
  directional_light.update_color({1.0f, 1.0f, 1.0f});
  update_ambient_light_intensity(0.08f);

  test_man->init();
  test_man->skin.play_animation(test_man->animations.first_animation, true);

  companion_cube->init();
}

void SimulationState::update() {
  PROFILE_FUNCTION();

  hands->update();
  test_man->update();
  companion_cube->update();

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
