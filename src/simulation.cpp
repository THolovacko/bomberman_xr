#include "tom_engine.h"
#include <cstdlib>
#include <time.h>

constexpr float global_scale = 0.25f;

struct HandControllers {
  GraphicsMeshInstanceArray mesh_instance_array;
  uint32_t material_id;
  Transform left_hand_transform  = input_state.left_hand_transform;
  Transform right_hand_transform = input_state.right_hand_transform;
  const float scale = 0.01f;

  void init() {
    this->material_id = create_graphics_material(Vector4f{1.0f, 1.0f, 1.0f, 1.0f}, Vector3f{0.0f, 0.0f, 0.0f}, 0.0f, 0.53f, "assets/textures/quest_2_controllers.png");
    create_graphics_mesh_instance_array_from_glb("assets/models/quest_2_controllers.glb", this->mesh_instance_array);
  }

  void update() {
    left_hand_transform  = input_state.left_hand_transform;
    right_hand_transform = input_state.right_hand_transform;

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

    left_hand_transform.scale  = {scale, scale, scale};
    right_hand_transform.scale = {scale, scale, scale};

    update_graphics_mesh_instance_array(this->mesh_instance_array, left_hand_transform, material_id, uint32_t(-1), 0);
    update_graphics_mesh_instance_array(this->mesh_instance_array, right_hand_transform, material_id, uint32_t(-1), 1);
  };
};

struct Bomb {
  Vector3f position;
  Quaternionf orientation;
  uint32_t material_id;
  uint32_t sound_id;
  GraphicsMeshInstanceArray mesh_instance_array;

  void update() {
    Transform transform = {this->position,this->orientation,{0.1f * global_scale, 0.1f * global_scale, 0.1f * global_scale}};
    for (uint32_t i=0; i < this->mesh_instance_array.size; ++i) update_graphics_mesh_instance_array(this->mesh_instance_array, transform, material_id, uint32_t(-1), i);
  }
};

struct Fire {
  Vector3f position;
  Quaternionf orientation;
  uint32_t material_id;
  uint32_t sound_id;
  GraphicsMeshInstanceArray mesh_instance_array;

  void update() {
    Transform transform = {this->position,this->orientation,{0.145f * global_scale, 0.145f * global_scale, 0.145f * global_scale}};
    for (uint32_t i=0; i < this->mesh_instance_array.size; ++i) update_graphics_mesh_instance_array(this->mesh_instance_array, transform, material_id, uint32_t(-1), i);
  }
};

struct BrickBlock {
  Vector3f position;
  Quaternionf orientation;
  uint32_t material_id_0;
  uint32_t material_id_1;
  GraphicsMeshInstanceArray mesh_instance_array;

  void update() {
    Transform transform = {this->position,this->orientation,{0.0001125f * global_scale, 0.0001125f * global_scale, 0.045f * global_scale}};
    update_graphics_mesh_instance_array(this->mesh_instance_array, transform, material_id_0, uint32_t(-1), 0);
    update_graphics_mesh_instance_array(this->mesh_instance_array, transform, material_id_1, uint32_t(-1), 1);
  }
};

struct StoneBlock {
  Vector3f position;
  Quaternionf orientation;
  uint32_t material_id;
  GraphicsMeshInstanceArray mesh_instance_array;

  void update() {
    Transform transform = {this->position,this->orientation,{0.05f * global_scale, 0.05f * global_scale, 0.05f * global_scale}};
    for (uint32_t i=0; i < this->mesh_instance_array.size; ++i) update_graphics_mesh_instance_array(this->mesh_instance_array, transform, material_id, uint32_t(-1), i);
  }
};

struct FloorWallBlock {
  Vector3f position;
  Quaternionf orientation;
  uint32_t material_id;
  GraphicsMeshInstanceArray mesh_instance_array;

  void update() {
    Transform transform = {this->position,this->orientation,{0.1f * global_scale, 0.1f * global_scale, 0.1f * global_scale}};
    for (uint32_t i=0; i < this->mesh_instance_array.size; ++i) update_graphics_mesh_instance_array(this->mesh_instance_array, transform, material_id, uint32_t(-1), i);
  }
};

struct Board {
  static constexpr float block_offset = 0.1f * global_scale;
  static constexpr size_t floor_wall_block_count = 247; // 13 height and 15 width
  static constexpr Vector3f hide_position = { 1000000.0f, 1000000.0f, 1000000.0f };
  enum class TileState : uint8_t { Stone, Brick, Bomb, Fire, Player_1, Player_2, Player_3, Player_4, Empty };
  const size_t floor_row_count    = 11;
  const size_t floor_column_count = 13;

