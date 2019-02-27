// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Convenience types related to account auth states.

use fidl_fuchsia_auth::{AuthState, AuthStateSummary};
use crate::{FidlLocalAccountId, LocalAccountId};

/// Convenience type for the autogenerated FIDL AccountAuthState.
pub type FidlAccountAuthState = fidl_fuchsia_auth_account::AccountAuthState;

/// Wrapper type of an AccountAuthState which is cloneable.
#[derive(Clone)]
pub struct AccountAuthState {
    /// Identifies the account to which this auth state belongs
    pub account_id: LocalAccountId,
    // TODO(dnordstrom): Add other states
}

impl From<&AccountAuthState> for FidlAccountAuthState {
    fn from(account_auth_state: &AccountAuthState) -> FidlAccountAuthState {
        FidlAccountAuthState {
            account_id: FidlLocalAccountId::from(account_auth_state.account_id.clone()),
            auth_state: AuthState { summary: AuthStateSummary::Unknown },
        }
    }
}
