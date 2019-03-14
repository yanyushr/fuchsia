// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]
#![feature(async_await, await_macro, futures_api)]

#[test]
fn add_interface_address_not_found() {
    let mut executor = fuchsia_async::Executor::new().expect("failed to create an executor");
    let stack =
        fuchsia_component::client::connect_to_service::<fidl_fuchsia_net_stack::StackMarker>()
            .expect("failed to connect to stack");
    let () = executor.run_singlethreaded(
        async {
            let interfaces = await!(stack.list_interfaces()).expect("failed to list interfaces");
            let max_id = interfaces.iter().map(|interface| interface.id).max().unwrap_or(0);
            let mut interface_address = fidl_fuchsia_net_stack::InterfaceAddress {
                ip_address: fidl_fuchsia_net::IpAddress::Ipv4(fidl_fuchsia_net::IPv4Address {
                    addr: [0, 0, 0, 0],
                }),
                prefix_len: 0,
            };
            let error = await!(stack.add_interface_address(max_id + 1, &mut interface_address,))
                .expect("failed to add interface address")
                .expect("failed to get add interface address response");
            assert_eq!(
                error.as_ref(),
                &fidl_fuchsia_net_stack::Error {
                    type_: fidl_fuchsia_net_stack::ErrorType::NotFound
                }
            )
        },
    );
}