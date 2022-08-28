// Copyright 2022 The Emscripten Authors.  All rights reserved.
// Emscripten is available under two separate licenses, the MIT license and the
// University of Illinois/NCSA Open Source License.  Both these licenses can be
// found in the LICENSE file.

#pragma once

#include <string>

#ifdef WASMFS_CASE_INSENSITIVE
#include <cctype>
#endif

namespace wasmfs {

// Represents a file path component.
// It exists mainly to support case-insensitive paths.
class PathString {
public:
  using StringT = std::string;

  PathString() = default;
  PathString(const StringT& p) : path(p) {
#ifdef WASMFS_CASE_INSENSITIVE
    pathNormalized.reserve(p.size());
    for (auto ch : p) {
      pathNormalized.push_back(std::tolower(ch));
    }
#endif
  }
  PathString(const char* p) : PathString(StringT{p}) {}

  const StringT& publicName() const { return path; }

  const StringT& internalName() const {
#ifdef WASMFS_CASE_INSENSITIVE
    return pathNormalized;
#else
    return path;
#endif
  }

private:
  StringT path; // Contains an entity name in original letter case. When in case
                // insensitive mode, it's used for case preservation.
#ifdef WASMFS_CASE_INSENSITIVE
  StringT pathNormalized; // Contains enity name for internal usage:
                          // searching, passing to backends, etc.
#endif
};

} // namespace wasmfs
