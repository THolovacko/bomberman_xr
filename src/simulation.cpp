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

    Vector3f translation_offset_left  = {-0.008f,0.0225f,0.0225f};
    Vector3f translation_offset_right = {0.008f,0.0225f,0.0225f};
    translate_transform_local(left_hand_transform, translation_offset_left);
    translate_transform_local(right_hand_transform, translation_offset_right);

    const float scale = 0.01f;
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

    this->material_id = create_graphics_material(Vector4f{1.0f, 1.0f, 1.0f, 1.0f}, Vector3f{0.0f, 0.0f, 0.0f}, 0.0f, 1.0f, "assets/textures/bomberman.png");
    create_graphics_skin_from_glb("assets/models/bomberman.glb", this->skin);

    this->sound_id = create_audio_source("assets/sounds/white_bomberman_hurt.wav", 4.0f, 16.0f, 0.0f, 1.0f);
    this->skin.update_all(material_id);
    this->animations = load_animations_from_glb_file("assets/models/bomberman.glb");
  }

  void update() {
    Transform transform = {this->position,this->orientation,{0.00035f,0.00035f,0.00035f}};

    Vector3f x_axis = { 1.0f, 0.0f, 0.0f };
    rotate_transform_global(transform, 90.0f, x_axis);

    update_audio_source(sound_id, position);

    this->skin.update(0, transform, material_id);
  }
};

struct Bomb {
  Vector3f position;
  Quaternionf orientation;
  uint32_t material_id;
  uint32_t sound_id;
  GraphicsMeshInstanceArray mesh_instance_array;

  void init() {
    this->orientation = identity_orientation;
    this->position = {1.0f,0.0f,0.0f};

    this->material_id = create_graphics_material(Vector4f{1.0f, 1.0f, 1.0f, 1.0f}, Vector3f{0.0f, 0.0f, 0.0f}, 0.0f, 0.8f, "assets/textures/bomb.png");
    create_graphics_mesh_instance_array_from_glb("assets/models/bomb.glb", this->mesh_instance_array);
  }

  void update() {
    Transform transform = {this->position,this->orientation,{0.1f,0.1f,0.1f}};
    for (uint32_t i=0; i < this->mesh_instance_array.size; ++i) update_graphics_mesh_instance_array(this->mesh_instance_array, transform, material_id, uint32_t(-1), i);
  }
};

struct Fire {
  Vector3f position;
  Quaternionf orientation;
  uint32_t material_id;
  uint32_t sound_id;
  GraphicsSkin skin;
  AnimationArray animations;

  void init() {
    this->orientation = identity_orientation;
    this->position = {0.0f,1.0f,0.0f};

    this->material_id = create_graphics_material(Vector4f{1.0f, 1.0f, 1.0f, 1.0f}, Vector3f{0.0f, 0.0f, 0.0f}, 0.0f, 0.8f, "assets/textures/fire.png");
    create_graphics_skin_from_glb("assets/models/fire.glb", this->skin);

    this->skin.update_all(material_id);
    this->animations = load_animations_from_glb_file("assets/models/fire.glb");
  }

  void update() {
    Transform transform = {this->position,this->orientation,{0.1f,0.1f,0.1f}};
    Vector3f x_axis = { 1.0f, 0.0f, 0.0f };
    rotate_transform_global(transform, 90.0f, x_axis);
    this->skin.update(0, transform, material_id);
  }
};

struct BrickBlock {
  Vector3f position;
  Quaternionf orientation;
  uint32_t material_id;
  uint32_t material_id2;
  GraphicsMeshInstanceArray mesh_instance_array;

  void init() {
    this->orientation = identity_orientation;
    this->position = {0.0f,0.5f,0.0f};

    this->material_id  = create_graphics_material(Vector4f{1.0f, 1.0f, 1.0f, 1.0f}, Vector3f{0.0f, 0.0f, 0.0f}, 0.0f, 0.8f, "assets/textures/brick_texture.png");
    this->material_id2 = create_graphics_material(Vector4f{1.0f, 1.0f, 1.0f, 1.0f}, 0.0f, 0.8f);
    create_graphics_mesh_instance_array_from_glb("assets/models/brick_block.glb", this->mesh_instance_array);
  }

  void update() {
    Transform transform = {this->position,this->orientation,{0.0001125f,0.0001125f,0.045f}};
    update_graphics_mesh_instance_array(this->mesh_instance_array, transform, material_id, uint32_t(-1), 0);
    update_graphics_mesh_instance_array(this->mesh_instance_array, transform, material_id2, uint32_t(-1), 1);
  }
};

struct StoneBlock {
  Vector3f position;
  Quaternionf orientation;
  uint32_t material_id;
  GraphicsMeshInstanceArray mesh_instance_array;

  void init() {
    this->orientation = identity_orientation;
    this->position = {-1.0f,0.0f,0.0f};

    this->material_id  = create_graphics_material(Vector4f{1.0f, 1.0f, 1.0f, 1.0f}, Vector3f{0.0f, 0.0f, 0.0f}, 0.0f, 0.8f, "assets/textures/block_rock.jpeg");
    create_graphics_mesh_instance_array_from_glb("assets/models/block_rock.glb", this->mesh_instance_array);
  }

