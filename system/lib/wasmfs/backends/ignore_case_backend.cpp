// Copyright 2021 The Emscripten Authors.  All rights reserved.
// Emscripten is available under two separate licenses, the MIT license and the
// University of Illinois/NCSA Open Source License.  Both these licenses can be
// found in the LICENSE file.

// This file defines the Ignore Case Backend of the new file system.
// It is a virtual backend that normalizes all file paths to lower case.

#include "backend.h"
#include "file.h"
#include "memory_backend.h"
#include "wasmfs.h"
#include <map>

namespace {

std::string normalize(const std::string& name) {
  std::string result = name;
  for (auto& ch : result) {
    ch = std::tolower(ch);
  }
  return result;
}
} // namespace

namespace wasmfs {

// Normalizes case and then forwards calls to a directory from underlying
// backend `baseDirectory`.
class IgnoreCaseDirectory : public MemoryDirectory {
  using BaseClass = MemoryDirectory;
  std::shared_ptr<Directory> baseDirectory;
  std::vector<std::string> origNames; // same index as in entries

  void insertChild(const std::string& name,
                   std::shared_ptr<File> child) override {
    BaseClass::insertChild(normalize(name), child);
    origNames.push_back(name);
    assert(entries.size() == origNames.size());
  }

public:
  IgnoreCaseDirectory(std::shared_ptr<Directory> base, backend_t proxyBackend)
    : BaseClass(base->locked().getMode(), proxyBackend), baseDirectory(base) {}

  std::shared_ptr<File> getChild(const std::string& name) override {
    return BaseClass::getChild(normalize(name));
  }

  std::shared_ptr<DataFile> insertDataFile(const std::string& name,
                                           mode_t mode) override {
    auto name2 = normalize(name);
    auto baseDirLocked = baseDirectory->locked();
    auto child = baseDirLocked.insertDataFile(name2, mode);
    if (child) {
      insertChild(name, child);
      // Directory::Hanlde needs a parent
      child->locked().setParent(cast<Directory>());
    }
    return child;
  }

  std::shared_ptr<Directory> insertDirectory(const std::string& name,
                                             mode_t mode) override {
    auto name2 = normalize(name);
    auto baseDirLocked = baseDirectory->locked();
    if (!baseDirLocked.getParent())
      baseDirLocked.setParent(
        parent.lock()); // Directory::Hanlde needs a parent
    auto baseChild = baseDirLocked.insertDirectory(name2, mode);
    auto child = std::make_shared<IgnoreCaseDirectory>(baseChild, getBackend());
    insertChild(name, child);
    return child;
  }

  std::shared_ptr<Symlink> insertSymlink(const std::string& name,
                                         const std::string& target) override {
    auto name2 = normalize(name);
    auto child = baseDirectory->locked().insertSymlink(name2, target);
    if (child) {
      insertChild(name, child);
      // Directory::Hanlde needs a parent
      child->locked().setParent(cast<Directory>());
    }
    return child;
  }

  int insertMove(const std::string& name, std::shared_ptr<File> file) override {
    auto name2 = normalize(name);
    // Remove entry with the new name (if any) from this directory.
    if (auto err = removeChild(name))
      return err;
    auto oldParent = file->locked().getParent()->locked();
    auto oldName = oldParent.getName(file);
    auto oldName2 = normalize(oldName);
    // Move in underlying directory.
    if (auto err = baseDirectory->locked().insertMove(name2, file))
      return err;
    // Ensure old file was removed.
    if (auto err = oldParent.removeChild(oldName2))
      return err;
    // Cache file with the new name in this directory.
    insertChild(name, file);
    file->locked().setParent(cast<Directory>());
    return 0;
  }

  int removeChild(const std::string& name) override {
    auto name2 = normalize(name);
    ptrdiff_t pos = -1;
    if (auto it = findEntry(name2); it != entries.end())
      pos = std::distance(entries.begin(), it);
    if (auto err = BaseClass::removeChild(name2))
      return err;
    if (pos >= 0) {
      auto it = std::next(origNames.begin(), pos);
      origNames.erase(it);
    }
    assert(entries.size() == origNames.size());
    return baseDirectory->locked().removeChild(name2);
  }

  ssize_t getNumEntries() override {
    return baseDirectory->locked().getNumEntries();
  }

  Directory::MaybeEntries getEntries() override {
    auto xs = baseDirectory->locked().getEntries();
    if (xs.getError()) {
      return xs;
    }
    for (size_t i = 0; i != xs->size(); ++i) {
      auto& x = xs->at(i);
      if (auto it = findEntry(normalize(x.name)); it != entries.end()) {
        auto pos = std::distance(entries.begin(), it);
        x.name = origNames[pos];
      }
    }
    return xs;
  }

  std::string getName(std::shared_ptr<File> file) override {
    for (size_t i = 0; i != entries.size(); ++i) {
      if (entries[i].child == file)
        return origNames[i];
    }
    return {};
  }

  bool maintainsFileIdentity() override { return true; }
};

class IgnoreCaseBackend : public Backend {
  backend_t backend;

public:
  IgnoreCaseBackend(std::function<backend_t()> createBackend) {
    backend = createBackend();
  }

  std::shared_ptr<DataFile> createFile(mode_t mode) override {
    return backend->createFile(mode);
  }

  std::shared_ptr<Directory> createDirectory(mode_t mode) override {
    return std::make_shared<IgnoreCaseDirectory>(backend->createDirectory(mode),
                                                 this);
  }

  std::shared_ptr<Symlink> createSymlink(std::string target) override {
    return backend->createSymlink(normalize(target));
  }
};

// Create an ignore case backend by supplying another backend.
backend_t createIgnoreCaseBackend(std::function<backend_t()> createBackend) {
  return wasmFS.addBackend(std::make_unique<IgnoreCaseBackend>(createBackend));
}

extern "C" {
// C API for creating ignore case backend.
backend_t wasmfs_create_icase_backend(backend_constructor_t create_backend,
                                      void* arg) {
  return createIgnoreCaseBackend(
    [create_backend, arg]() { return create_backend(arg); });
}
}

} // namespace wasmfs
