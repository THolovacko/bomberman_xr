#include "tom_engine.h"
#include <cstdlib>
#include <time.h>
#include <bitset>

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
  const float min_scale_factor = 0.1f;
  const float max_scale_factor = 0.15f;
  const float speed            = 0.11f;
  float current_scale_factor   = min_scale_factor;
  float scale_sign             = 1.0f;

  void update() {
    const float scale_magnitude = fmod(speed * delta_time_seconds, max_scale_factor);
    scale_sign = (  1.0f * static_cast<float>((scale_sign == 1.0f)  && (this->current_scale_factor + scale_magnitude) <= max_scale_factor) ) +
                 (  1.0f * static_cast<float>((scale_sign == -1.0f) && (this->current_scale_factor - scale_magnitude) < min_scale_factor)  ) +
                 ( -1.0f * static_cast<float>((scale_sign == 1.0f)  && (this->current_scale_factor + scale_magnitude) > max_scale_factor)  ) +
                 ( -1.0f * static_cast<float>((scale_sign == -1.0f) && (this->current_scale_factor - scale_magnitude) >= min_scale_factor) );
    this->current_scale_factor += scale_magnitude * scale_sign;
    this->current_scale_factor = ( min_scale_factor * static_cast<float>(this->current_scale_factor < min_scale_factor) ) + ( this->current_scale_factor * static_cast<float>(this->current_scale_factor >= min_scale_factor) );

    Transform transform = {this->position,this->orientation,{current_scale_factor * global_scale, current_scale_factor * global_scale, current_scale_factor * global_scale}};
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
  enum class TileState : uint8_t { Stone, Brick, Bomb, Fire, Empty };
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
    tile_states[tile_index] = TileState::Bomb;
    all_bombs[tile_index].position = { first_floor_position.x + ((float)column_index * block_offset), first_floor_position.y + (block_offset / 2.0f), first_floor_position.z + ((float)row_index * block_offset) };
    all_bombs[tile_index].update();
  }

  void hide_bomb(const size_t tile_index) {
    tile_states[tile_index] = TileState::Empty;
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
    this->floor_1_material_id = create_graphics_material(Vector4f{0.22353f, 0.43922f, 0.1451f, 1.0f}, 0.0f, 0.9f);
    this->floor_2_material_id = create_graphics_material(Vector4f{0.27843f, 0.52941f, 0.18039f, 1.0f}, 0.0f, 0.9f);
    this->stone_material_id   = create_graphics_material(Vector4f{1.0f, 1.0f, 1.0f, 1.0f}, Vector3f{0.0f, 0.0f, 0.0f}, 0.0f, 0.8f, "assets/textures/block_rock.jpeg");
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

  Vector3f move(const Vector3f& position) {
    const Vector3f amount_moved = { position.x - this->first_block_position.x, position.y - this->first_block_position.y, position.z - this->first_block_position.z };
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
        floor_wall_blocks[floor_wall_blocks_index].position = { first_floor_position.x + ((float)column_index * block_offset), first_floor_position.y, first_floor_position.z + ((float)row_index * block_offset) };
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

    return amount_moved;
  }
};

struct Bomberman {
  enum class GlobalDirection : uint8_t { Down, Up, Right, Left };
  static constexpr uint32_t death_animation_offset   = 0;
  static constexpr uint32_t idle_animation_offset    = 1;
  static constexpr uint32_t running_animation_offset = 2;
  static constexpr uint32_t dancing_animation_offset = 3;

  Transform transform;
  uint32_t material_id;
  uint32_t sound_id;
  GraphicsSkin skin;
  AnimationArray animations;
  uint32_t player_id;
  GlobalDirection current_direction;

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

