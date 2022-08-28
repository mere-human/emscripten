/*
 * Copyright 2022 The Emscripten Authors.  All rights reserved.
 * Emscripten is available under two separate licenses, the MIT license and the
 * University of Illinois/NCSA Open Source License.  Both these licenses can be
 * found in the LICENSE file.
 */

#include <assert.h>
#include <dirent.h>
#include <emscripten/emscripten.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string>
#include <vector>

// TODO: check different WasmFS backends.

void write_file(const char* fname) {
  FILE* fp = fopen(fname, "wt");
  assert(fp);
  char data[] = "test";
  printf("Write '%s' to '%s'\n", data, fname);
  assert(fputs(data, fp) >= 0);
  assert(fclose(fp) == 0);
}

void read_file(const char* fname) {
  FILE* fp = fopen(fname, "rt");
  assert(fp);
  char buffer[10] = {};
  assert(fgets(buffer, sizeof(buffer), fp) != NULL);
  printf("Read '%s' from '%s'\n", buffer, fname);
  assert(strcmp(buffer, "test") == 0);
  assert(fclose(fp) == 0);
}

int exists(const char* fname) {
  struct stat st = {};
  return stat(fname, &st) == 0 ? 1 : 0;
}

std::vector<std::string> readdir(const char* dname) {
  printf("Files in '%s': ", dname);
  std::vector<std::string> files;
  DIR* d = opendir("subdir");
  if (d) {
    struct dirent* dir;
    while ((dir = readdir(d)) != NULL) {
      if (dir->d_type == DT_REG) {
        files.emplace_back(dir->d_name);
        printf("%s ", dir->d_name);
      }
    }
    closedir(d);
  }
  printf("\n");
  return files;
}

int main() {
  // Create a file.
  write_file("test.txt");

  // Read and check the file.
  struct stat st;
  assert(stat("test.txt", &st) == 0);
  assert(st.st_size == 4);
  assert(exists("test.TXT"));
  assert(exists("Test.Txt"));
  read_file("Test.txt");

  // Rename the file.
  assert(rename("tesT.Txt", "test2.txt") == 0);
  assert(exists("test2.txt"));
  assert(exists("Test2.txt"));
  read_file("Test2.txt");

  // Delete the file.
#ifdef WASMFS
  assert(unlink("TEST2.txt") == 0);
#else
  // bug in FS.unlink()
  assert(unlink("test2.txt") == 0);
#endif
  assert(!exists("TEST2.txt"));
  assert(!exists("test2.txt"));

  // Create a directory.
  assert(mkdir("Subdir", S_IRWXUGO) == 0);
  assert(exists("Subdir"));
  assert(exists("subdir"));
  assert(mkdir("SUBDIR", S_IRWXUGO) != 0);
  assert(errno == EEXIST);

  // Create a file in the directory.
  write_file("SubDir/Test.txt");
  assert(exists("subdir/test.txt"));
  read_file("subdir/Test.txt");

  // Check directory contents and entries name.
  auto dir_files = readdir("subdir");
  assert(dir_files.size() == 1);
  assert(std::find(dir_files.begin(), dir_files.end(), "Test.txt") != dir_files.end());
  assert(std::find(dir_files.begin(), dir_files.end(), "test.txt") == dir_files.end());

  // Delete a file from a directory.
#ifdef WASMFS
  assert(unlink("SUBDIR/TEST.TXT") == 0);
#else
  // bug in FS.unlink()
  assert(unlink("subdir/Test.txt") == 0);
#endif
  assert(!exists("subdir/test.txt"));
  assert(readdir("subdir").size() == 0);

  // Check current directory name and case.
  assert(chdir("subdir") == 0);
  char buffer[256];
  printf("getcwd: %s\n", getcwd(buffer, sizeof(buffer)));
#ifdef WASMFS
  assert(std::string(buffer).ends_with("Subdir"));
#else
  // FS.lookupNode doesn't preserve case. But in theory it could.
  assert(std::string(buffer).ends_with("subdir"));
#endif
  assert(chdir("..") == 0);

  // Rename a directory.
  assert(rename("subdir", "Subdir2") == 0);
  assert(!exists("subdir"));
  assert(exists("subdir2"));

  // Delete a directory.
  assert(rmdir("SUBDIR2") == 0);
  assert(!exists("SUBDIR2"));
  assert(!exists("Subdir2"));

  printf("ok\n");

  return 0;
}
