// Copyright 2019 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include <fuchsia/sys/cpp/fidl.h>
#include <lib/sys/cpp/component_context.h>

//#include <fuchsia/sys/component_context/cpp/fidl.h>
//#include <fuchsia/sys/index/cpp/fidl.h>
#include "flutter/shell/platform/fuchsia/flutter/logging.h"
#include "flutter/shell/platform/fuchsia/flutter/vsync_recorder.h"

#include "flutter/shell/platform/fuchsia/flutter/runner.h"
#include "flutter/shell/platform/fuchsia/flutter/session_connection.h"

using namespace flutter_runner;

namespace flutter_runner_test {

static fuchsia::scenic::scheduling::PresentationInfo CreatePresentationInfo(
    zx_time_t latch_point,
    zx_time_t presentation_time) {
  fuchsia::scenic::scheduling::PresentationInfo info;

  info.set_latch_point(latch_point);
  info.set_presentation_time(presentation_time);
  return info;
}

// IMPORTANT NOTE: Because there only exists one VsyncRecorder, the order of
// these tests matter.

TEST(VsyncRecorderTest, DefaultValues_AreReasonable) {
  VsyncInfo vsync_info = VsyncRecorder::GetInstance().GetCurrentVsyncInfo();

  EXPECT_GE(vsync_info.presentation_time,
            fml::TimePoint::FromEpochDelta(fml::TimeDelta::FromNanoseconds(0)));

  EXPECT_GE(vsync_info.presentation_interval,
            fml::TimeDelta::FromMilliseconds(10));
}

TEST(VsyncRecorderTest, SinglePresentationInfo_IsUpdatedCorrectly) {
  std::vector<fuchsia::scenic::scheduling::PresentationInfo>
      future_presentations = {};

  // Update the |vsync_info|.
  future_presentations.push_back(
      CreatePresentationInfo(/*latch_point=*/5, /*presentation_time=*/10));
  VsyncRecorder::GetInstance().UpdateNextPresentationInfo(
      {.future_presentations = std::move(future_presentations),
       .remaining_presents_in_flight_allowed = 1});

  // Check that |vsync_info| was correctly updated.
  VsyncInfo vsync_info = VsyncRecorder::GetInstance().GetCurrentVsyncInfo();
  EXPECT_EQ(
      vsync_info.presentation_time,
      fml::TimePoint::FromEpochDelta(fml::TimeDelta::FromNanoseconds(10)));

  EXPECT_GE(vsync_info.presentation_interval,
            fml::TimeDelta::FromMilliseconds(10));
}

TEST(VsyncRecorderTest, MultiplePresentationInfos_AreUpdatedCorrectly) {
  std::vector<fuchsia::scenic::scheduling::PresentationInfo>
      future_presentations = {};

  // Update the |vsync_info|.
  future_presentations.push_back(
      CreatePresentationInfo(/*latch_point=*/15, /*presentation_time=*/20));
  future_presentations.push_back(
      CreatePresentationInfo(/*latch_point=*/25, /*presentation_time=*/30));
  VsyncRecorder::GetInstance().UpdateNextPresentationInfo(
      {.future_presentations = std::move(future_presentations),
       .remaining_presents_in_flight_allowed = 1});

  // Check that |vsync_info| was correctly updated with the first time.
  VsyncInfo vsync_info = VsyncRecorder::GetInstance().GetCurrentVsyncInfo();
  EXPECT_EQ(
      vsync_info.presentation_time,
      fml::TimePoint::FromEpochDelta(fml::TimeDelta::FromNanoseconds(20)));
  EXPECT_GE(vsync_info.presentation_interval,
            fml::TimeDelta::FromMilliseconds(10));

  // Clear and re-try with more future times!
  future_presentations.clear();
  future_presentations.push_back(
      CreatePresentationInfo(/*latch_point=*/15, /*presentation_time=*/20));
  future_presentations.push_back(
      CreatePresentationInfo(/*latch_point=*/25, /*presentation_time=*/30));
  future_presentations.push_back(
      CreatePresentationInfo(/*latch_point=*/35, /*presentation_time=*/40));
  future_presentations.push_back(
      CreatePresentationInfo(/*latch_point=*/45, /*presentation_time=*/50));
  VsyncRecorder::GetInstance().UpdateNextPresentationInfo(
      {.future_presentations = std::move(future_presentations),
       .remaining_presents_in_flight_allowed = 1});

  // Check that |vsync_info| was correctly updated with the first time.
  vsync_info = VsyncRecorder::GetInstance().GetCurrentVsyncInfo();
  EXPECT_EQ(
      vsync_info.presentation_time,
      fml::TimePoint::FromEpochDelta(fml::TimeDelta::FromNanoseconds(30)));
  EXPECT_GE(vsync_info.presentation_interval,
            fml::TimeDelta::FromMilliseconds(10));
}

}  // namespace flutter_runner_test
