#ifndef INCLUDE_TOM_ENGINE_SIMULATION_H
#define INCLUDE_TOM_ENGINE_SIMULATION_H

#include <stdlib.h>
#include <bitset>

struct SimulationState {
  void init();
  void update();
  void exit();
};

void head_pose_dependent_sim();

extern SimulationState sim_state;

#endif  // INCLUDE_TOM_ENGINE_SIMULATION_H
