// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vsync_recorder.h"

#include <mutex>

namespace flutter_runner {

namespace {

std::mutex g_mutex;

// Assume a 60hz refresh rate before we have enough past
// |fuchsia::scenic::scheduling::PresentationInfo|s to calculate it ourselves.
static constexpr fml::TimeDelta kDefaultPresentationInterval =
    fml::TimeDelta::FromSecondsF(1.0 / 60.0);

}  // namespace

VsyncRecorder& VsyncRecorder::GetInstance() {
  static VsyncRecorder vsync_recorder;
  return vsync_recorder;
}

VsyncInfo VsyncRecorder::GetCurrentVsyncInfo() const {
  {
    std::unique_lock<std::mutex> lock(g_mutex);
    return {fml::TimePoint::FromEpochDelta(fml::TimeDelta::FromNanoseconds(
                next_presentation_info_.presentation_time())),
            kDefaultPresentationInterval};
  }
}

void VsyncRecorder::FuturePresentationTimesUpdate(
    fuchsia::scenic::scheduling::FuturePresentationTimes info) {
  std::unique_lock<std::mutex> lock(g_mutex);

  // Get earliest vsync time that is in the future.
  for (auto& presentation_info : info.future_presentations) {
    if (presentation_info.presentation_time() >
        next_presentation_info_.presentation_time()) {
      next_presentation_info_.set_presentation_time(
          presentation_info.presentation_time());
      break;
    }
  }
}

}  // namespace flutter_runner
