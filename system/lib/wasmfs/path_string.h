// Copyright 2022 The Emscripten Authors.  All rights reserved.
// Emscripten is available under two separate licenses, the MIT license and the
// University of Illinois/NCSA Open Source License.  Both these licenses can be
// found in the LICENSE file.

#pragma once

#include <string>

#ifdef WASMFS_CASE_INSENSITIVE
#include <strings.h>
#endif

namespace wasmfs {

// Represents a file path component.
// It exists mainly to support case-insensitive paths.
class PathString {
public:
  using StringT = std::string;

  PathString() = default;
  PathString(const StringT& p) : path(p) {}
  PathString(const char* p) : path(p) {}

  const StringT& str() const { return path; }
  const StringT::value_type* c_str() const noexcept { return path.c_str(); }
  StringT::size_type size() const noexcept { return path.size(); }

  bool operator==(const PathString& other) const {
#ifdef WASMFS_CASE_INSENSITIVE
    return size() == other.size() &&
           strncasecmp(c_str(), other.c_str(), other.size()) == 0;
#else
    return str() == other.str();
#endif
  }

  bool operator!=(const PathString& other) const { return !(*this == other); }

  bool operator<(const PathString& other) const {
#ifdef WASMFS_CASE_INSENSITIVE
    return strcasecmp(c_str(), other.c_str()) < 0;
#else
    return str() < other.str();
#endif
  }

private:
  StringT path;
};

} // namespace wasmfs