    this->skin.play_animation(this->animations.first_animation + idle_animation_offset, true);
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
        case GlobalDirection::Right: {
          rotate_transform_global(target_transform, 90.0f, y_axis);
          target_transform.position.x += (Board::block_offset / 2.5f);
          target_transform.position.z += (Board::block_offset / 2.5f);
          break;
        }
        case GlobalDirection::Left: {
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

struct MovementSystem {
  struct PlayerMovementState {
    Vector3f initial_position;
    Vector3f target_position;
    uint32_t current_tile_index;
    uint32_t target_tile_index;
    Bomberman* player;
  };

  Board* board_state;
  std::bitset<8> is_player_moving_bits; // first half player is moving; second half player has chained move
  PlayerMovementState player_movement_states[4];
  Bomberman::GlobalDirection chain_move_directions[4];

  void reset(Board* const board_state, Bomberman* const player_1, Bomberman* const player_2, Bomberman* const player_3, Bomberman* const player_4) {
    this->board_state = board_state;

    player_movement_states[0].player = player_1;
    player_movement_states[1].player = player_2;
    player_movement_states[2].player = player_3;
    player_movement_states[3].player = player_4;

    player_movement_states[0].player->skin.play_animation(player_movement_states[0].player->animations.first_animation + Bomberman::idle_animation_offset, true);
    player_movement_states[1].player->skin.play_animation(player_movement_states[1].player->animations.first_animation + Bomberman::idle_animation_offset, true);
    player_movement_states[2].player->skin.play_animation(player_movement_states[2].player->animations.first_animation + Bomberman::idle_animation_offset, true);
    player_movement_states[3].player->skin.play_animation(player_movement_states[3].player->animations.first_animation + Bomberman::idle_animation_offset, true);

    player_movement_states[0].current_tile_index = 0;
    player_movement_states[0].target_tile_index  = 0;
    player_movement_states[1].current_tile_index = 12;
    player_movement_states[1].target_tile_index  = 12;
    player_movement_states[2].current_tile_index = 130;
    player_movement_states[2].target_tile_index  = 130;
    player_movement_states[3].current_tile_index = 142;
    player_movement_states[3].target_tile_index  = 142;
  }

  bool move_player(const uint32_t player_id, const Bomberman::GlobalDirection direction) {
    const uint32_t player_index = player_id - 1;
    PlayerMovementState& movement_state = player_movement_states[player_index];

    if (is_player_moving_bits[player_index]) {
      const Vector3f total_distance = { movement_state.target_position.x - movement_state.initial_position.x,
                                        0.0f,
                                        movement_state.target_position.z - movement_state.initial_position.z
                                      };
      const Vector3f current_distance = { movement_state.player->transform.position.x - movement_state.initial_position.x,
                                          0.0f,
                                          movement_state.player->transform.position.z - movement_state.initial_position.z
                                        };
      const float chain_threshold = 0.8f;
      is_player_moving_bits[player_index + 4] = ( (current_distance.x != 0.0f) && ((current_distance.x / total_distance.x) > chain_threshold) || 
                                                  (current_distance.z != 0.0f) && ((current_distance.z / total_distance.z) > chain_threshold));
      chain_move_directions[player_index] = direction;
      return false;
    }

    const size_t row_index    = movement_state.current_tile_index / 13;
    const size_t column_index = movement_state.current_tile_index % 13;
    uint32_t target_tile_index;
    Vector3f position_offset = {0.0f, 0.0f, 0.0f};
    if (direction == Bomberman::GlobalDirection::Up) {
      if (row_index == 0) return false;
      target_tile_index = movement_state.current_tile_index - 13;
      position_offset.z -= board_state->Board::block_offset;
    } else if (direction == Bomberman::GlobalDirection::Right) {
      if (column_index == 12 ) return false;
      target_tile_index = movement_state.current_tile_index + 1;
      position_offset.x += board_state->Board::block_offset;
    } else if (direction == Bomberman::GlobalDirection::Left) {
      if (column_index == 0 ) return false;
      target_tile_index = movement_state.current_tile_index - 1;
      position_offset.x -= board_state->Board::block_offset;
    } else if (direction == Bomberman::GlobalDirection::Down) {
      if (row_index == 10) return false;
      target_tile_index = movement_state.current_tile_index + 13;
      position_offset.z += board_state->Board::block_offset;
    }

    if (board_state->tile_states[target_tile_index] != Board::TileState::Empty) return false;
    for (uint32_t i=0; i < 4; ++i) {
      if (i == player_index) continue;
      if (player_movement_states[i].current_tile_index == target_tile_index) return false;
      if (player_movement_states[i].target_tile_index == target_tile_index) return false;
    }

    movement_state.target_tile_index         = target_tile_index;
    movement_state.initial_position          = movement_state.player->transform.position;
    movement_state.target_position           = { movement_state.initial_position.x + position_offset.x, movement_state.initial_position.y + position_offset.y, movement_state.initial_position.z + position_offset.z };
    movement_state.player->current_direction = direction;

    is_player_moving_bits[player_index] = true;
    return true;
  }

  void update() {
    for (uint32_t i=0; i < 4; ++i) {
      if (is_player_moving_bits[i]) {
        const Vector3f direction = { player_movement_states[i].target_position.x - player_movement_states[i].initial_position.x, 0.0f, player_movement_states[i].target_position.z - player_movement_states[i].initial_position.z };
        const bool is_moving_x_axis = direction.x != 0.0f;
        const float direction_sign = (static_cast<float>(direction.x > 0.0f || direction.z > 0.0f) * 1.0f) + (static_cast<float>(direction.x < 0.0f || direction.z < 0.0f) * -1.0f);
        constexpr float tiles_per_second = 4.0f;
        const Vector3f velocity = { (board_state->Board::block_offset * tiles_per_second) * delta_time_seconds * direction_sign * static_cast<float>(is_moving_x_axis), 0.0f, (board_state->Board::block_offset * tiles_per_second) * delta_time_seconds * direction_sign * static_cast<float>(!is_moving_x_axis) };
        Vector3f& current_position = player_movement_states[i].player->transform.position;
        current_position.x += velocity.x;
        current_position.z += velocity.z;

        PlayerMovementState& movement_state = player_movement_states[i];
        bool reached_destination = ( (velocity.x > 0.0f) && (current_position.x >= movement_state.target_position.x) ) ||
                                   ( (velocity.x < 0.0f) && (current_position.x <= movement_state.target_position.x) ) ||
                                   ( (velocity.z > 0.0f) && (current_position.z >= movement_state.target_position.z) ) ||
                                   ( (velocity.z < 0.0f) && (current_position.z <= movement_state.target_position.z) );

        if (reached_destination) {
          const Vector3f difference = { current_position.x - movement_state.target_position.x, 0.0f, current_position.z - movement_state.target_position.z };

          is_player_moving_bits[i] = false;
          player_movement_states[i].player->transform.position = movement_state.target_position;
          movement_state.current_tile_index = movement_state.target_tile_index;

          if (is_player_moving_bits[i + 4]) { // check if chained move
            if ( this->move_player(movement_state.player->player_id, chain_move_directions[i]) ) {
              const Vector3f chain_direction = { player_movement_states[i].target_position.x - player_movement_states[i].initial_position.x, 0.0f, player_movement_states[i].target_position.z - player_movement_states[i].initial_position.z };
              const bool chain_is_moving_x_axis = chain_direction.x != 0.0f;
              const float chain_direction_sign = (static_cast<float>(chain_direction.x > 0.0f || chain_direction.z > 0.0f) * 1.0f) + (static_cast<float>(chain_direction.x < 0.0f || chain_direction.z < 0.0f) * -1.0f);
              const float difference_magnitude = abs(difference.x + difference.z);
              current_position.x += (difference_magnitude * static_cast<float>(chain_is_moving_x_axis) * chain_direction_sign);
              current_position.z += (difference_magnitude * static_cast<float>(!chain_is_moving_x_axis) * chain_direction_sign);

              bool chain_reached_destination = ( (chain_direction.x > 0.0f) && (current_position.x >= movement_state.target_position.x) ) ||
                                               ( (chain_direction.x < 0.0f) && (current_position.x <= movement_state.target_position.x) ) ||
                                               ( (chain_direction.z > 0.0f) && (current_position.z >= movement_state.target_position.z) ) ||
                                               ( (chain_direction.z < 0.0f) && (current_position.z <= movement_state.target_position.z) );

              if (chain_reached_destination) {
                is_player_moving_bits[i] = false;
                player_movement_states[i].player->transform.position = movement_state.target_position;
                movement_state.current_tile_index = movement_state.target_tile_index;
                player_movement_states[i].player->skin.play_animation(player_movement_states[i].player->animations.first_animation + Bomberman::idle_animation_offset, true);
              }

              is_player_moving_bits[i + 4] = false;
            } else {  // player has move chained but move unsuccessful
              player_movement_states[i].player->skin.play_animation(player_movement_states[i].player->animations.first_animation + Bomberman::idle_animation_offset, true);
            }
          } else {  // player has no move chained
            player_movement_states[i].player->skin.play_animation(player_movement_states[i].player->animations.first_animation + Bomberman::idle_animation_offset, true);
          }
        }
      }
    }
  }
};

struct BombSystem {
  Board* board_state;
  MovementSystem* movement_state;
  std::bitset<143> is_bomb_active_bits;
  std::bitset<143> is_fire_active_bits;
  float timers[143];
  const float detonation_time_seconds = 3.0f;
  const float explosion_time_seconds  = 1.0f;
  const uint32_t blast_radius         = 2;

  void init(Board* const board_state, MovementSystem* const movement_state) {
    this->board_state    = board_state;
    this->movement_state = movement_state;
  }

  void place_bomb(const uint32_t player_id) {
    const uint32_t player_index = player_id - 1;
    const uint32_t tile_index = movement_state->player_movement_states[player_index].current_tile_index;
    if (board_state->tile_states[tile_index] == Board::TileState::Bomb) return;

    board_state->show_bomb(tile_index);
    is_bomb_active_bits[tile_index] = true;
    timers[tile_index] = 0.0f;
  }

  void update() {
    for (uint32_t tile_index=0; tile_index < is_bomb_active_bits.size(); ++tile_index) {
      if (is_bomb_active_bits[tile_index]) {
        board_state->all_bombs[tile_index].update();
        timers[tile_index] += delta_time_seconds;
        if (timers[tile_index] >= detonation_time_seconds) {
          board_state->hide_bomb(tile_index);
          is_bomb_active_bits[tile_index] = false;
          timers[tile_index] = timers[tile_index] - detonation_time_seconds;  // timer is being used for fire now
          bool render_fire = timers[tile_index] > explosion_time_seconds;

          const int32_t column_index = tile_index % 13;
          const int32_t row_index    = tile_index / 13;

          uint32_t blast_up_count = 0;
          for (int32_t i=1; i <= blast_radius; ++i) {
            const int32_t target_tile_index = int32_t(tile_index) + (i * int32_t(-13));
            if ( (row_index - i) < 0 ) break;
            if (board_state->tile_states[target_tile_index] == Board::TileState::Stone) break;

            ++blast_up_count;
            if ( (board_state->tile_states[target_tile_index] == Board::TileState::Brick) || (board_state->tile_states[target_tile_index] == Board::TileState::Bomb) ) break;
            // TODO: also break if hit another player
          }
          uint32_t blast_down_count = 0;
          for (int32_t i=1; i <= blast_radius; ++i) {
            const int32_t target_tile_index = int32_t(tile_index) + (i * int32_t(13));
            if ( (row_index + i) > 11 ) break;
            if (board_state->tile_states[target_tile_index] == Board::TileState::Stone) break;

            ++blast_down_count;
            if ( (board_state->tile_states[target_tile_index] == Board::TileState::Brick) || (board_state->tile_states[target_tile_index] == Board::TileState::Bomb) ) break;
          }
          uint32_t blast_right_count = 0;
          for (int32_t i=1; i <= blast_radius; ++i) {
            const int32_t target_tile_index = int32_t(tile_index) + i;
            if ( (column_index + i) > 12 ) break;
            if (board_state->tile_states[target_tile_index] == Board::TileState::Stone) break;

            ++blast_right_count;
            if ( (board_state->tile_states[target_tile_index] == Board::TileState::Brick) || (board_state->tile_states[target_tile_index] == Board::TileState::Bomb) ) break;
          }
          uint32_t blast_left_count = 0;
          for (int32_t i=1; i <= blast_radius; ++i) {
            const int32_t target_tile_index = int32_t(tile_index) - i;
            if ( (column_index - i) < 0 ) break;
            if (board_state->tile_states[target_tile_index] == Board::TileState::Stone) break;

            ++blast_left_count;
            if ( (board_state->tile_states[target_tile_index] == Board::TileState::Brick) || (board_state->tile_states[target_tile_index] == Board::TileState::Bomb) ) break;
          }

          auto explode_tile = [&](const uint32_t p_tile_index) {
            switch (board_state->tile_states[p_tile_index]) {
              case Board::TileState::Brick: {
                board_state->hide_brick(p_tile_index);
                board_state->tile_states[p_tile_index] = Board::TileState::Empty;
                break;
              }
              case Board::TileState::Bomb: {
                break;
              }
            }

            if (p_tile_index == movement_state->player_movement_states[0].current_tile_index) {
            } else if (p_tile_index == movement_state->player_movement_states[1].current_tile_index) {
            } else if (p_tile_index == movement_state->player_movement_states[2].current_tile_index) {
            } else if (p_tile_index == movement_state->player_movement_states[3].current_tile_index) {
            }

            if (render_fire) {
              //board_state->show_fire(p_tile_index);
            }
          };


          for (uint32_t i=1; i <= blast_up_count; ++i) {
            const uint32_t target_tile_index = tile_index - (i * 13);
            explode_tile(target_tile_index);
          }
          for (uint32_t i=1; i <= blast_down_count; ++i) {
            const uint32_t target_tile_index = tile_index + (i * 13);
            explode_tile(target_tile_index);
          }
          for (uint32_t i=1; i <= blast_right_count; ++i) {
            const uint32_t target_tile_index = tile_index + i;
            explode_tile(target_tile_index);
          }
          for (uint32_t i=1; i <= blast_left_count; ++i) {
            const uint32_t target_tile_index = tile_index - i;
            explode_tile(target_tile_index);
          }
        }
      }
    }
  }
};

HandControllers* hands          = new HandControllers();
Board* board                    = new Board();
Bomberman* player_1             = new Bomberman();
Bomberman* player_2             = new Bomberman();
Bomberman* player_3             = new Bomberman();
Bomberman* player_4             = new Bomberman();
MovementSystem* movement_system = new MovementSystem();
BombSystem* bomb_system         = new BombSystem();

void head_pose_dependent_sim() {
  hands->update();
}

void SimulationState::init() {
  hands->init();

  directional_light.update_direction({0.0f, -1.0f, 0.0f});
  directional_light.update_color({1.0f, 1.0f, 1.0f});
  update_ambient_light_intensity(0.25f);

  player_1->init(1);
  player_2->init(2);
  player_3->init(3);
  player_4->init(4);

  board->init();
  player_1->transform.position = board->player_1_start_position;
  player_2->transform.position = board->player_2_start_position;
  player_3->transform.position = board->player_3_start_position;
  player_4->transform.position = board->player_4_start_position;

  movement_system->reset(board, player_1, player_2, player_3, player_4);
  bomb_system->init(board, movement_system);
}

void SimulationState::update() {
  PROFILE_FUNCTION();

  //play_audio_source(player_1->sound_id);

  if (input_state.exit || input_state.gamepad_exit) {
    platform_request_exit();
  }

  constexpr float movement_threshold = 0.5f;
  bool player_moved = false;
  if (input_state.move_player.x > movement_threshold) { // move right
    player_moved = movement_system->move_player(player_1->player_id, Bomberman::GlobalDirection::Right);
  } else if ( input_state.move_player.x < (-1.0f * movement_threshold) ) {  // move left
    player_moved = movement_system->move_player(player_1->player_id, Bomberman::GlobalDirection::Left);
  } else if ( input_state.move_player.y > movement_threshold ) {  // move up
    player_moved = movement_system->move_player(player_1->player_id, Bomberman::GlobalDirection::Up);
  } else if ( input_state.move_player.y < (-1.0 * movement_threshold) ) {  // move down
    player_moved = movement_system->move_player(player_1->player_id, Bomberman::GlobalDirection::Down);
  }
  movement_system->update();

  if (player_moved) player_1->skin.play_animation(player_1->animations.first_animation + Bomberman::running_animation_offset, 1.65f, true);

  if (input_state.action_button || input_state.gamepad_action_button) {
    bomb_system->place_bomb(player_1->player_id);
  }

  if (input_state.move_board && ( !movement_system->is_player_moving_bits[0] && !movement_system->is_player_moving_bits[1] && !movement_system->is_player_moving_bits[2] && !movement_system->is_player_moving_bits[3] )) {
    const Vector3f moved_vector = board->move(input_state.right_hand_transform.position);

    player_1->transform.position = add_vectors(player_1->transform.position, moved_vector);
    player_2->transform.position = add_vectors(player_2->transform.position, moved_vector);
    player_3->transform.position = add_vectors(player_3->transform.position, moved_vector);
    player_4->transform.position = add_vectors(player_4->transform.position, moved_vector);
  }

  bomb_system->update();
  hands->update();
  player_1->update();
  player_2->update();
  player_3->update();
  player_4->update();
}

void SimulationState::exit() {
}