  Vector3f first_block_position = {0.0f, 1.0f, 1.0f}; // position of top left floor block
  Vector3f first_floor_position; // position of top left floor block
  Vector3f player_1_start_position;
  Vector3f player_2_start_position;
  Vector3f player_3_start_position;
  Vector3f player_4_start_position;

  FloorWallBlock floor_wall_blocks[floor_wall_block_count];
  StoneBlock all_stones[30];
  BrickBlock all_bricks[143];
  Bomb all_bombs[143];
  Fire all_fire[143];
  TileState tile_states[143];

  uint32_t wall_material_id;
  uint32_t floor_1_material_id;
  uint32_t floor_2_material_id;
  uint32_t stone_material_id;
  uint32_t brick_material_id_0;
  uint32_t brick_material_id_1;
  uint32_t bomb_material_id;
  uint32_t fire_material_id;

  void reset_tile_states() {
    for (size_t tile_index=0; tile_index < std::size(tile_states); ++tile_index) {
      tile_states[tile_index] = TileState::Empty;
    }

    for (size_t i=0; i < std::size(all_bricks); ++i) { hide_brick(i); }

    for (size_t row_index=1; row_index < floor_row_count; row_index+=2) {
      for (size_t column_index=1; column_index < floor_column_count; column_index+=2) {
        tile_states[(row_index * 13) + column_index] = TileState::Stone;
      }
    }

    tile_states[0]   = TileState::Player_1;
    tile_states[12]  = TileState::Player_2;
    tile_states[130] = TileState::Player_3;
    tile_states[142] = TileState::Player_4;

    srand(static_cast<unsigned int>(time(NULL)));
    for (size_t tile_index=0; tile_index < std::size(tile_states); ++tile_index) {
      bool is_tile_in_player_spawn_space = (tile_index == 0)   || (tile_index == 1)   || (tile_index == 13)
                                        || (tile_index == 11)  || (tile_index == 12)  || (tile_index == 25)
                                        || (tile_index == 130) || (tile_index == 117) || (tile_index == 131)
                                        || (tile_index == 129) || (tile_index == 141) || (tile_index == 142);
      bool is_tile_stone = tile_states[tile_index] == TileState::Stone;
      if ( is_tile_stone || is_tile_in_player_spawn_space ) continue;

      int random_number = (rand() % 10 + 1);
      if (random_number <= 7) {
        tile_states[tile_index] = TileState::Brick;
        show_brick(tile_index);
      }
    }
  }

  void show_brick(const size_t tile_index) {
    const size_t row_index    = tile_index / 13;
    const size_t column_index = tile_index % 13;
    all_bricks[tile_index].position = { first_floor_position.x + ((float)column_index * block_offset), first_floor_position.y + block_offset, first_floor_position.z + ((float)row_index * block_offset) };
    all_bricks[tile_index].update();
  }

  void hide_brick(const size_t tile_index) {
    all_bricks[tile_index].position = Board::hide_position;
    all_bricks[tile_index].update();
  }

  void show_bomb(const size_t tile_index) {
    const size_t row_index    = tile_index / 13;
    const size_t column_index = tile_index % 13;
    all_bombs[tile_index].position = { first_floor_position.x + ((float)column_index * block_offset), first_floor_position.y + (block_offset / 2.0f), first_floor_position.z + ((float)row_index * block_offset) };
    all_bombs[tile_index].update();
  }

  void hide_bomb(const size_t tile_index) {
    all_bombs[tile_index].position = Board::hide_position;
    all_bombs[tile_index].update();
  }

  void show_fire(const size_t tile_index) {
    const size_t row_index    = tile_index / 13;
    const size_t column_index = tile_index % 13;
    all_fire[tile_index].position = { first_floor_position.x + ((float)column_index * block_offset), first_floor_position.y + (block_offset / 2.0f) - 0.005f, first_floor_position.z + ((float)row_index * block_offset) };
    all_fire[tile_index].update();
  }

  void hide_fire(const size_t tile_index) {
    all_fire[tile_index].position = Board::hide_position;
    all_fire[tile_index].update();
  }

