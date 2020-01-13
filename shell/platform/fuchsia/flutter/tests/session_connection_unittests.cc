// Copyright 2019 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"

#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>
#include <lib/zx/time.h>

#include <fuchsia/sys/cpp/fidl.h>

#include "flutter/fml/platform/fuchsia/message_loop_fuchsia.h"
#include "flutter/shell/platform/fuchsia/flutter/logging.h"
#include "flutter/shell/platform/fuchsia/flutter/runner.h"
#include "flutter/shell/platform/fuchsia/flutter/session_connection.h"
#include "flutter/shell/platform/fuchsia/flutter/vsync_recorder.h"

using namespace flutter_runner;

namespace flutter_runner_test {

// using SessionConnectionTest = ::testing::Test;

class SessionConnectionTest : public ::testing::Test {};

TEST_F(SessionConnectionTest, PresentTest) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  thrd_t fidl_thread;
  ZX_ASSERT(ZX_OK == loop.StartThread("FIDL_thread", &fidl_thread));

  auto context = sys::ComponentContext::Create();
  auto svc = context->svc();
  auto scenic = svc->Connect<fuchsia::ui::scenic::Scenic>();

  fidl::InterfaceHandle<fuchsia::ui::scenic::Session> session;
  fidl::InterfaceHandle<fuchsia::ui::scenic::SessionListener> session_listener;
  auto session_listener_request = session_listener.NewRequest();

  scenic->CreateSession(session.NewRequest(), session_listener.Bind());

  // Grab the parent environment services. The platform view may want to access
  // some of these services.
  fuchsia::sys::EnvironmentPtr environment;
  svc->Connect(environment.NewRequest());
  fidl::InterfaceHandle<fuchsia::sys::ServiceProvider>
      parent_environment_service_provider;
  environment->GetServices(parent_environment_service_provider.NewRequest());
  environment.Unbind();

  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();

  zx::event vsync_event;

  ASSERT_TRUE(zx::event::create(0, &vsync_event) == ZX_OK);

  fml::closure on_session_error_callback = []() { FML_CHECK(false); };

  flutter_runner::SessionConnection session_connection(
      "debug label", std::move(view_token), std::move(session),
      on_session_error_callback, vsync_event.get());

  // TODO: Replace sleeps with a better mechanism.
  std::this_thread::sleep_for(std::chrono::microseconds(10000000));
  // Set wireframe to false to begin with.
  session_connection.set_enable_wireframe(false);
  session_connection.Present(nullptr);

  /*
          std::this_thread::sleep_for(std::chrono::microseconds(2000000));
    session_connection.set_enable_wireframe(true);
    session_connection.Present(nullptr);


          std::this_thread::sleep_for(std::chrono::microseconds(3000000));
    session_connection.set_enable_wireframe(false);
    session_connection.Present(nullptr);
    */

  std::this_thread::sleep_for(std::chrono::microseconds(1000000000000));
}

}  // namespace flutter_runner_test
