// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "session_connection.h"

#include "flutter/fml/make_copyable.h"
#include "lib/fidl/cpp/optional.h"
#include "lib/ui/scenic/cpp/commands.h"
#include "vsync_recorder.h"
#include "vsync_waiter.h"

static fml::TimePoint get_next_target_presentation_time(
    fml::TimePoint present_requested_time,
    fml::TimePoint last_targeted_present,
    fml::TimeDelta minimum_frame_build_time,
    int max_frames_in_flight,
    std::deque<std::pair<fml::TimePoint, fml::TimePoint>>&
        future_presentation_infos,
    flutter_runner::VsyncInfo vsync_info) {
  // The minimum time we can present at based on the current time and how much
  // time we expect it takes to build the next frame.
  fml::TimePoint earliest_latch_time =
      present_requested_time + minimum_frame_build_time;

  // The minimum time we can present at due to us wanting to target the next
  // vsync after the last targeted vsync. Keep in mind the
  // |last_targeted_present_| is already adjusted for vsync_drift, so we don't
  // have to account for it here.
  fml::TimePoint earliest_vsync_time =
      last_targeted_present + vsync_info.presentation_interval;

  auto it = future_presentation_infos.begin();
  while (it != future_presentation_infos.end()) {
    fml::TimePoint latch_time = it->first;
    fml::TimePoint vsync_time = it->second;

    if (latch_time >= earliest_latch_time &&
        vsync_time >= earliest_vsync_time) {
      break;
    }

    it++;
  }

  fml::TimePoint target_presentation_time;
  if (it == future_presentation_infos.end()) {
    // If we don't have a vsync time sufficiently in the future to target, this
    // means we have not produced a frame in the last 5 or so vsyncs. Therefore
    // we should target the earliest possible time.
    target_presentation_time =
        std::max(earliest_latch_time, earliest_vsync_time);
  } else {
    // Else, we should target the vsync_time in the correct
    // |future_presentation_info|, while accounting for some vsync drift.
    target_presentation_time = it->second;
    target_presentation_time =
        target_presentation_time - (vsync_info.presentation_interval / 2);
  }

  // We have established the minimum time to target, but we must also make sure
  // we're not falling too far behind. So we must cap our
  // target_presentation_time with the max time. The max time is based on
  // how many frames in flight we can have simultaneously.
  fml::TimePoint latest_possible_presentation_time =
      present_requested_time +
      (vsync_info.presentation_interval * max_frames_in_flight);
  target_presentation_time =
      std::min(target_presentation_time, latest_possible_presentation_time);

  return target_presentation_time;
}

