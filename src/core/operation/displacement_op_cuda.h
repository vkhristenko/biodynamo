// //
// -----------------------------------------------------------------------------
// //
// // Copyright (C) The BioDynaMo Project.
// // All Rights Reserved.
// //
// // Licensed under the Apache License, Version 2.0 (the "License");
// // you may not use this file except in compliance with the License.
// //
// // See the LICENSE file distributed with this work for details.
// // See the NOTICE file distributed with this work for additional information
// // regarding copyright ownership.
// //
// //
// -----------------------------------------------------------------------------

#ifndef CORE_OPERATION_DISPLACEMENT_OP_CUDA_H_
#define CORE_OPERATION_DISPLACEMENT_OP_CUDA_H_

#include <vector>

#include "core/gpu/displacement_op_cuda_kernel.h"
#include "core/operation/bound_space_op.h"
#include "core/resource_manager.h"
#include "core/shape.h"
#include "core/sim_object/cell.h"
#include "core/simulation.h"
#include "core/util/log.h"
#include "core/util/thread_info.h"
#include "core/util/type.h"

namespace bdm {

inline void IsNonSphericalObjectPresent(const SimObject* so, bool* answer) {
  if (so->GetShape() != Shape::kSphere) {
    *answer = true;
  }
}

struct Bar : public Functor<void, SimObject*, SoHandle> {
  bool bound_space = false;
  double min_bound = 0;
  double max_bound = 1;
  std::vector<std::array<double, 3>> cell_movements;

  Bar(std::vector<std::array<double, 3>> cm, bool bs, double minb,
      double maxb) {
    cell_movements = cm;
    bound_space = bs;
    min_bound = minb;
    max_bound = maxb;
  }

  void operator()(SimObject* so, SoHandle soh) override {
    auto* cell = dynamic_cast<Cell*>(so);
    auto idx = soh.GetElementIdx();
    Double3 new_pos;
    new_pos[0] = cell_movements[idx][0];
    new_pos[1] = cell_movements[idx][1];
    new_pos[2] = cell_movements[idx][2];
    cell->UpdatePosition(new_pos);
    if (bound_space) {
      ApplyBoundingBox(so, min_bound, max_bound);
    }
  }
};

struct Foo : public Functor<void, SimObject*, SoHandle> {
  bool is_non_spherical_object = false;
  std::vector<Double3> cell_positions;
  std::vector<double> cell_diameters;
  std::vector<double> cell_adherence;
  std::vector<Double3> cell_tractor_force;
  std::vector<uint32_t> cell_boxid;
  std::vector<double> mass;
  std::vector<uint32_t> successors;
  std::vector<ElementIdx_t> offset;

  Foo(uint32_t num_objects, const std::vector<ElementIdx_t>& offs) {
    cell_positions.resize(num_objects);
    cell_diameters.resize(num_objects);
    cell_adherence.resize(num_objects);
    cell_tractor_force.resize(num_objects);
    cell_boxid.resize(num_objects);
    mass.resize(num_objects);
    successors.resize(num_objects);
    offset = offs;
  }

  void operator()(SimObject* so, SoHandle soh) override {
    // Check if there are any non-spherical objects in our simulation, because
    // GPU accelerations currently supports only sphere-sphere interactions
    IsNonSphericalObjectPresent(so, &is_non_spherical_object);
    if (is_non_spherical_object) {
      Log::Fatal("DisplacementOpCuda",
                 "\nWe detected a non-spherical object during the GPU "
                 "execution. This is currently not supported.");
      return;
    }
    auto* cell = bdm_static_cast<Cell*>(so);
    auto idx = offset[soh.GetNumaNode()] + soh.GetElementIdx();
    mass[idx] = cell->GetMass();
    cell_diameters[idx] = cell->GetDiameter();
    cell_adherence[idx] = cell->GetAdherence();
    cell_tractor_force[idx] = cell->GetTractorForce();
    cell_positions[idx] = cell->GetPosition();
    cell_boxid[idx] = cell->GetBoxIdx();

    // Populate successor list
    successors[idx] = offset[soh.GetNumaNode()] + grid->successors_[soh].GetElementIdx();
  }
};

/// Defines the 3D physical interactions between physical objects
class DisplacementOpCuda {
 public:
  DisplacementOpCuda() {}
  ~DisplacementOpCuda() {}

