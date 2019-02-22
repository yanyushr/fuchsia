// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/drivers/audio/virtual_audio/virtual_audio_control_impl.h"

#include <ddk/debug.h>
#include <lib/async/default.h>

#include "garnet/drivers/audio/virtual_audio/virtual_audio.h"

namespace virtual_audio {

// static
//
// Always called after any published children have been unbound.
// Shut down any async loop; make sure there is nothing in flight.
void VirtualAudioControlImpl::DdkUnbind(void* ctx) {
  ZX_DEBUG_ASSERT(ctx != nullptr);

  auto self = static_cast<VirtualAudioControlImpl*>(ctx);

  // By now, the stream lists should be empty.
  // Close/free any remaining control (or stream) bindings.
  self->ReleaseBindings();
}

// static
//
// Always called after DdkUnbind, which should guarantee that lists are emptied.
// Any last cleanup or logical consistency checks would be done here.
void VirtualAudioControlImpl::DdkRelease(void* ctx) {
  ZX_DEBUG_ASSERT(ctx != nullptr);

  // DevMgr has returned ownership of whatever we provided as driver ctx (our
  // VirtualAudioControlImpl). When this functions returns, this unique_ptr will
  // go out of scope, triggering ~VirtualAudioControlImpl.
  std::unique_ptr<VirtualAudioControlImpl> control_ptr(
      static_cast<VirtualAudioControlImpl*>(ctx));

  // By now, all our lists should be empty.
  ZX_DEBUG_ASSERT(control_ptr->bindings_.size() == 0);
  ZX_DEBUG_ASSERT(control_ptr->input_bindings_.size() == 0);
  ZX_DEBUG_ASSERT(control_ptr->output_bindings_.size() == 0);
}

// static
//
zx_status_t VirtualAudioControlImpl::DdkMessage(void* ctx, fidl_msg_t* msg,
                                                fidl_txn_t* txn) {
  ZX_DEBUG_ASSERT(ctx != nullptr);

  return fuchsia_virtualaudio_Forwarder_dispatch(ctx, txn, msg, &fidl_ops_);
}

// static
//
fuchsia_virtualaudio_Forwarder_ops_t VirtualAudioControlImpl::fidl_ops_ = {
    .SendControl =
        [](void* ctx, zx_handle_t control_request) {
          ZX_DEBUG_ASSERT(ctx != nullptr);
          return static_cast<VirtualAudioControlImpl*>(ctx)->SendControl(
              zx::channel(control_request));
        },
    .SendInput =
        [](void* ctx, zx_handle_t input_request) {
          ZX_DEBUG_ASSERT(ctx != nullptr);
          return static_cast<VirtualAudioControlImpl*>(ctx)->SendInput(
              zx::channel(input_request));
        },
    .SendOutput =
        [](void* ctx, zx_handle_t output_request) {
          ZX_DEBUG_ASSERT(ctx != nullptr);
          return static_cast<VirtualAudioControlImpl*>(ctx)->SendOutput(
              zx::channel(output_request));
        },
};

// A client connected to fuchsia.virtualaudio.Control hosted by the virtual
// audio service, which is forwarding the server-side binding to us.
zx_status_t VirtualAudioControlImpl::SendControl(
    zx::channel control_request_channel) {
  if (!control_request_channel.is_valid()) {
    zxlogf(TRACE, "%s: channel from request handle is invalid\n", __func__);
    return ZX_ERR_INVALID_ARGS;
  }

  // VirtualAudioControlImpl is a singleton so just save the binding in a list.
  // Note, using the default dispatcher means that we will be running on the
  // same that drives all of our peer devices in the /dev/test device host.
  // We should ensure there are no long VirtualAudioControl operations.
  bindings_.AddBinding(this,
                       fidl::InterfaceRequest<fuchsia::virtualaudio::Control>(
                           std::move(control_request_channel)),
                       async_get_default_dispatcher());

  return ZX_OK;
}

// A client connected to fuchsia.virtualaudio.Input hosted by the
// virtual audio service, which is forwarding the server-side binding to us.
zx_status_t VirtualAudioControlImpl::SendInput(
    zx::channel input_request_channel) {
  if (!input_request_channel.is_valid()) {
    zxlogf(TRACE, "%s: channel from request handle is invalid\n", __func__);
    return ZX_ERR_INVALID_ARGS;
  }

  // Create an VirtualAudioInputImpl for this binding; save it in our list.
  // Note, using the default dispatcher means that we will be running on the
  // same that drives all of our peer devices in the /dev/test device host.
  // We should be mindful of this if doing long VirtualAudioInput operations.
  input_bindings_.AddBinding(
      VirtualAudioInputImpl::Create(this),
      fidl::InterfaceRequest<fuchsia::virtualaudio::Input>(
          std::move(input_request_channel)),
      async_get_default_dispatcher());

  return ZX_OK;
}

zx_status_t VirtualAudioControlImpl::SendOutput(
    zx::channel output_request_channel) {
  if (!output_request_channel.is_valid()) {
    zxlogf(TRACE, "%s: channel from request handle is invalid\n", __func__);
    return ZX_ERR_INVALID_ARGS;
  }

  // Create a VirtualAudioOutputImpl for this binding; save it in our list.
  // Note, using the default dispatcher means that we will be running on the
  // same that drives all of our peer devices in the /dev/test device host.
  // We should be mindful of this if doing long VirtualAudioOutput operations.
  output_bindings_.AddBinding(
      VirtualAudioOutputImpl::Create(this),
      fidl::InterfaceRequest<fuchsia::virtualaudio::Output>(
          std::move(output_request_channel)),
      async_get_default_dispatcher());

  return ZX_OK;
}

// Reset any remaining bindings of Controls, Inputs and Outputs.
// This is called during Unbind, at which time child drivers should be gone (and
// input_bindings_ and output_bindings_ empty).
void VirtualAudioControlImpl::ReleaseBindings() {
  bindings_.CloseAll();
  input_bindings_.CloseAll();
  output_bindings_.CloseAll();

  zxlogf(TRACE,
         "%s for %p: now we have %lu/%lu/%lu control/input/output bindings\n",
         __func__, this, bindings_.size(), input_bindings_.size(),
         output_bindings_.size());
}

// Allow subsequent new stream creation -- but do not automatically reactivate
// any streams that may have been deactivated (removed) by the previous Disable.
// Upon construction, the default state of this object is Enabled.
void VirtualAudioControlImpl::Enable() { enabled_ = true; }

// Deactivate active streams and prevent subsequent new stream creation. Audio
// devices disappear from the dev tree (VirtualAudioStream objects are freed),
// but Input and Output channels remain open and can be reconfigured. Once
// Enable is called; they can be readded without losing configuration state.
void VirtualAudioControlImpl::Disable() {
  if (enabled_) {
    for (auto& binding : input_bindings_.bindings()) {
      auto stream = binding->impl()->stream();
      if (stream) {
        stream->DdkUnbind();
      }
    }
    for (auto& binding : output_bindings_.bindings()) {
      auto stream = binding->impl()->stream();
      if (stream) {
        stream->DdkUnbind();
      }
    }

    enabled_ = false;
  }
}

}  // namespace virtual_audio