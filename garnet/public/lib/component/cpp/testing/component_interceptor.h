// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_COMPONENT_CPP_TESTING_COMPONENT_INTERCEPTOR_H_
#define LIB_COMPONENT_CPP_TESTING_COMPONENT_INTERCEPTOR_H_

#include <mutex>
#include <string>

#include <fbl/ref_ptr.h>
#include <fs/pseudo-dir.h>
#include <fs/synchronous-vfs.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/component/cpp/testing/enclosing_environment.h>
#include <lib/fidl/cpp/binding_set.h>

namespace component {
namespace testing {

// ComponentInterceptor is a utility that helps users construct an
// EnvironmentService (to be used alongside EnclosingEnvironment) that is able
// to intercept and mock components launched under the EnclosingEnvironment.
//
// This class is thread-safe. Underlying FIDL communication is processed on the
// async dispatcher supplied to this class.
class ComponentInterceptor : fuchsia::sys::Loader, fuchsia::sys::Runner {
 public:
  // TODO(CF-608): Provide a utility which implements ComponentController and
  // gives the user a simpler front-end.
  using ComponentLaunchHandler = std::function<void(
      fuchsia::sys::StartupInfo,
      fidl::InterfaceRequest<fuchsia::sys::ComponentController>)>;

  ComponentInterceptor(fuchsia::sys::LoaderPtr fallback_loader,
                       async_dispatcher_t* dispatcher = nullptr);

  virtual ~ComponentInterceptor() override;

  // Constructs a fallback loader from the given |env|.
  static ComponentInterceptor CreateWithEnvironmentLoader(
      const fuchsia::sys::EnvironmentPtr& env,
      async_dispatcher_t* dispatcher = nullptr);

  // Creates an |EnvironmentServices| which contains custom Loader and
  // Runner services which intercept component launch URLs configured using
  // |InterceptURL|. Calls to |InterceptURL| are effective regardless of if
  // they're called before or after calls to this method.
  //
  // Restrictions:
  //  * Users must not override the fuchsia::sys::Loader and
  //    fuchsia::sys::Runner services.
  //  * An instance of |ComponentInterceptor| must outlive instances of
  //    vended |EnvironmentServices|
  std::unique_ptr<EnvironmentServices> MakeEnvironmentServices(
      const fuchsia::sys::EnvironmentPtr& env);

  // Intercepts |component_url| from being launched under this environment,
  // and calls the supplied |handler| to handle the runtime of this
  // component.
  void InterceptURL(std::string component_url, ComponentLaunchHandler handler);

 private:
  // Returns the contents of the autogenerated component manifest.
  //
  // |BufferedPseudoFile::ReadHandler|
  static zx_status_t CmxReadHandler(fbl::String* out);

  // Returns a faked fuchsia.sys.Package with a custom runner which forwards
  // the StartComponent request to environment's fuchsia::sys::Runner
  // service hosted by this object instance.
  //
  // |fuchsia::sys::Loader|
  void LoadUrl(std::string url, LoadUrlCallback response) override;

  // We arrive here if our fuchsia::sys::Loader sends a component launch to
  // the test harness runner component, which forwards it to here.
  //
  // |fuchsia::sys::Runner|
  void StartComponent(fuchsia::sys::Package package,
                      fuchsia::sys::StartupInfo startup_info,
                      fidl::InterfaceRequest<fuchsia::sys::ComponentController>
                          controller) override;

  // Ensures that calls to intercepting URLs remains thread-safe.
  std::mutex whitelist_mu_;

  std::map<std::string, ComponentLaunchHandler> whitelist_urls_
      __TA_GUARDED(whitelist_mu_);

  fuchsia::sys::LoaderPtr fallback_loader_;

  fbl::RefPtr<fs::Service> loader_svc_;
  fidl::BindingSet<fuchsia::sys::Loader> loader_bindings_;
  fidl::BindingSet<fuchsia::sys::Runner> runner_bindings_;

  async_dispatcher_t* dispatcher_;
  // Fake component package directory where we host our fake manifest.
  fbl::RefPtr<fs::PseudoDir> pkg_dir_;
  // VFS hook for |pkg_dir_|.
  fs::SynchronousVfs vfs_;

  std::unique_ptr<EnclosingEnvironment> env_;
};

}  // namespace testing
}  // namespace component

#endif  // LIB_COMPONENT_CPP_TESTING_COMPONENT_INTERCEPTOR_H_