  void update() {
    Transform transform = {this->position,this->orientation,{0.06f,0.06f,0.06f}};
    update_graphics_mesh_instance_array(this->mesh_instance_array, transform, material_id, uint32_t(-1), 0);
    for (uint32_t i=0; i < this->mesh_instance_array.size; ++i) update_graphics_mesh_instance_array(this->mesh_instance_array, transform, material_id, uint32_t(-1), i);
  }
};

struct FloorWallBlock {
  Vector3f position;
  Quaternionf orientation;
  uint32_t material_id;
  GraphicsMeshInstanceArray mesh_instance_array;

  void update() {
    Transform transform = {this->position,this->orientation,{0.001f,0.001f,0.001f}};
    update_graphics_mesh_instance_array(this->mesh_instance_array, transform, material_id, uint32_t(-1), 0);
    for (uint32_t i=0; i < this->mesh_instance_array.size; ++i) update_graphics_mesh_instance_array(this->mesh_instance_array, transform, material_id, uint32_t(-1), i);
  }
};

struct Board {
  Vector3f first_block_position = {0.0f, 0.0f, 0.0f}; // position of top left floor block
  const float block_offset = 0.109f;
  static constexpr size_t floor_wall_block_count = 247; // 15 height and 13 width
  FloorWallBlock floor_wall_blocks[floor_wall_block_count];
  //Fire           all_fire[floor_wall_block_count];
  //StoneBlock     stones[floor_wall_block_count];
  //BrickBlock     bricks[floor_wall_block_count];

  uint32_t wall_material_id;
  uint32_t floor_1_material_id;
  uint32_t floor_2_material_id;
  uint32_t brick_material_id;
  uint32_t stone_material_id;

  void init() {
    this->wall_material_id = create_graphics_material(Vector4f{0.0f, 1.0f, 0.0f, 1.0f}, 0.0f, 0.8f);
    this->floor_1_material_id = create_graphics_material(Vector4f{1.0f, 0.0f, 0.0f, 1.0f}, 0.0f, 0.8f);
    //this->floor_2_material_id = create_graphics_material(Vector4f{0.0f, 1.0f, 0.0f, 1.0f}, 0.0f, 0.8f);
    //this->brick_material_id = create_graphics_material(Vector4f{0.0f, 1.0f, 0.0f, 1.0f}, 0.0f, 0.8f);
    //this->stone_material_id = create_graphics_material(Vector4f{0.0f, 1.0f, 0.0f, 1.0f}, 0.0f, 0.8f);

    for (size_t board_index=0; board_index < std::size(floor_wall_blocks); board_index += 2) {
      floor_wall_blocks[board_index].orientation = identity_orientation;
      floor_wall_blocks[board_index].position    = { first_block_position.x + (board_index * block_offset), first_block_position.y, first_block_position.z };
      floor_wall_blocks[board_index].material_id = wall_material_id;
      create_graphics_mesh_instance_array_from_glb("assets/models/block.glb", floor_wall_blocks[board_index].mesh_instance_array);
    }

    for (size_t board_index=1; board_index < std::size(floor_wall_blocks); board_index += 2) {
      floor_wall_blocks[board_index].orientation = identity_orientation;
      floor_wall_blocks[board_index].position    = { first_block_position.x + (board_index * block_offset), first_block_position.y, first_block_position.z };
      floor_wall_blocks[board_index].material_id = floor_1_material_id;
      create_graphics_mesh_instance_array_from_glb("assets/models/block.glb", floor_wall_blocks[board_index].mesh_instance_array);
    }
  }

  void update() {
    for (size_t board_index=0; board_index < std::size(floor_wall_blocks); ++board_index) {
      floor_wall_blocks[board_index].update();
    }
  }
};


HandControllers* hands = new HandControllers();
BomberMan* test_man    = new BomberMan();
Bomb* test_bomb        = new Bomb();
Fire* test_fire        = new Fire();
BrickBlock* test_brick = new BrickBlock();
StoneBlock* test_stone = new StoneBlock();
Board*      board      = new Board();

void head_pose_dependent_sim() {
  hands->update();
}

void SimulationState::init() {
  hands->init();

  directional_light.update_direction({0.0f, -1.0f, 0.0f});
  directional_light.update_color({1.0f, 1.0f, 1.0f});
  update_ambient_light_intensity(0.25f);

  test_man->init();
  test_man->skin.play_animation(test_man->animations.first_animation + 3, true);

  test_fire->init();
  test_fire->skin.play_animation(test_fire->animations.first_animation, true);

  test_bomb->init();
  test_brick->init();
  test_stone->init();
  board->init();
}

void SimulationState::update() {
  PROFILE_FUNCTION();

  hands->update();
  test_man->update();
  test_fire->update();
  test_bomb->update();
  test_brick->update();
  test_stone->update();
  board->update();

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
