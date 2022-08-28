// Copyright 2021 The Emscripten Authors.  All rights reserved.
// Emscripten is available under two separate licenses, the MIT license and the
// University of Illinois/NCSA Open Source License.  Both these licenses can be
// found in the LICENSE file.
// This file defines the file object of the new file system.
// Current Status: Work in Progress.
// See https://github.com/emscripten-core/emscripten/issues/15041.

#include "file.h"
#include "wasmfs.h"
#include <emscripten/threading.h>

extern "C" {
size_t _wasmfs_get_preloaded_file_size(uint32_t index);

void _wasmfs_copy_preloaded_file_data(uint32_t index, uint8_t* data);
}

namespace wasmfs {

//
// DataFile
//

void DataFile::Handle::preloadFromJS(int index) {
  // TODO: Each Datafile type could have its own impl of file preloading.
  // Create a buffer with the required file size.
  std::vector<uint8_t> buffer(_wasmfs_get_preloaded_file_size(index));

  // Ensure that files are preloaded from the main thread.
  assert(emscripten_is_main_runtime_thread());

  // Load data into the in-memory buffer.
  _wasmfs_copy_preloaded_file_data(index, buffer.data());

  write((const uint8_t*)buffer.data(), buffer.size(), 0);
}

//
// Directory
//

void Directory::Handle::cacheChild(const PathString& name,
                                   std::shared_ptr<File> child,
                                   DCacheKind kind) {
  // Update the dcache if the backend hasn't opted out of using the dcache or if
  // this is a mount point, in which case it is not under the control of the
  // backend.
  if (kind == DCacheKind::Mount || !getDir()->maintainsFileIdentity()) {
    auto& dcache = getDir()->dcache;
    auto [_, inserted] = dcache.insert({name.internalName(),
                                        {kind,
                                        child,
  #ifdef WASMFS_CASE_INSENSITIVE
                                        name.publicName()
  #endif
                                        }});
    assert(inserted && "inserted child already existed!");
  }
  // Set the child's parent.
  assert(child->locked().getParent() == nullptr ||
         child->locked().getParent() == getDir());
  child->locked().setParent(getDir());
}

std::shared_ptr<File> Directory::Handle::getChild(const PathString& name) {
  // Unlinked directories must be empty, without even "." or ".."
  if (!getParent()) {
    return nullptr;
  }
  if (name.publicName() == ".") {
    return file;
  }
  if (name.publicName() == "..") {
    return getParent();
  }
  // Check whether the cache already contains this child.
  auto& dcache = getDir()->dcache;
  if (auto it = dcache.find(name.internalName()); it != dcache.end()) {
    return it->second.file;
  }
  // Otherwise check whether the backend contains an underlying file we don't
  // know about.
  auto child = getDir()->getChild(name.internalName());
  if (!child) {
    return nullptr;
  }
  cacheChild(name, child, DCacheKind::Normal);
  return child;
}

bool Directory::Handle::mountChild(const PathString& name,
                                   std::shared_ptr<File> child) {
  assert(child);
  // Cannot insert into an unlinked directory.
  if (!getParent()) {
    return false;
  }
  cacheChild(name, child, DCacheKind::Mount);
  return true;
}

std::shared_ptr<DataFile>
Directory::Handle::insertDataFile(const PathString& name, mode_t mode) {
  // Cannot insert into an unlinked directory.
  if (!getParent()) {
    return nullptr;
  }
  auto child = getDir()->insertDataFile(name.internalName(), mode);
  if (!child) {
    return nullptr;
  }
  cacheChild(name, child, DCacheKind::Normal);
  setMTime(time(NULL));
  return child;
}

std::shared_ptr<Directory>
Directory::Handle::insertDirectory(const PathString& name, mode_t mode) {
  // Cannot insert into an unlinked directory.
  if (!getParent()) {
    return nullptr;
  }
  auto child = getDir()->insertDirectory(name.internalName(), mode);
  if (!child) {
    return nullptr;
  }
  cacheChild(name, child, DCacheKind::Normal);
  setMTime(time(NULL));
  return child;
}

std::shared_ptr<Symlink>
Directory::Handle::insertSymlink(const PathString& name,
                                 const PathString& target) {
  // Cannot insert into an unlinked directory.
  if (!getParent()) {
    return nullptr;
  }
  auto child = getDir()->insertSymlink(name.internalName(), target.internalName());
  if (!child) {
    return nullptr;
  }
  cacheChild(name, child, DCacheKind::Normal);
  setMTime(time(NULL));
  return child;
}

// TODO: consider moving this to be `Backend::move` to avoid asymmetry between
// the source and destination directories and/or taking `Directory::Handle`
// arguments to prove that the directories have already been locked.
int Directory::Handle::insertMove(const PathString& name,
                                  std::shared_ptr<File> file) {
  // Cannot insert into an unlinked directory.
  if (!getParent()) {
    return -EPERM;
  }

  // Look up the file in its old parent's cache.
  auto oldParent = file->locked().getParent();
  auto& oldCache = oldParent->dcache;
  auto oldIt = std::find_if(oldCache.begin(), oldCache.end(), [&](auto& kv) {
    return kv.second.file == file;
  });

  // TODO: Handle moving mount points correctly by only updating caches without
  // involving the backend.

  // Attempt the move.
  if (auto err = getDir()->insertMove(name.internalName(), file)) {
    return err;
  }

  if (oldIt != oldCache.end()) {
    // Do the move and update the caches.
    auto [oldName, entry] = *oldIt;
    assert(oldName.size());
    // Update parent pointers and caches to reflect the successful move.
    oldCache.erase(oldIt);
    auto& newCache = getDir()->dcache;
    auto [it, inserted] = newCache.insert({name.internalName(), entry});
    if (!inserted) {
      // Update and overwrite the overwritten file.
      it->second.file->locked().setParent(nullptr);
      it->second = entry;
    }
    file->locked().setParent(getDir());
  } else {
    // This backend doesn't use the dcache.
    assert(getDir()->maintainsFileIdentity());
  }

  // TODO: Moving mount points probably shouldn't update the mtime.
  auto now = time(NULL);
  oldParent->locked().setMTime(now);
  setMTime(now);

  return 0;
}

bool Directory::Handle::removeChild(const PathString& name) {
  auto& dcache = getDir()->dcache;
  auto entry = dcache.find(name.internalName());
  // If this is a mount, we don't need to call into the backend.
  if (entry != dcache.end() && entry->second.kind == DCacheKind::Mount) {
    dcache.erase(entry);
    return true;
  }
  if (!getDir()->removeChild(name.internalName())) {
    return false;
  }
  if (entry != dcache.end()) {
    entry->second.file->locked().setParent(nullptr);
    dcache.erase(entry);
  }
  setMTime(time(NULL));
  return true;
}

std::string Directory::Handle::getName(std::shared_ptr<File> file) {
  if (getDir()->maintainsFileIdentity()) {
    return getDir()->getName(file);
  }
  auto& dcache = getDir()->dcache;
  for (auto it = dcache.begin(); it != dcache.end(); ++it) {
    if (it->second.file == file) {
#ifdef WASMFS_CASE_INSENSITIVE
      return it->second.originalName.empty() ? it->first
                                             : it->second.originalName;
#else
      return it->first;
#endif
    }
  }
  return "";
}

size_t Directory::Handle::getNumEntries() {
  size_t mounts = 0;
  auto& dcache = getDir()->dcache;
  for (auto it = dcache.begin(); it != dcache.end(); ++it) {
    if (it->second.kind == DCacheKind::Mount) {
      ++mounts;
    }
  }
  return getDir()->getNumEntries() + mounts;
}

std::vector<Directory::Entry> Directory::Handle::getEntries() {
  auto entries = getDir()->getEntries();
  auto& dcache = getDir()->dcache;
  for (auto it = dcache.begin(); it != dcache.end(); ++it) {
    auto& [name, entry] = *it;
    if (entry.kind == DCacheKind::Mount) {
      entries.push_back({name, entry.file->kind, entry.file->getIno()});
    }
  }

#ifdef WASMFS_CASE_INSENSITIVE
  // Restore the original name of the entries (case preservation).
  for (auto& e : entries) {
    auto it = dcache.find(e.name);
    if (it != dcache.end()) {
      const auto& f = it->second.file;
      if (e.ino == f->getIno() && e.kind == f->kind) {
        e.name = it->second.originalName;
      }
    }
  }
#endif

  return entries;
}

} // namespace wasmfs
