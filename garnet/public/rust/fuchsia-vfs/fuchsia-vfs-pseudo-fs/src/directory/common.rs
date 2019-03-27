// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Common utilities used by several directory implementations.

use {
    crate::directory::entry::EntryInfo,
    byteorder::{LittleEndian, WriteBytesExt},
    fidl_fuchsia_io::{
        MAX_FILENAME, OPEN_FLAG_APPEND, OPEN_FLAG_CREATE, OPEN_FLAG_CREATE_IF_ABSENT,
        OPEN_FLAG_DESCRIBE, OPEN_FLAG_DIRECTORY, OPEN_FLAG_NODE_REFERENCE, OPEN_FLAG_TRUNCATE,
        OPEN_RIGHT_ADMIN, OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE,
    },
    fuchsia_zircon::Status,
    std::{io::Write, mem::size_of},
};

/// Compares flags provided for a new connection with the flags of a parent connection.  Returns
/// adjusted flags for the new connection (cleaning up some ambiguities) or a fidl Status error, in
/// case new new connection flags are not permitting the connection to be opened.
pub fn new_connection_validate_flags(parent_flags: u32, mut flags: u32) -> Result<u32, Status> {
    if flags & OPEN_FLAG_NODE_REFERENCE != 0 {
        flags &= !OPEN_FLAG_NODE_REFERENCE;
        flags &= OPEN_FLAG_DIRECTORY | OPEN_FLAG_DESCRIBE;
    }

    let allowed_flags = OPEN_RIGHT_READABLE | OPEN_FLAG_DIRECTORY | OPEN_FLAG_DESCRIBE;

    let prohibited_flags =
        OPEN_FLAG_CREATE | OPEN_FLAG_CREATE_IF_ABSENT | OPEN_FLAG_TRUNCATE | OPEN_FLAG_APPEND;

    if flags & OPEN_RIGHT_READABLE != 0 && parent_flags & OPEN_RIGHT_READABLE == 0 {
        return Err(Status::ACCESS_DENIED);
    }

    // Pseudo directories do not allow modifications or mounting, at this point.
    if flags & OPEN_RIGHT_WRITABLE != 0 || flags & OPEN_RIGHT_ADMIN != 0 {
        return Err(Status::ACCESS_DENIED);
    }

    if flags & prohibited_flags != 0 {
        return Err(Status::INVALID_ARGS);
    }

    if flags & !allowed_flags != 0 {
        return Err(Status::NOT_SUPPORTED);
    }

    Ok(flags)
}

/// A helper to generate binary encodings for the ReadDirents response.  This function will append
/// an entry description as specified by `entry` and `name` to the `buf`, and would return `true`.
/// In case this would cause the buffer size to exceed `max_bytes`, the buffer is then left
/// untouched and a `false` value is returned.
pub fn encode_dirent(buf: &mut Vec<u8>, max_bytes: u64, entry: &EntryInfo, name: &str) -> bool {
    let header_size = size_of::<u64>() + size_of::<u8>() + size_of::<u8>();

    assert_eq_size!(u64, usize);

    if buf.len() + header_size + name.len() > max_bytes as usize {
        return false;
    }

    assert!(
        name.len() < MAX_FILENAME as usize,
        "Entry names are expected to be shorter than MAX_FILENAME ({}) bytes.\n\
         Got entry: '{}'\n\
         Length: {} bytes",
        MAX_FILENAME,
        name,
        name.len()
    );

    assert!(
        MAX_FILENAME <= u8::max_value() as u64,
        "Expecting to be able to store MAX_FILENAME ({}) in one byte.",
        MAX_FILENAME
    );

    buf.write_u64::<LittleEndian>(entry.inode())
        .expect("out should be an in memory buffer that grows as needed");
    buf.write_u8(name.len() as u8).expect("out should be an in memory buffer that grows as needed");
    buf.write_u8(entry.type_()).expect("out should be an in memory buffer that grows as needed");
    buf.write(name.as_ref()).expect("out should be an in memory buffer that grows as needed");

    true
}