// Copyright 2014 The Bazel Authors. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <map>
#include <memory>  // unique_ptr
#include <thread>  // NOLINT (to silence Google-internal linter)
#include <vector>

#include "src/main/cpp/util/file.h"
#include "src/main/cpp/util/file_platform.h"
#include "src/test/cpp/util/test_util.h"
#include "gtest/gtest.h"

namespace blaze_util {

using std::string;

TEST(FileTest, TestSingleThreadedPipe) {
  std::unique_ptr<IPipe> pipe(CreatePipe());
  char buffer[50] = {0};
  ASSERT_TRUE(pipe.get()->Send("hello", 5));
  ASSERT_EQ(3, pipe.get()->Receive(buffer, 3));
  ASSERT_TRUE(pipe.get()->Send(" world", 6));
  ASSERT_EQ(5, pipe.get()->Receive(buffer + 3, 5));
  ASSERT_EQ(3, pipe.get()->Receive(buffer + 8, 40));
  ASSERT_EQ(0, strncmp(buffer, "hello world", 11));
}

TEST(FileTest, TestMultiThreadedPipe) {
  std::unique_ptr<IPipe> pipe(CreatePipe());
  char buffer[50] = {0};
  std::thread writer_thread([&pipe]() {
    ASSERT_TRUE(pipe.get()->Send("hello", 5));
    ASSERT_TRUE(pipe.get()->Send(" world", 6));
  });

  // Wait for all data to be fully written to the pipe.
  writer_thread.join();

  ASSERT_EQ(3, pipe.get()->Receive(buffer, 3));
  ASSERT_EQ(5, pipe.get()->Receive(buffer + 3, 5));
  ASSERT_EQ(3, pipe.get()->Receive(buffer + 8, 40));
  ASSERT_EQ(0, strncmp(buffer, "hello world", 11));
}

TEST(FileTest, TestReadFile) {
  const char* tempdir = getenv("TEST_TMPDIR");
  ASSERT_NE(nullptr, tempdir);
  ASSERT_NE(0, tempdir[0]);

  std::string filename(JoinPath(tempdir, "test.readfile"));
  AutoFileStream fh(fopen(filename.c_str(), "wt"));
  EXPECT_TRUE(fh.IsOpen());
  ASSERT_EQ(11, fwrite("hello world", 1, 11, fh));
  fh.Close();

  std::string actual;
  ASSERT_TRUE(ReadFile(filename, &actual));
  ASSERT_EQ(std::string("hello world"), actual);

  ASSERT_TRUE(ReadFile(filename, &actual, 5));
  ASSERT_EQ(std::string("hello"), actual);

  ASSERT_TRUE(ReadFile("/dev/null", &actual, 42));
  ASSERT_EQ(std::string(""), actual);
}

TEST(FileTest, TestWriteFile) {
  const char* tempdir = getenv("TEST_TMPDIR");
  ASSERT_NE(nullptr, tempdir);
  ASSERT_NE(0, tempdir[0]);

  std::string filename(JoinPath(tempdir, "test.writefile"));

  ASSERT_TRUE(WriteFile("hello", 3, filename));

  char buf[6] = {0};
  AutoFileStream fh(fopen(filename.c_str(), "rt"));
  EXPECT_TRUE(fh.IsOpen());
  fflush(fh);
  ASSERT_EQ(3, fread(buf, 1, 5, fh));
  fh.Close();
  ASSERT_EQ(std::string(buf), std::string("hel"));

  ASSERT_TRUE(WriteFile("hello", 5, filename));
  fh = fopen(filename.c_str(), "rt");
  EXPECT_TRUE(fh.IsOpen());
  memset(buf, 0, 6);
  ASSERT_EQ(5, fread(buf, 1, 5, fh));
  fh.Close();
  ASSERT_EQ(std::string(buf), std::string("hello"));

  ASSERT_TRUE(WriteFile("hello", 5, "/dev/null"));
  ASSERT_EQ(0, remove(filename.c_str()));
}

TEST(FileTest, TestMtimeHandling) {
  const char* tempdir_cstr = getenv("TEST_TMPDIR");
  ASSERT_NE(tempdir_cstr, nullptr);
  ASSERT_NE(tempdir_cstr[0], 0);
  string tempdir(tempdir_cstr);

  std::unique_ptr<IFileMtime> mtime(CreateFileMtime());
  bool actual = false;
  ASSERT_TRUE(mtime.get()->GetIfInDistantFuture(tempdir, &actual));
  ASSERT_FALSE(actual);

  // Create a new file, assert its mtime is not in the future.
  string file(JoinPath(tempdir, "foo.txt"));
  ASSERT_TRUE(WriteFile("hello", 5, file));
  ASSERT_TRUE(mtime.get()->GetIfInDistantFuture(file, &actual));
  ASSERT_FALSE(actual);
  // Set the file's mtime to the future, assert that it's so.
  ASSERT_TRUE(mtime.get()->SetToDistantFuture(file));
  ASSERT_TRUE(mtime.get()->GetIfInDistantFuture(file, &actual));
  ASSERT_TRUE(actual);
  // Overwrite the file, resetting its mtime, assert that GetIfInDistantFuture
  // notices.
  ASSERT_TRUE(WriteFile("world", 5, file));
  ASSERT_TRUE(mtime.get()->GetIfInDistantFuture(file, &actual));
  ASSERT_FALSE(actual);
  // Set it to the future again so we can reset it using SetToNow.
  ASSERT_TRUE(mtime.get()->SetToDistantFuture(file));
  ASSERT_TRUE(mtime.get()->GetIfInDistantFuture(file, &actual));
  ASSERT_TRUE(actual);
  // Assert that SetToNow resets the timestamp.
  ASSERT_TRUE(mtime.get()->SetToNow(file));
  ASSERT_TRUE(mtime.get()->GetIfInDistantFuture(file, &actual));
  ASSERT_FALSE(actual);
  // Delete the file and assert that we can no longer set or query its mtime.
  ASSERT_TRUE(UnlinkPath(file));
  ASSERT_FALSE(mtime.get()->SetToNow(file));
  ASSERT_FALSE(mtime.get()->SetToDistantFuture(file));
  ASSERT_FALSE(mtime.get()->GetIfInDistantFuture(file, &actual));
}

TEST(FileTest, TestRenameDirectory) {
  const char* tempdir_cstr = getenv("TEST_TMPDIR");
  EXPECT_NE(tempdir_cstr, nullptr);
  EXPECT_NE(tempdir_cstr[0], 0);
  string tempdir(tempdir_cstr);

  string dir1(JoinPath(tempdir, "test_rename_dir/dir1"));
  string dir2(JoinPath(tempdir, "test_rename_dir/dir2"));
  EXPECT_TRUE(MakeDirectories(dir1, 0700));
  string file1(JoinPath(dir1, "file1.txt"));
  EXPECT_TRUE(WriteFile("hello", 5, file1));

  ASSERT_EQ(RenameDirectory(dir1, dir2), kRenameDirectorySuccess);
  ASSERT_EQ(RenameDirectory(dir1, dir2), kRenameDirectoryFailureOtherError);
  EXPECT_TRUE(MakeDirectories(dir1, 0700));
  EXPECT_TRUE(WriteFile("hello", 5, file1));
  ASSERT_EQ(RenameDirectory(dir2, dir1), kRenameDirectoryFailureNotEmpty);
}

class CollectingDirectoryEntryConsumer : public DirectoryEntryConsumer {
 public:
  CollectingDirectoryEntryConsumer(const string& _rootname)
      : rootname(_rootname) {}

