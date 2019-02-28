// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <unistd.h>
#include <memory>
#include <utility>

#include <fuchsia/ledger/internal/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/backoff/exponential_backoff.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fit/function.h>
#include <lib/fxl/command_line.h>
#include "src/lib/files/unique_fd.h"
#include <lib/fxl/log_settings_command_line.h>
#include <lib/fxl/logging.h>
#include <lib/fxl/macros.h>
#include <trace-provider/provider.h>
#include <zircon/device/vfs.h>

#include "src/ledger/bin/app/constants.h"
#include "src/ledger/bin/app/ledger_repository_factory_impl.h"
#include "src/ledger/bin/cobalt/cobalt.h"
#include "src/ledger/bin/environment/environment.h"
#include "src/ledger/bin/fidl/error_notifier.h"
#include "src/ledger/bin/fidl/include/types.h"
#include "src/ledger/bin/p2p_sync/impl/user_communicator_factory_impl.h"

namespace ledger {
namespace {

constexpr fxl::StringView kNoStatisticsReportingFlag = "disable_reporting";
constexpr fxl::StringView kFirebaseApiKeyFlag = "firebase_api_key";

struct AppParams {
  bool disable_statistics = false;
  std::string firebase_api_key = "";
};

fit::deferred_action<fit::closure> SetupCobalt(
    bool disable_statistics, async_dispatcher_t* dispatcher,
    component::StartupContext* startup_context) {
  if (disable_statistics) {
    return fit::defer<fit::closure>([] {});
  }
  return InitializeCobalt(dispatcher, startup_context);
};

// App is the main entry point of the Ledger application.
//
// It is responsible for setting up the LedgerRepositoryFactory, which connects
// clients to individual Ledger instances. It should not however hold long-lived
// objects shared between Ledger instances, as we need to be able to put them in
// separate processes when the app becomes multi-instance.
class App : public ledger_internal::LedgerController {
 public:
  explicit App(AppParams app_params)
      : app_params_(app_params),
        loop_(&kAsyncLoopConfigAttachToThread),
        io_loop_(&kAsyncLoopConfigNoAttachToThread),
        trace_provider_(loop_.dispatcher()),
        startup_context_(component::StartupContext::CreateFromStartupInfo()),
        cobalt_cleaner_(SetupCobalt(app_params_.disable_statistics,
                                    loop_.dispatcher(),
                                    startup_context_.get())) {
    FXL_DCHECK(startup_context_);

    ReportEvent(CobaltEvent::LEDGER_STARTED);
  }
  ~App() override {}

  bool Start() {
    io_loop_.StartThread("io thread");

    EnvironmentBuilder builder;

    if (!app_params_.firebase_api_key.empty()) {
      builder.SetFirebaseApiKey(app_params_.firebase_api_key);
    }

    environment_ = std::make_unique<Environment>(
        builder.SetDisableStatistics(app_params_.disable_statistics)
            .SetAsync(loop_.dispatcher())
            .SetIOAsync(io_loop_.dispatcher())
            .SetStartupContext(startup_context_.get())
            .Build());
    auto user_communicator_factory =
        std::make_unique<p2p_sync::UserCommunicatorFactoryImpl>(
            environment_.get());

    factory_impl_ = std::make_unique<LedgerRepositoryFactoryImpl>(
        environment_.get(), std::move(user_communicator_factory),
        *startup_context_->outgoing().object_dir());

    startup_context_->outgoing()
        .AddPublicService<ledger_internal::LedgerRepositoryFactory>(
            [this](
                fidl::InterfaceRequest<ledger_internal::LedgerRepositoryFactory>
                    request) {
              factory_bindings_.emplace(factory_impl_.get(),
                                        std::move(request));
            });
    startup_context_->outgoing().AddPublicService<LedgerController>(
        [this](fidl::InterfaceRequest<LedgerController> request) {
          controller_bindings_.AddBinding(this, std::move(request));
        });

    startup_context_->outgoing().object_dir()->set_prop(
        "statistic_gathering", app_params_.disable_statistics ? "off" : "on");

    startup_context_->outgoing().object_dir()->set_children_callback(
        {kRepositoriesInspectPathComponent},
        [this](component::Object::ObjectVector* out) {
          factory_impl_->GetChildren(out);
        });

    loop_.Run();

    startup_context_->outgoing().object_dir()->set_children_callback(
        {kRepositoriesInspectPathComponent}, nullptr);

    return true;
  }

 private:
  // LedgerController:
  void Terminate() override { loop_.Quit(); }

  const AppParams app_params_;
  async::Loop loop_;
  async::Loop io_loop_;
  trace::TraceProvider trace_provider_;
  std::unique_ptr<component::StartupContext> startup_context_;
  fit::deferred_action<fit::closure> cobalt_cleaner_;
  std::unique_ptr<Environment> environment_;
  std::unique_ptr<LedgerRepositoryFactoryImpl> factory_impl_;
  callback::AutoCleanableSet<ErrorNotifierBinding<
      fuchsia::ledger::internal::LedgerRepositoryFactoryErrorNotifierDelegate>>
      factory_bindings_;
  fidl::BindingSet<LedgerController> controller_bindings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(App);
};

int Main(int argc, const char** argv) {
  const auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  fxl::SetLogSettingsFromCommandLine(command_line);

  AppParams app_params;
  app_params.disable_statistics =
      command_line.HasOption(kNoStatisticsReportingFlag);
  if (command_line.HasOption(kFirebaseApiKeyFlag)) {
    if (!command_line.GetOptionValue(kFirebaseApiKeyFlag,
                                     &app_params.firebase_api_key)) {
      FXL_LOG(ERROR) << "Unable to retrieve the firebase api key.";
      return 1;
    }
  }

  App app(app_params);
  if (!app.Start()) {
    return 1;
  }

  return 0;
}

}  // namespace
}  // namespace ledger

int main(int argc, const char** argv) { return ledger::Main(argc, argv); }