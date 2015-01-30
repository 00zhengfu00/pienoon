// Copyright 2014 Google Inc. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "precompiled.h"
#include "bulk_spline_evaluator.h"
#include "impel_engine.h"
#include "impel_init.h"


namespace impel {

using fpl::CompactSpline;
using fpl::BulkSplineEvaluator;
using fpl::Range;


// Add some buffer to the y-range to allow for intermediate nodes
// that go above or below the supplied nodes.
static const float kYRangeBufferPercent = 1.2f;


struct SmoothImpelData  {
  SmoothImpelData() : local_spline(nullptr) {}

  void Initialize(const SmoothImpelInit& init_param) {
    init = init_param;
  }

  // If we own the spline, recycle it in the spline pool.
  CompactSpline* local_spline;

  // Keep a local copy of the init params.
  SmoothImpelInit init;
};


class SmoothImpelProcessor : public ImpelProcessor<float> {
 public:
  virtual ~SmoothImpelProcessor() {
    for (auto it = spline_pool_.begin(); it != spline_pool_.end(); ++it) {
      delete *(it);
    }
  }

  virtual ImpellerType Type() const { return SmoothImpelInit::kType; }

  // Accessors to allow the user to get and set simluation values.
  virtual float Value(ImpelIndex index) const { return interpolator_.Y(index); }
  virtual float Velocity(ImpelIndex index) const {
    return interpolator_.Derivative(index);
  }
  virtual float TargetValue(ImpelIndex index) const {
    return interpolator_.EndY(index);
  }
  virtual float Difference(ImpelIndex index) const {
    const SmoothImpelData& d = Data(index);
    return d.init.Normalize(interpolator_.EndY(index) -
                            interpolator_.Y(index));
  }

  virtual void AdvanceFrame(ImpelTime delta_time) {
    Defragment();
    interpolator_.AdvanceFrame(delta_time);
  }

  virtual void SetState(ImpelIndex index, const ImpellerState& s) {
    SmoothImpelData& d = Data(index);

    const bool use_external_spline = s.valid & kTargetWaypointsValid;
    if (use_external_spline) {
      // Return the local spline to the spline pool. We use external
      // splines now.
      FreeSpline(d.local_spline);

      // Initialize spline to follow way points.
      // Snaps the current value and velocity to the way point's start value
      // and velocity.
      interpolator_.SetSpline(index, *s.waypoints, s.waypoints_start_time);

    } else {
      // Initialize spline to match specified parameters. We maintain current
      // values for any parameters that aren't specified.
      const float start_y = (s.valid & kValueValid) ? s.value :
                            interpolator_.X(index);
      const float start_derivative = (s.valid & kVelocityValid) ? s.velocity :
                                     interpolator_.Derivative(index);
      const float end_y = (s.valid & kTargetValueValid) ? s.target_value :
                          interpolator_.EndY(index);
      const float end_derivative = (s.valid & kTargetVelocityValid) ?
                                   s.target_velocity : 0.0f;
      const float end_x = (s.valid & kTargetTimeValid) ? s.target_time :
                          interpolator_.EndX(index);
      const float x_granularity = CompactSpline::RecommendXGranularity(end_x);
      const Range y_range =
          fpl::CreateValidRange(start_y, end_y).Lengthen(kYRangeBufferPercent);

      // Ensure we have a local spline available, allocated from our
      // pool of splines.
      if (d.local_spline == nullptr) {
        d.local_spline = AllocateSpline();
      }

      // An intermediate node might be inserted to make the cubic curve well
      // behaved, so reserve 3 nodes in the spline.
      d.local_spline->Init(y_range, x_granularity, 3);
      d.local_spline->AddNode(0.0f, start_y, start_derivative);
      d.local_spline->AddNode(end_x, end_y, end_derivative);
      interpolator_.SetSpline(index, *d.local_spline);
    }
  }

  virtual int Priority() const { return 0; }

 protected:
  virtual void InitializeIndex(const ImpelInit& init, ImpelIndex index,
                               ImpelEngine* engine) {
    (void)engine;
    Data(index).Initialize(static_cast<const SmoothImpelInit&>(init));
  }

  virtual void RemoveIndex(ImpelIndex index) {
    // Return the spline to the pool of splines.
    SmoothImpelData& d = Data(index);
    FreeSpline(d.local_spline);
    d.local_spline = nullptr;
  }

  virtual void MoveIndex(ImpelIndex old_index, ImpelIndex new_index) {
    data_[new_index] = data_[old_index];
    interpolator_.MoveIndex(old_index, new_index);
  }

  virtual void SetNumIndices(ImpelIndex num_indices) {
    data_.resize(num_indices);
    interpolator_.SetNumIndices(num_indices);
  }

  const SmoothImpelData& Data(ImpelIndex index) const {
    assert(ValidIndex(index));
    return data_[index];
  }

  SmoothImpelData& Data(ImpelIndex index) {
    assert(ValidIndex(index));
    return data_[index];
  }

  CompactSpline* AllocateSpline() {
    // Only create a new spline if there are no left in the pool.
    if (spline_pool_.empty())
      return new CompactSpline();

    // Return a spline from the pool. Eventually we'll reach a high water mark
    // and we will stop allocating new splines.
    CompactSpline* spline = spline_pool_.back();
    spline_pool_.pop_back();
    return spline;
  }

  void FreeSpline(CompactSpline* spline) {
    if (spline != nullptr) {
      spline_pool_.push_back(spline);
    }
  }

  // Hold index-specific data, for example the init params and a pointer
  // to the spline allocated from 'spline_pool_'.
  std::vector<SmoothImpelData> data_;

  // Holds unused splines. When we need another local spline (because we're
  // supplied with target values but not the actual curve to get there),
  // try to recycle an old one from this pool first.
  std::vector<CompactSpline*> spline_pool_;

  // Perform the spline evaluation, over time. Indices in 'interpolator_'
  // are the same as the ImpelIndex values in this class.
  BulkSplineEvaluator interpolator_;
};

IMPEL_INSTANCE(SmoothImpelInit, SmoothImpelProcessor);


} // namespace impel


