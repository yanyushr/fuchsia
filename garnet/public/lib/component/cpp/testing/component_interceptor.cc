// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The intercepting mechanism works by creating an Environment containing a
// custom |fuchsia.sys.Loader| and |fuchsia.sys.Runner|. This custom environment
// loader, which answers to all components launches under this environment,
// responds with an autogenerated package directory with a .cmx pointing to a
// custom runner component. The runner component, which will also under the
// environment, forwards its requests back up to environment's injected
// |fuchsia.sys.Runner| implemented here.

#include <lib/component/cpp/testing/component_interceptor.h>

#include <fs/pseudo-file.h>
#include <fs/service.h>
#include <lib/fxl/logging.h>
#include <lib/fxl/strings/substitute.h>

namespace component {
namespace testing {

namespace {
// Contents of the autogenerated .cmx we construct.
constexpr char kAutogenCmxContents[] = R"({
      "program": {
        "binary": ""
      },
      "runner": "fuchsia-pkg://fuchsia.com/environment_delegating_runner#meta/environment_delegating_runner.cmx"
    })";

// Relative path within the autogenerated package directory to the manifest.
constexpr char kAutogenPkgDirManifestPath[] = "autogenerated_manifest.cmx";
// Path to the autogenerated cmx file of the intercepted component. $0 is
// substituted for `kAutogenPkgDirManifestPath` at runtime.
constexpr char kAutogenCmxPathSubstitutePattern[] =
    "fuchsia-pkg://example.com/fake_pkg#$0";

}  // namespace

ComponentInterceptor::ComponentInterceptor(
    fuchsia::sys::LoaderPtr fallback_loader, async_dispatcher_t* dispatcher)
    : fallback_loader_(std::move(fallback_loader)),
      dispatcher_(dispatcher),
      pkg_dir_(fbl::AdoptRef(new fs::PseudoDir())),
      vfs_(dispatcher == nullptr ? async_get_default_dispatcher()
                                 : dispatcher_) {
  pkg_dir_->AddEntry(kAutogenPkgDirManifestPath,
                     fbl::AdoptRef(new fs::BufferedPseudoFile(
                         &ComponentInterceptor::CmxReadHandler)));

  loader_svc_ = fbl::AdoptRef(new fs::Service([this](zx::channel h) mutable {
    loader_bindings_.AddBinding(
        this, fidl::InterfaceRequest<fuchsia::sys::Loader>(std::move(h)),
        dispatcher_);
    return ZX_OK;
  }));
}

ComponentInterceptor::~ComponentInterceptor() = default;

// static
ComponentInterceptor ComponentInterceptor::CreateWithEnvironmentLoader(
    const fuchsia::sys::EnvironmentPtr& env, async_dispatcher_t* dispatcher) {
  // The fallback loader comes from |parent_env|.
  fuchsia::sys::LoaderPtr fallback_loader;
  fuchsia::sys::ServiceProviderPtr sp;
  env->GetServices(sp.NewRequest());
  sp->ConnectToService(fuchsia::sys::Loader::Name_,
                       fallback_loader.NewRequest().TakeChannel());

  return ComponentInterceptor(std::move(fallback_loader), dispatcher);
}

std::unique_ptr<EnvironmentServices>
ComponentInterceptor::MakeEnvironmentServices(
    const fuchsia::sys::EnvironmentPtr& parent_env) {
  auto env_services = EnvironmentServices::CreateWithCustomLoader(
      parent_env, loader_svc_, dispatcher_);

  FXL_CHECK(env_services->AddService(
                runner_bindings_.GetHandler(this, dispatcher_)) == ZX_OK)
      << "Could not initialize EnvironmentServices with custom runner.";

  return env_services;
}

void ComponentInterceptor::InterceptURL(std::string component_url,
                                        ComponentLaunchHandler handler) {
  FXL_DCHECK(handler) << "Must be a valid handler.";

  std::lock_guard<std::mutex> lock(whitelist_mu_);
  whitelist_urls_[component_url] = std::move(handler);
}

// static
zx_status_t ComponentInterceptor::CmxReadHandler(fbl::String* out) {
  *out = kAutogenCmxContents;
  return ZX_OK;
}

void ComponentInterceptor::LoadUrl(std::string url, LoadUrlCallback response) {
  {
    std::lock_guard<std::mutex> lock(whitelist_mu_);
    if (whitelist_urls_.find(url) == whitelist_urls_.end()) {
      fallback_loader_->LoadUrl(url, std::move(response));
      return;
    }
  }

  auto pkg = std::make_unique<fuchsia::sys::Package>();
  zx::channel server, client;
  FXL_CHECK(zx::channel::create(0, &server, &client) == ZX_OK);
  FXL_CHECK(vfs_.ServeDirectory(pkg_dir_, std::move(server)) == ZX_OK);
  pkg->directory = std::move(client);
  pkg->resolved_url = fxl::Substitute(kAutogenCmxPathSubstitutePattern,
                                      kAutogenPkgDirManifestPath);

  response(std::move(pkg));
  // The runner specified in the autogenerated manifest should forward its
  // requests back to us.
}

void ComponentInterceptor::StartComponent(
    fuchsia::sys::Package package, fuchsia::sys::StartupInfo startup_info,
    fidl::InterfaceRequest<fuchsia::sys::ComponentController> controller) {
  ComponentLaunchHandler handler;
  {
    std::lock_guard<std::mutex> lock(whitelist_mu_);
    auto it = whitelist_urls_.find(startup_info.launch_info.url);
    FXL_DCHECK(it != whitelist_urls_.end());

    // We keep a reference to |handler| and unlock the mutex, to enable the
    // handler to call InterceptURL().
    handler = it->second;
  }

  handler(std::move(startup_info), std::move(controller));
}

}  // namespace testing
}  // namespace component