  void operator()() {
    auto* sim = Simulation::GetActive();
    auto* grid = sim->GetGrid();
    auto* param = sim->GetParam();
    auto* rm = sim->GetResourceManager();

    auto num_numa_nodes = ThreadInfo::GetInstance()->GetNumaNodes();
    std::vector<ElementIdx_t> offset(num_numa_nodes);
    offset[0] = 0;
    for (auto& nn = 1; nn < num_numa_nodes - 1; nn++) {
      offset[nn] = offset[nn - 1] + rm->GetNumSimObjects(nn);
    }

    uint32_t total_num_objects = rm->GetNumSimObjects();

    // Cannot use Double3 here, because the `data()` function returns a const
    // pointer to the underlying array, whereas the CUDA kernel will cast it to
    // a void pointer. The conversion of `const double *` to `void *` is
    // illegal.
    std::vector<std::array<double, 3>> cell_movements(total_num_objects);
    std::vector<uint32_t> starts;
    std::vector<uint16_t> lengths;
    uint32_t box_length;
    std::array<uint32_t, 3> num_boxes_axis;
    std::array<int32_t, 3> grid_dimensions;
    double squared_radius =
        grid->GetLargestObjectSize() * grid->GetLargestObjectSize();

    Foo f(total_num_objects, offset);
    rm->ApplyOnAllElementsParallelDynamic(1000, f);

    starts.resize(grid->boxes_.size());
    lengths.resize(grid->boxes_.size());
    size_t i = 0;
    for (auto& box : grid->boxes_) {
      starts[i] = box.start_.GetElementIdx();
      lengths[i] = box.length_;
      i++;
    }
    grid->GetGridInfo(&box_length, &num_boxes_axis, &grid_dimensions);

    // If this is the first time we perform physics on GPU using CUDA
    if (cdo_ == nullptr) {
      // Allocate 25% more memory so we don't need to reallocate GPU memory
      // for every (small) change
      uint32_t new_total_num_objects =
          static_cast<uint32_t>(1.25 * total_num_objects);
      uint32_t new_num_boxes = static_cast<uint32_t>(1.25 * starts.size());

      // Store these extended buffer sizes for future reference
      total_num_objects_ = new_total_num_objects;
      num_boxes_ = new_num_boxes;

      // Allocate required GPU memory
      cdo_ = new DisplacementOpCudaKernel(new_total_num_objects, new_num_boxes);
    } else {
      // If the number of simulation objects increased
      if (total_num_objects >= total_num_objects_) {
        Log::Info("DisplacementOpCuda",
                  "\nThe number of cells increased signficantly (from ",
                  total_num_objects_, " to ", total_num_objects,
                  "), so we allocate bigger GPU buffers\n");
        uint32_t new_total_num_objects =
            static_cast<uint32_t>(1.25 * total_num_objects);
        total_num_objects_ = new_total_num_objects;
        cdo_->ResizeCellBuffers(new_total_num_objects);
      }

      // If the neighbor grid size increased
      if (starts.size() >= num_boxes_) {
        Log::Info("DisplacementOpCuda",
                  "\nThe number of boxes increased signficantly (from ",
                  num_boxes_, " to ", "), so we allocate bigger GPU buffers\n");
        uint32_t new_num_boxes = static_cast<uint32_t>(1.25 * starts.size());
        num_boxes_ = new_num_boxes;
        cdo_->ResizeGridBuffers(new_num_boxes);
      }
    }

    cdo_->LaunchDisplacementKernel(
        f.cell_positions.data()->data(), f.cell_diameters.data(),
        f.cell_tractor_force.data()->data(), f.cell_adherence.data(),
        f.cell_boxid.data(), f.mass.data(), &(param->simulation_time_step_),
        &(param->simulation_max_displacement_), &squared_radius,
        &total_num_objects, starts.data(), lengths.data(), successors.data(),
        &box_length, num_boxes_axis.data(), grid_dimensions.data(),
        cell_movements.data()->data());

    // set new positions after all updates have been calculated
    // otherwise some cells would see neighbors with already updated positions
    // which would lead to inconsistencies

    Bar b(cell_movements, param->bound_space_, param->min_bound_,
          param->max_bound_);
    rm->ApplyOnAllElementsParallelDynamic(1000, b);
  }

 private:
  DisplacementOpCudaKernel* cdo_ = nullptr;
  uint32_t num_boxes_ = 0;
  uint32_t total_num_objects_ = 0;
};

}  // namespace bdm

#endif  // CORE_OPERATION_DISPLACEMENT_OP_CUDA_H_
