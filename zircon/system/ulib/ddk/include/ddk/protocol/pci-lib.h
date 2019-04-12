// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DDK_PROTOCOL_PCI_LIB_H_
#define DDK_PROTOCOL_PCI_LIB_H_

#ifdef __cplusplus
#include <ddktl/protocol/pci.h>
#include <lib/mmio/mmio.h>

static inline zx_status_t PciMapBarBuffer(ddk::PciProtocolClient* pci, uint32_t bar_id,
                                          uint32_t cache_policy,
                                          std::optional<ddk::MmioBuffer>* mmio) {
    zx_pci_bar_t bar;
    zx_status_t st = pci->GetBar(bar_id, &bar);
    if (st != ZX_OK) {
        return st;
    }

    if (bar.type == ZX_PCI_BAR_TYPE_PIO || bar.handle == ZX_HANDLE_INVALID) {
        return ZX_ERR_WRONG_TYPE;
    }

    st = ddk::MmioBuffer::Create(0, bar.size, zx::vmo(bar.handle), cache_policy, mmio);
    if (st != ZX_OK) {
        zx_handle_close(bar.handle);
        return st;
    }

    return ZX_OK;
}

#endif

#include <ddk/protocol/pci.h>
#include <ddk/mmio-buffer.h>

__BEGIN_CDECLS

static inline zx_status_t pci_map_bar_buffer(const pci_protocol_t* pci, uint32_t bar_id,
                                      uint32_t cache_policy, mmio_buffer_t* buffer) {

    zx_pci_bar_t bar;

    zx_status_t status = pci->ops->get_bar(pci->ctx, bar_id, &bar);
    if (status != ZX_OK) {
        return status;
    }
    // TODO(cja): PIO may be mappable on non-x86 architectures
    if (bar.type == ZX_PCI_BAR_TYPE_PIO || bar.handle == ZX_HANDLE_INVALID) {
        return ZX_ERR_WRONG_TYPE;
    }
    return mmio_buffer_init(buffer, 0, bar.size, bar.handle, cache_policy);
}

__END_CDECLS

#endif  // DDK_PROTOCOL_PCI_LIB_H_
