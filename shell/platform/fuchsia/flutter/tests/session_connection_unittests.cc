// Copyright 2020 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"

#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>

#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>

#include "flutter/shell/platform/fuchsia/flutter/logging.h"
#include "flutter/shell/platform/fuchsia/flutter/runner.h"
#include "flutter/shell/platform/fuchsia/flutter/session_connection.h"

using namespace flutter_runner;

namespace flutter_runner_test {

class SessionConnectionTest : public ::testing::Test {
 public:
  void SetUp() override {
    FML_CHECK(ZX_OK ==
              loop_.StartThread("SessionConnectionTestThread", &fidl_thread_));

    auto session_listener_request = session_listener_.NewRequest();
    auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();
    view_token_ = std::move(view_token);

    scenic_->CreateSession(session_.NewRequest(), session_listener_.Bind());
    presenter_->PresentView(std::move(view_holder_token), nullptr);

    FML_CHECK(zx::event::create(0, &vsync_event_) == ZX_OK);
  }

  // Warning: Initialization order matters here. |loop_| must be initialized
  // before |context_| so that we have a dispatcher initialized for it.
  async::Loop loop_ = async::Loop(&kAsyncLoopConfigAttachToCurrentThread);
  std::unique_ptr<sys::ComponentContext> context_ =
      sys::ComponentContext::Create();

  // These InterfacePtrs can be initialized any time after |context_|, but must
  // be member variables so they remain in scope for the duration of the test.
  fidl::InterfacePtr<fuchsia::ui::scenic::Scenic> scenic_ =
      context_->svc()->Connect<fuchsia::ui::scenic::Scenic>();
  fidl::InterfacePtr<fuchsia::ui::policy::Presenter> presenter_ =
      context_->svc()->Connect<fuchsia::ui::policy::Presenter>();

  fidl::InterfaceHandle<fuchsia::ui::scenic::Session> session_;
  fidl::InterfaceHandle<fuchsia::ui::scenic::SessionListener> session_listener_;
  fuchsia::ui::views::ViewToken view_token_;
  zx::event vsync_event_;
  thrd_t fidl_thread_;
};

TEST_F(SessionConnectionTest, SimplePresentTest) {
  fml::closure on_session_error_callback = []() { FML_CHECK(false); };

  flutter_runner::SessionConnection session_connection(
      "debug label", std::move(view_token_), std::move(session_),
      on_session_error_callback, vsync_event_.get());

  for (int i = 0; i < 200; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    session_connection.Present(nullptr);
  }
}

TEST_F(SessionConnectionTest, BatchedPresentTest) {
  fml::closure on_session_error_callback = []() { FML_CHECK(false); };

  flutter_runner::SessionConnection session_connection(
      "debug label", std::move(view_token_), std::move(session_),
      on_session_error_callback, vsync_event_.get());

  for (int i = 0; i < 200; ++i) {
    if (i % 10 == 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    session_connection.Present(nullptr);
  }
}

}  // namespace flutter_runner_test
