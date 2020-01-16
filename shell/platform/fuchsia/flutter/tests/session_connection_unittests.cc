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

class SessionConnectionTest : public ::testing::Test {};

TEST_F(SessionConnectionTest, PresentTest) {
  // Begin setup.
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  thrd_t fidl_thread;
  ZX_ASSERT(ZX_OK ==
            loop.StartThread("SessionConnectionTestThread", &fidl_thread));

  auto context = sys::ComponentContext::Create();
  auto scenic = context->svc()->Connect<fuchsia::ui::scenic::Scenic>();
   fidl::InterfaceHandle<fuchsia::ui::scenic::Session> session_;
   fidl::InterfaceHandle<fuchsia::ui::scenic::SessionListener> session_listener_;

  auto session_listener_request = session_listener_.NewRequest();

  scenic->CreateSession(session_.NewRequest(), session_listener_.Bind());

  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();

  zx::event vsync_event;

  ASSERT_TRUE(zx::event::create(0, &vsync_event) == ZX_OK);

  fml::closure on_session_error_callback = []() { FML_CHECK(false); };

  auto presenter = context->svc()->Connect<fuchsia::ui::policy::Presenter>();
  presenter->PresentView(std::move(view_holder_token), nullptr);

  flutter_runner::SessionConnection session_connection(
      "debug label", std::move(view_token), std::move(session_),
      on_session_error_callback, vsync_event.get());

  // Indiviudal test logic begins here.

  for (int i = 0; i < 200; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    session_connection.Present(nullptr);
  }
}

}  // namespace flutter_runner_test