  Vector3f calculate_player_1_start_position() { return {first_floor_position.x - (block_offset / 2.5f), first_block_position.y + (block_offset / 2.0f), first_floor_position.z}; }
  Vector3f calculate_player_2_start_position() { return {first_floor_position.x + (block_offset * 12.0f) - (block_offset / 2.5f), first_block_position.y + (block_offset / 2.0f), first_floor_position.z}; }
  Vector3f calculate_player_3_start_position() { return {first_floor_position.x - (block_offset / 2.5f), first_block_position.y + (block_offset / 2.0f), first_floor_position.z + (block_offset * 10.0f)}; }
  Vector3f calculate_player_4_start_position() { return {first_floor_position.x + (block_offset * 12.0f) - (block_offset / 2.5f), first_block_position.y + (block_offset / 2.0f), first_floor_position.z + (block_offset * 10.0f)}; }

  void init() {
    this->wall_material_id    = create_graphics_material(Vector4f{0.27843f, 0.27451f, 0.2549f, 1.0f}, 0.6f, 0.7f);
    this->floor_1_material_id = create_graphics_material(Vector4f{0.22353f, 0.43922f, 0.1451f, 1.0f}, 0.0f, 1.0f);
    this->floor_2_material_id = create_graphics_material(Vector4f{0.27843f, 0.52941f, 0.18039f, 1.0f}, 0.0f, 1.0f);
    this->stone_material_id   = create_graphics_material(Vector4f{1.0f, 1.0f, 1.0f, 1.0f}, Vector3f{0.0f, 0.0f, 0.0f}, 0.3f, 0.75f, "assets/textures/block_rock.jpeg");
    this->brick_material_id_0 = create_graphics_material(Vector4f{1.0f, 1.0f, 1.0f, 1.0f}, Vector3f{0.0f, 0.0f, 0.0f}, 0.0f, 0.85f, "assets/textures/brick_texture_0.png");
    this->brick_material_id_1 = create_graphics_material(Vector4f{1.0f, 1.0f, 1.0f, 1.0f}, Vector3f{0.0f, 0.0f, 0.0f}, 0.0f, 0.85f, "assets/textures/brick_texture_1.png");
    this->bomb_material_id    = create_graphics_material(Vector4f{1.0f, 1.0f, 1.0f, 1.0f}, Vector3f{0.0f, 0.0f, 0.0f}, 0.2f, 0.9f, "assets/textures/bomb.png");
    this->fire_material_id    = create_graphics_material(Vector4f{1.0f, 1.0f, 1.0f, 1.0f}, Vector3f{0.0f, 0.0f, 0.0f}, 0.0f, 0.6f, "assets/textures/fire.jpeg");

    size_t floor_wall_blocks_index = 0;

    /* create walls */
    const Vector3f top_right_block_position = { first_block_position.x + (14.0f * block_offset), first_block_position.y, first_block_position.z };
    const Vector3f bottom_left_block_position = { first_block_position.x, first_block_position.y, first_block_position.z + (12.0f * block_offset) };
    for (size_t i=0; i < 15; ++i) {
      floor_wall_blocks[floor_wall_blocks_index].orientation = identity_orientation;
      floor_wall_blocks[floor_wall_blocks_index].position    = { first_block_position.x + ((float)i * block_offset), first_block_position.y, first_block_position.z };
      floor_wall_blocks[floor_wall_blocks_index].material_id = wall_material_id;
      create_graphics_mesh_instance_array_from_glb("assets/models/cube.glb", floor_wall_blocks[floor_wall_blocks_index].mesh_instance_array);
      floor_wall_blocks[floor_wall_blocks_index].update();
      ++floor_wall_blocks_index;

      floor_wall_blocks[floor_wall_blocks_index].orientation = identity_orientation;
      floor_wall_blocks[floor_wall_blocks_index].position    = { first_block_position.x + ((float)i * block_offset), first_block_position.y + block_offset, first_block_position.z };
      floor_wall_blocks[floor_wall_blocks_index].material_id = wall_material_id;
      create_graphics_mesh_instance_array_from_glb("assets/models/cube.glb", floor_wall_blocks[floor_wall_blocks_index].mesh_instance_array);
      floor_wall_blocks[floor_wall_blocks_index].update();
      ++floor_wall_blocks_index;
    }
    for (size_t i=1; i < 13; ++i) {
      floor_wall_blocks[floor_wall_blocks_index].orientation = identity_orientation;
      floor_wall_blocks[floor_wall_blocks_index].position    = { first_block_position.x, first_block_position.y, first_block_position.z + (i * block_offset) };
      floor_wall_blocks[floor_wall_blocks_index].material_id = wall_material_id;
      create_graphics_mesh_instance_array_from_glb("assets/models/cube.glb", floor_wall_blocks[floor_wall_blocks_index].mesh_instance_array);
      floor_wall_blocks[floor_wall_blocks_index].update();
      ++floor_wall_blocks_index;

      floor_wall_blocks[floor_wall_blocks_index].orientation = identity_orientation;
      floor_wall_blocks[floor_wall_blocks_index].position    = { first_block_position.x, first_block_position.y + block_offset, first_block_position.z + ((float)i * block_offset) };
      floor_wall_blocks[floor_wall_blocks_index].material_id = wall_material_id;
      create_graphics_mesh_instance_array_from_glb("assets/models/cube.glb", floor_wall_blocks[floor_wall_blocks_index].mesh_instance_array);
      floor_wall_blocks[floor_wall_blocks_index].update();
      ++floor_wall_blocks_index;
    }
    for (size_t i=1; i < 13; ++i) {
      floor_wall_blocks[floor_wall_blocks_index].orientation = identity_orientation;
      floor_wall_blocks[floor_wall_blocks_index].position    = { top_right_block_position.x, top_right_block_position.y, top_right_block_position.z + ((float)i * block_offset) };
      floor_wall_blocks[floor_wall_blocks_index].material_id = wall_material_id;
      create_graphics_mesh_instance_array_from_glb("assets/models/cube.glb", floor_wall_blocks[floor_wall_blocks_index].mesh_instance_array);
      floor_wall_blocks[floor_wall_blocks_index].update();
      ++floor_wall_blocks_index;

      floor_wall_blocks[floor_wall_blocks_index].orientation = identity_orientation;
      floor_wall_blocks[floor_wall_blocks_index].position    = { top_right_block_position.x, top_right_block_position.y + block_offset, top_right_block_position.z + ((float)i * block_offset) };
      floor_wall_blocks[floor_wall_blocks_index].material_id = wall_material_id;
      create_graphics_mesh_instance_array_from_glb("assets/models/cube.glb", floor_wall_blocks[floor_wall_blocks_index].mesh_instance_array);
      floor_wall_blocks[floor_wall_blocks_index].update();
      ++floor_wall_blocks_index;
    }
    for (size_t i=1; i < 14; ++i) {
      floor_wall_blocks[floor_wall_blocks_index].orientation = identity_orientation;
      floor_wall_blocks[floor_wall_blocks_index].position    = { bottom_left_block_position.x + ((float)i * block_offset), bottom_left_block_position.y, bottom_left_block_position.z };
      floor_wall_blocks[floor_wall_blocks_index].material_id = wall_material_id;
      create_graphics_mesh_instance_array_from_glb("assets/models/cube.glb", floor_wall_blocks[floor_wall_blocks_index].mesh_instance_array);
      floor_wall_blocks[floor_wall_blocks_index].update();
      ++floor_wall_blocks_index;

      floor_wall_blocks[floor_wall_blocks_index].orientation = identity_orientation;
      floor_wall_blocks[floor_wall_blocks_index].position    = { bottom_left_block_position.x + ((float)i * block_offset), bottom_left_block_position.y + block_offset, bottom_left_block_position.z };
      floor_wall_blocks[floor_wall_blocks_index].material_id = wall_material_id;
      create_graphics_mesh_instance_array_from_glb("assets/models/cube.glb", floor_wall_blocks[floor_wall_blocks_index].mesh_instance_array);
      floor_wall_blocks[floor_wall_blocks_index].update();
      ++floor_wall_blocks_index;
    }

    /* create floor */
    first_floor_position = { first_block_position.x + block_offset, first_block_position.y, first_block_position.z + block_offset };
    for (size_t row_index=0; row_index < floor_row_count; ++row_index) {
      for (size_t column_index=0; column_index < floor_column_count; ++column_index) {
        floor_wall_blocks[floor_wall_blocks_index].orientation = identity_orientation;
        floor_wall_blocks[floor_wall_blocks_index].position    = { first_floor_position.x + ((float)column_index * block_offset), first_floor_position.y, first_floor_position.z + ((float)row_index * block_offset) };
        floor_wall_blocks[floor_wall_blocks_index].material_id = ( !(row_index % 2) == !(column_index % 2) ) ? floor_1_material_id : floor_2_material_id;
        create_graphics_mesh_instance_array_from_glb("assets/models/cube.glb", floor_wall_blocks[floor_wall_blocks_index].mesh_instance_array);
        floor_wall_blocks[floor_wall_blocks_index].update();
        ++floor_wall_blocks_index;
      }
    }

    /* create stones */
    size_t stone_index = 0;
    for (size_t row_index=1; row_index < floor_row_count; row_index+=2) {
      for (size_t column_index=1; column_index < floor_column_count; column_index+=2) {
        all_stones[stone_index].orientation = identity_orientation;
        all_stones[stone_index].position    = { first_floor_position.x + ((float)column_index * block_offset), first_floor_position.y + block_offset, first_floor_position.z + ((float)row_index * block_offset) };
        all_stones[stone_index].material_id = stone_material_id;
        create_graphics_mesh_instance_array_from_glb("assets/models/block_rock.glb", all_stones[stone_index].mesh_instance_array);
        all_stones[stone_index].update();
        ++stone_index;
      }
    }

    /* create bricks and fire */
    size_t tile_index = 0;
    for (size_t row_index=0; row_index < floor_row_count; ++row_index) {
      for (size_t column_index=0; column_index < floor_column_count; ++column_index) {
        all_bricks[tile_index].orientation   = identity_orientation;
        all_bricks[tile_index].material_id_0 = brick_material_id_0;
        all_bricks[tile_index].material_id_1 = brick_material_id_1;
        create_graphics_mesh_instance_array_from_glb("assets/models/brick_block.glb", all_bricks[tile_index].mesh_instance_array);
        this->hide_brick(tile_index);

        all_bombs[tile_index].orientation = identity_orientation;
        all_bombs[tile_index].material_id = bomb_material_id;
        create_graphics_mesh_instance_array_from_glb("assets/models/bomb.glb", all_bombs[tile_index].mesh_instance_array);
        this->hide_bomb(tile_index);

        all_fire[tile_index].orientation = identity_orientation;
        all_fire[tile_index].material_id = fire_material_id;
        create_graphics_mesh_instance_array_from_glb("assets/models/fire.glb", all_fire[tile_index].mesh_instance_array);
        this->hide_fire(tile_index);

        ++tile_index;
      }
    }

    player_1_start_position = calculate_player_1_start_position();
    player_2_start_position = calculate_player_2_start_position();
    player_3_start_position = calculate_player_3_start_position();
    player_4_start_position = calculate_player_4_start_position();

    reset_tile_states();
  }