  void Consume(const string& name, bool is_directory) override {
    // Strip the path prefix up to the `rootname` to ease testing on all
    // platforms.
    int index = name.rfind(rootname);
    string key = (index == string::npos) ? name : name.substr(index);
    // Replace backslashes with forward slashes (necessary on Windows only).
    std::replace(key.begin(), key.end(), '\\', '/');
    entries[key] = is_directory;
  }

  const string rootname;
  std::map<string, bool> entries;
};

TEST(FileTest, ForEachDirectoryEntryTest) {
  string tmpdir(getenv("TEST_TMPDIR"));
  EXPECT_FALSE(tmpdir.empty());
  // Create a directory structure:
  //   $TEST_TMPDIR/
  //      foo/
  //        bar/
  //          file3.txt
  //        file1.txt
  //        file2.txt
  string rootdir(JoinPath(tmpdir, "foo"));
  string file1(JoinPath(rootdir, "file1.txt"));
  string file2(JoinPath(rootdir, "file2.txt"));
  string subdir(JoinPath(rootdir, "bar"));
  string file3(JoinPath(subdir, "file3.txt"));

  EXPECT_TRUE(MakeDirectories(subdir, 0700));
  EXPECT_TRUE(WriteFile("hello", 5, file1));
  EXPECT_TRUE(WriteFile("hello", 5, file2));
  EXPECT_TRUE(WriteFile("hello", 5, file3));

  std::map<string, bool> expected;
  expected["foo/file1.txt"] = false;
  expected["foo/file2.txt"] = false;
  expected["foo/bar"] = true;

  CollectingDirectoryEntryConsumer consumer("foo");
  ForEachDirectoryEntry(rootdir, &consumer);
  ASSERT_EQ(consumer.entries, expected);
}

}  // namespace blaze_util
