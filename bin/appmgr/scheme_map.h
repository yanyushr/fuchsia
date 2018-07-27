// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_APPMGR_SCHEME_MAP_H_
#define GARNET_BIN_APPMGR_SCHEME_MAP_H_

#include <string>
#include <unordered_map>

#include <lib/fxl/macros.h>
#include "garnet/lib/json/json_parser.h"

namespace component {

// Represents a mapping from file scheme to component type. Generated from
// scheme_map configuration file.
class SchemeMap {
 public:
  SchemeMap() = default;

  // Parses a schema map from a file.
  bool ParseFromFile(const std::string& file);

  bool HasError() const { return json_parser_.HasError(); }
  std::string error_str() const { return json_parser_.error_str(); }

  // Returns the launcher type for a given scheme, or "" if none.
  std::string LookUp(const std::string& scheme) const;

  // Returns the path for the scheme map config file.
  static std::string GetSchemeMapPath();

 private:
  std::unordered_map<std::string, std::string> internal_map_;
  json::JSONParser json_parser_;

  FXL_DISALLOW_COPY_AND_ASSIGN(SchemeMap);
};

}  // namespace component

#endif  // GARNET_BIN_APPMGR_SCHEME_MAP_H_