  void move(const Vector3f& position) {
    this->first_block_position = position;

    size_t floor_wall_blocks_index = 0;

    const Vector3f top_right_block_position = { first_block_position.x + (14.0f * block_offset), first_block_position.y, first_block_position.z };
    const Vector3f bottom_left_block_position = { first_block_position.x, first_block_position.y, first_block_position.z + (12.0f * block_offset) };
    for (size_t i=0; i < 15; ++i) {
      floor_wall_blocks[floor_wall_blocks_index].position = { first_block_position.x + ((float)i * block_offset), first_block_position.y, first_block_position.z };
      ++floor_wall_blocks_index;
      floor_wall_blocks[floor_wall_blocks_index].position = { first_block_position.x + ((float)i * block_offset), first_block_position.y + block_offset, first_block_position.z };
      ++floor_wall_blocks_index;
    }
    for (size_t i=1; i < 13; ++i) {
      floor_wall_blocks[floor_wall_blocks_index].position = { first_block_position.x, first_block_position.y, first_block_position.z + (i * block_offset) };
      ++floor_wall_blocks_index;

      floor_wall_blocks[floor_wall_blocks_index].position = { first_block_position.x, first_block_position.y + block_offset, first_block_position.z + ((float)i * block_offset) };
      ++floor_wall_blocks_index;
    }
    for (size_t i=1; i < 13; ++i) {
      floor_wall_blocks[floor_wall_blocks_index].orientation = identity_orientation;
      floor_wall_blocks[floor_wall_blocks_index].position = { top_right_block_position.x, top_right_block_position.y, top_right_block_position.z + ((float)i * block_offset) };
      ++floor_wall_blocks_index;

      floor_wall_blocks[floor_wall_blocks_index].position = { top_right_block_position.x, top_right_block_position.y + block_offset, top_right_block_position.z + ((float)i * block_offset) };
      ++floor_wall_blocks_index;
    }
    for (size_t i=1; i < 14; ++i) {
      floor_wall_blocks[floor_wall_blocks_index].position = { bottom_left_block_position.x + ((float)i * block_offset), bottom_left_block_position.y, bottom_left_block_position.z };
      ++floor_wall_blocks_index;

      floor_wall_blocks[floor_wall_blocks_index].position = { bottom_left_block_position.x + ((float)i * block_offset), bottom_left_block_position.y + block_offset, bottom_left_block_position.z };
      ++floor_wall_blocks_index;
    }

    const size_t floor_row_count    = 11;
    const size_t floor_column_count = 13;
    first_floor_position = { first_block_position.x + block_offset, first_block_position.y, first_block_position.z + block_offset };
    for (size_t row_index=0; row_index < floor_row_count; ++row_index) {
      for (size_t column_index=0; column_index < floor_column_count; ++column_index) {
        floor_wall_blocks[floor_wall_blocks_index].position    = { first_floor_position.x + ((float)column_index * block_offset), first_floor_position.y, first_floor_position.z + ((float)row_index * block_offset) };
        ++floor_wall_blocks_index;
      }
    }

    size_t stone_index = 0;
    for (size_t row_index=1; row_index < floor_row_count; row_index+=2) {
      for (size_t column_index=1; column_index < floor_column_count; column_index+=2) {
        all_stones[stone_index].position = { first_floor_position.x + ((float)column_index * block_offset), first_floor_position.y + block_offset, first_floor_position.z + ((float)row_index * block_offset) };
        ++stone_index;
      }
    }

    size_t tile_index = 0;
    for (size_t row_index=0; row_index < floor_row_count; ++row_index) {
      for (size_t column_index=0; column_index < floor_column_count; ++column_index) {
        switch (tile_states[tile_index]) {
          case TileState::Brick: {
            all_bricks[tile_index].position = { first_floor_position.x + ((float)column_index * block_offset), first_floor_position.y + block_offset, first_floor_position.z + ((float)row_index * block_offset) };
            all_bricks[tile_index].update();
            break;
          }
          case TileState::Bomb: {
            all_bombs[tile_index].position = { first_floor_position.x + ((float)column_index * block_offset), first_floor_position.y + (block_offset / 2.0f), first_floor_position.z + ((float)row_index * block_offset) };
            all_bombs[tile_index].update();
            break;
          }
          case TileState::Fire: {
            all_fire[tile_index].position = { first_floor_position.x + ((float)column_index * block_offset), first_floor_position.y + (block_offset / 2.0f) - 0.005f, first_floor_position.z + ((float)row_index * block_offset) };
            all_fire[tile_index].update();
            break;
          }
        }

        ++tile_index;
      }
    }

    player_1_start_position = calculate_player_1_start_position();
    player_2_start_position = calculate_player_2_start_position();
    player_3_start_position = calculate_player_3_start_position();
    player_4_start_position = calculate_player_4_start_position();

    for (size_t i=0; i < std::size(floor_wall_blocks); ++i) {
      floor_wall_blocks[i].update();
    }
    for (size_t i=0; i < std::size(all_stones); ++i) {
      all_stones[i].update();
    }
  }

