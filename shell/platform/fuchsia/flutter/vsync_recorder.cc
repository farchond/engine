// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vsync_recorder.h"

#include <mutex>

namespace flutter_runner {

namespace {

std::mutex g_mutex;

// Since we don't have any presentation info until we call |Present| for the
// first time, assume a 60hz refresh rate in the meantime.
constexpr fml::TimeDelta kDefaultPresentationInterval =
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
      fml::TimePoint::FromEpochDelta(fml::TimeDelta::FromNanoseconds(
                  next_presentation_info_.latch_point())),
                  kDefaultPresentationInterval
    };
  }
  return {fml::TimePoint::Now(), fml::TimePoint::Now(), kDefaultPresentationInterval};
}

/*
void VsyncRecorder::UpdateVsyncInfo(
    fuchsia::images::PresentationInfo presentation_info) {
  std::unique_lock<std::mutex> lock(g_mutex);
  if (last_presentation_info_ &&
      presentation_info.presentation_time >
          last_presentation_info_->presentation_time) {
    last_presentation_info_ = presentation_info;
  } else if (!last_presentation_info_) {
    last_presentation_info_ = presentation_info;
  }
}
*/

void VsyncRecorder::UpdateFramePresentedInfo(fuchsia::scenic::scheduling::FramePresentedInfo info) {

}

void VsyncRecorder::UpdateFuturePresentationTimes(fuchsia::scenic::scheduling::FuturePresentationTimes info) {
  std::unique_lock<std::mutex> lock(g_mutex);

  // Get earliest vsync time that is past our current time.
  for (auto& presentation_info : info.future_presentations) {
    if (presentation_info.presentation_time() > next_presentation_info_.presentation_time()) {
      next_presentation_info_.set_presentation_time(presentation_info.presentation_time());
      next_presentation_info_.set_latch_point(presentation_info.latch_point());
      break;
    }
  }

}

}  // namespace flutter_runner
