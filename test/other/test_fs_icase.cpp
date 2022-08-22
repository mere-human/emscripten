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

std::vector<std::string> get_dir_files(const char* dname) {
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
  write_file("test.txt");

  struct stat st;
  assert(stat("test.txt", &st) == 0);
  assert(st.st_size == 4);

  assert(exists("test.TXT"));
  assert(exists("Test.Txt"));

  read_file("Test.txt");

  assert(rename("tesT.Txt", "test2.txt") == 0);
  assert(exists("test2.txt"));
  assert(exists("Test2.txt"));
  read_file("Test2.txt");

#ifdef WASMFS
  assert(unlink("TEST2.txt") == 0);
#else
  // bug in FS.unlink()
  assert(unlink("test2.txt") == 0);
#endif
  assert(!exists("TEST2.txt"));
  assert(!exists("test2.txt"));

  assert(mkdir("subdir", S_IRWXUGO) == 0);
  assert(exists("subdir"));

  write_file("SubDir/Test.txt");
  assert(exists("subdir/test.txt"));
  read_file("subdir/Test.txt");
  auto dir_files = get_dir_files("subdir");
  assert(dir_files.size() == 1);

  // File name and letter-case should be the same as at the moment of creation.
  assert(std::find(dir_files.begin(), dir_files.end(), "Test.txt") != dir_files.end());
  assert(std::find(dir_files.begin(), dir_files.end(), "test.txt") == dir_files.end());

#ifdef WASMFS
  assert(unlink("SUBDIR/TEST.TXT") == 0);
#else
  // bug in FS.unlink()
  assert(unlink("subdir/Test.txt") == 0);
#endif
  assert(!exists("subdir/test.txt"));
  assert(get_dir_files("subdir").size() == 0);

  assert(rmdir("Subdir") == 0);
  assert(!exists("subdir"));

  printf("ok\n");

  return 0;
}