  void update() {
    for (size_t i=0; i < std::size(all_bombs); ++i) {
      all_bombs[i].update();
    }

    for (size_t i=0; i < std::size(all_fire); ++i) {
      all_fire[i].update();
    }
  }
};

struct Bomberman {
  enum class GlobalDirection : uint8_t { Down, Up, Right, Left };

  Transform transform;
  uint32_t material_id;
  uint32_t sound_id;
  GraphicsSkin skin;
  AnimationArray animations;
  uint32_t player_id;
  GlobalDirection current_direction;
  GlobalDirection previous_direction;

  void init(const uint32_t player_id) {
    this->player_id             = player_id;
    this->transform.orientation = identity_orientation;
    this->transform.position    = {0.0f,0.0f,0.0f};
    this->transform.scale       = {0.00035f * global_scale, 0.00035f * global_scale, 0.00035f * global_scale};
    this->current_direction     = ( (this->player_id == 1) || (this->player_id == 2) ) ? GlobalDirection::Down : GlobalDirection::Up;

    switch (this->player_id) {
      case 1: {
        this->material_id = create_graphics_material(Vector4f{1.0f, 1.0f, 1.0f, 1.0f}, Vector3f{0.0f, 0.0f, 0.0f}, 0.0f, 1.0f, "assets/textures/bomberman.png");
        break;
      }
      case 2: {
        this->material_id = create_graphics_material(Vector4f{1.0f, 0.0f, 0.0f, 1.0f}, Vector3f{0.0f, 0.0f, 0.0f}, 0.0f, 1.0f, "assets/textures/bomberman.png");
        break;
      }
      case 3: {
        this->material_id = create_graphics_material(Vector4f{0.0f, 1.0f, 0.0f, 1.0f}, Vector3f{0.0f, 0.0f, 0.0f}, 0.0f, 1.0f, "assets/textures/bomberman.png");
        break;
      }
      case 4: {
        this->material_id = create_graphics_material(Vector4f{0.0f, 0.0f, 1.0f, 1.0f}, Vector3f{0.0f, 0.0f, 0.0f}, 0.0f, 1.0f, "assets/textures/bomberman.png");
        break;
      }
    }
    create_graphics_skin_from_glb("assets/models/bomberman.glb", this->skin);

    this->sound_id = create_audio_source("assets/sounds/white_bomberman_hurt.wav", 4.0f, 16.0f, 0.0f, 1.0f);
    this->skin.update_all(material_id);
    this->animations = load_animations_from_glb_file("assets/models/bomberman.glb");

    Vector3f x_axis = { 1.0f, 0.0f, 0.0f };
    rotate_transform_global(this->transform, 90.0f, x_axis);
  }