namespace flutter_runner {

SessionConnection::SessionConnection(
    std::string debug_label,
    fuchsia::ui::views::ViewToken view_token,
    scenic::ViewRefPair view_ref_pair,
    fidl::InterfaceHandle<fuchsia::ui::scenic::Session> session,
    fml::closure session_error_callback,
    on_frame_presented_event on_frame_presented_callback,
    zx_handle_t vsync_event_handle)
    : debug_label_(std::move(debug_label)),
      session_wrapper_(session.Bind(), nullptr),
      root_view_(&session_wrapper_,
                 std::move(view_token),
                 std::move(view_ref_pair.control_ref),
                 std::move(view_ref_pair.view_ref),
                 debug_label),
      root_node_(&session_wrapper_),
      surface_producer_(
          std::make_unique<VulkanSurfaceProducer>(&session_wrapper_)),
      scene_update_context_(&session_wrapper_, surface_producer_.get()),
      on_frame_presented_callback_(std::move(on_frame_presented_callback)),
      vsync_event_handle_(vsync_event_handle) {
  session_wrapper_.set_error_handler(
      [callback = session_error_callback](zx_status_t status) { callback(); });

  // Set the |fuchsia::ui::scenic::OnFramePresented()| event handler that will
  // fire every time a set of one or more frames is presented.
  session_wrapper_.set_on_frame_presented_handler(
      [this, handle = vsync_event_handle_](
          fuchsia::scenic::scheduling::FramePresentedInfo info) {
        // Update Scenic's limit for our remaining frames in flight allowed.
        size_t num_presents_handled = info.presentation_infos.size();
        frames_in_flight_allowed_ = info.num_presents_allowed;

        // A frame was presented: Update our |frames_in_flight| to match the
        // updated unfinalized present requests.
        frames_in_flight_ -= num_presents_handled;
        FML_DCHECK(frames_in_flight_ >= 0);

        if (num_presents_handled > 1) {
          // This is not ideal, it means that we missed a frame at some point
          // and a future vsync updated 2x/3x/etc. the content.
          FML_LOG(WARNING)
              << "Handled multiple Present()s in a single vsync. Handled "
              << num_presents_handled;
        }

        VsyncRecorder::GetInstance().UpdateFramePresentedInfo(
            zx::time(info.actual_presentation_time));

        // Call the client-provided callback once we are done using |info|.
        on_frame_presented_callback_(std::move(info));

        if (present_session_pending_) {
          PresentSession();
        }
        ToggleSignal(handle, true);
      }  // callback
  );

  session_wrapper_.SetDebugName(debug_label_);

  root_view_.AddChild(root_node_);
  root_node_.SetEventMask(fuchsia::ui::gfx::kMetricsEventMask |
                          fuchsia::ui::gfx::kSizeChangeHintEventMask);

  // Get information to finish initialization and only then allow Present()s.
  session_wrapper_.RequestPresentationTimes(
      /*requested_prediction_span=*/0,
      [this](fuchsia::scenic::scheduling::FuturePresentationTimes info) {
        frames_in_flight_allowed_ = info.remaining_presents_in_flight_allowed;

        // If Scenic alloted us 0 frames to begin with, we should fail here.
        FML_CHECK(frames_in_flight_allowed_ > 0);

        VsyncRecorder::GetInstance().UpdateNextPresentationInfo(
            std::move(info));

        // Signal is initially high indicating availability of the session.
        ToggleSignal(vsync_event_handle_, true);
        initialized_ = true;

        present_requested_time_ = fml::TimePoint::Now();
        PresentSession();
      });
}

SessionConnection::~SessionConnection() = default;

void SessionConnection::Present(
    flutter::CompositorContext::ScopedFrame* frame) {
  TRACE_EVENT0("gfx", "SessionConnection::Present");

  TRACE_FLOW_BEGIN("gfx", "SessionConnection::PresentSession",
                   next_present_session_trace_id_);
  next_present_session_trace_id_++;

  // TODO: How should we handle it when we have multiple present_requests before
  // we drain them?
  present_requested_time_ = fml::TimePoint::Now();

  // Throttle frame submission to Scenic if we already have the maximum amount
  // of frames in flight. This allows the paint tasks for this frame to execute
  // in parallel with the presentation of previous frame but still provides
  // back-pressure to prevent us from enqueuing even more work.
  if (initialized_ && frames_in_flight_ < kMaxFramesInFlight) {
    PresentSession();
  } else {
    TRACE_EVENT0("gfx", "SessionConnection::NOPRESENT");
    // We should never exceed the max frames in flight.
    FML_CHECK(frames_in_flight_ == kMaxFramesInFlight || !initialized_);

    present_session_pending_ = true;
    ToggleSignal(vsync_event_handle_, false);
  }

  if (frame) {
    // Execute paint tasks and signal fences.
    auto surfaces_to_submit = scene_update_context_.ExecutePaintTasks(*frame);

    // Tell the surface producer that a present has occurred so it can perform
    // book-keeping on buffer caches.
    surface_producer_->OnSurfacesPresented(std::move(surfaces_to_submit));
  }
}

void SessionConnection::OnSessionSizeChangeHint(float width_change_factor,
                                                float height_change_factor) {
  surface_producer_->OnSessionSizeChangeHint(width_change_factor,
                                             height_change_factor);
}

void SessionConnection::set_enable_wireframe(bool enable) {
  session_wrapper_.Enqueue(
      scenic::NewSetEnableDebugViewBoundsCmd(root_view_.id(), enable));
}

void SessionConnection::EnqueueClearOps() {
  // We are going to be sending down a fresh node hierarchy every frame. So just
  // enqueue a detach op on the imported root node.
  session_wrapper_.Enqueue(scenic::NewDetachChildrenCmd(root_node_.id()));
}

void SessionConnection::PresentSession() {
  TRACE_EVENT0("gfx", "SessionConnection::PresentSession");

  // If we cannot call Present2() because we have no more Scenic frame budget,
  // then we must wait until the OnFramePresented() event fires so we can
  // continue our work.
  //
  // This should never happen unless we are starting up given that we are
  // keeping track of our own frames in flight.
  if (frames_in_flight_allowed_ == 0) {
    FML_CHECK(!initialized_ || present_session_pending_);
    return;
  }

  present_session_pending_ = false;

  while (processed_present_session_trace_id_ < next_present_session_trace_id_) {
    TRACE_FLOW_END("gfx", "SessionConnection::PresentSession",
                   processed_present_session_trace_id_);
    processed_present_session_trace_id_++;
  }
  TRACE_FLOW_BEGIN("gfx", "Session::Present", next_present_trace_id_);
  next_present_trace_id_++;

  ++frames_in_flight_;

  VsyncInfo vsync_info = VsyncRecorder::GetInstance().GetCurrentVsyncInfo();
  FML_CHECK(present_requested_time_ > fml::TimePoint::Min());

  // Replace this next line:
  fml::TimePoint target_presentation_time = get_next_target_presentation_time(
      present_requested_time_, last_targeted_present_,
      minimum_frame_build_time_, kMaxFramesInFlight, future_presentation_infos_,
      vsync_info);

  // Reset present_requested_time_.
  present_requested_time_ = fml::TimePoint::Min();

  // We must make sure our target_presentation_time is greater than or
  // equal to the last target_presentation_time.
  if (target_presentation_time < last_targeted_present_) {
    FML_LOG(WARNING) << "Fucked up targeted present times:";
    FML_LOG(WARNING)
        << "Last: " << last_targeted_present_.ToEpochDelta().ToMicroseconds()
        << " Current: "
        << target_presentation_time.ToEpochDelta().ToMicroseconds();
    target_presentation_time = last_targeted_present_;
  }

  last_targeted_present_ = target_presentation_time;

  // TODO: Remove. Used for debugging asking for times in the future.
  fml::TimePoint next =
      fml::TimePoint::Now() + (vsync_info.presentation_interval * 3);
  (void)next;

  // Flush all session ops. Paint tasks may not yet have executed but those are
  // fenced. The compositor can start processing ops while we finalize paint
  // tasks.
  session_wrapper_.Present2(
      /*requested_presentation_time=*/next.ToEpochDelta().ToNanoseconds(),
      // target_presentation_time.ToEpochDelta().ToNanoseconds(),
      /*requested_prediction_span=*/
      vsync_info.presentation_interval.ToNanoseconds() * 6,
      [this](fuchsia::scenic::scheduling::FuturePresentationTimes info) {
        frames_in_flight_allowed_ = info.remaining_presents_in_flight_allowed;

        // Clear |future_presentation_infos_| and replace it with the updated
        // information.
        std::deque<std::pair<fml::TimePoint, fml::TimePoint>>().swap(
            future_presentation_infos_);

        for (fuchsia::scenic::scheduling::PresentationInfo& presentation_info :
             info.future_presentations) {
          future_presentation_infos_.push_back(
              {fml::TimePoint::FromEpochDelta(fml::TimeDelta::FromNanoseconds(
                   presentation_info.latch_point())),
               fml::TimePoint::FromEpochDelta(fml::TimeDelta::FromNanoseconds(
                   presentation_info.presentation_time()))});
        }

        FML_DCHECK(future_presentation_infos_.size() ==
                   info.future_presentations.size());
        VsyncRecorder::GetInstance().UpdateNextPresentationInfo(
            std::move(info));
      });

  // Prepare for the next frame. These ops won't be processed till the next
  // present.
  EnqueueClearOps();
}

void SessionConnection::ToggleSignal(zx_handle_t handle, bool set) {
  const auto signal = VsyncWaiter::SessionPresentSignal;
  auto status = zx_object_signal(handle,            // handle
                                 set ? 0 : signal,  // clear mask
                                 set ? signal : 0   // set mask
  );
  if (status != ZX_OK) {
    FML_LOG(ERROR) << "Could not toggle vsync signal: " << set;
  }
}

}  // namespace flutter_runner