  void update() {
    Transform target_transform = {this->transform.position,this->transform.orientation, this->transform.scale};

    if (this->current_direction != GlobalDirection::Down) {
      Vector3f y_axis = { 0.0f, 1.0f, 0.0f };
      switch (this->current_direction) {
        case GlobalDirection::Up: {
          rotate_transform_global(target_transform, 180.0f, y_axis);
          target_transform.position.x += (Board::block_offset / 1.25f);
          break;
        }
        case GlobalDirection::Left: {
          rotate_transform_global(target_transform, 90.0f, y_axis);
          target_transform.position.x += (Board::block_offset / 2.5f);
          target_transform.position.z += (Board::block_offset / 2.5f);
          break;
        }
        case GlobalDirection::Right: {
          rotate_transform_global(target_transform, -90.0f, y_axis);
          target_transform.position.x += (Board::block_offset / 2.5f);
          target_transform.position.z -= (Board::block_offset / 2.5f);
          break;
        }
      }
    }

    update_audio_source(sound_id, this->transform.position);

    this->skin.update(0, target_transform, material_id);
  }
};

HandControllers* hands = new HandControllers();
Board* board           = new Board();
Bomberman* player_1    = new Bomberman();
Bomberman* player_2    = new Bomberman();
Bomberman* player_3    = new Bomberman();
Bomberman* player_4    = new Bomberman();

void head_pose_dependent_sim() {
  hands->update();
}

void SimulationState::init() {
  hands->init();

  directional_light.update_direction({0.0f, -1.0f, 0.0f});
  directional_light.update_color({1.0f, 1.0f, 1.0f});
  update_ambient_light_intensity(0.25f);

  player_1->init(1);
  player_1->skin.play_animation(player_1->animations.first_animation + 1, true);

  player_2->init(2);
  player_2->skin.play_animation(player_2->animations.first_animation + 1, true);

  player_3->init(3);
  player_3->skin.play_animation(player_3->animations.first_animation + 1, true);

  player_4->init(4);
  player_4->skin.play_animation(player_4->animations.first_animation + 1, true);

  board->init();
  player_1->transform.position = board->player_1_start_position;
  player_2->transform.position = board->player_2_start_position;
  player_3->transform.position = board->player_3_start_position;
  player_4->transform.position = board->player_4_start_position;
}

void SimulationState::update() {
  PROFILE_FUNCTION();

  hands->update();
  player_1->update();
  player_2->update();
  player_3->update();
  player_4->update();
  board->update();

  if (input_state.left_hand_select) {
    DEBUG_LOG("left hand select\n");

    //play_audio_source(player_1->sound_id);

    board->move(input_state.left_hand_transform.position);
    player_1->transform.position = board->player_1_start_position;
    player_2->transform.position = board->player_2_start_position;
    player_3->transform.position = board->player_3_start_position;
    player_4->transform.position = board->player_4_start_position;

    //player_1->current_direction = Bomberman::GlobalDirection::Right;
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
