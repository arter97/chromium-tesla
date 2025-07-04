// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/json/values_util.h"
#include "base/test/test_timeouts.h"
#include "build/buildflag.h"
#include "content/browser/web_contents/web_contents_impl.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/common/content_switches.h"
#include "content/public/test/back_forward_cache_util.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/content_browser_test.h"
#include "content/public/test/content_browser_test_utils.h"
#include "content/public/test/file_system_chooser_test_helpers.h"
#include "content/shell/browser/shell.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace content {

namespace {

#if !BUILDFLAG(IS_ANDROID) && !BUILDFLAG(IS_IOS) && !BUILDFLAG(IS_FUCHSIA)
constexpr int kBFCacheTestTimeoutMs = 3000;
#endif  // !BUILDFLAG(IS_ANDROID) && !BUILDFLAG(IS_IOS) &&
        // !BUILDFLAG(IS_FUCHSIA)

enum class TestFileSystemType {
  kBucket,
  kLocal,
};

}  // namespace

// Helpful macros to reduce the boilerplate script in the tests below.

// Resolves a promise with a serialized copy of the first `records` fired in the
// change callback.
//
// Notably, several FileSystemChangeRecord fields are not serializable, so tests
// which need to assert some behavior about the non-serializable fields may
// overwrite the `onChange` function.
#define CREATE_PROMISE_AND_RESOLVERS \
  "let promiseResolve, promiseReject; \
   let promise = new Promise(function(resolve, reject) { \
     promiseResolve = resolve; \
     promiseReject = reject; \
   }); \
   async function onChange(records, observer) { \
     let serializedRecords = records.map(record => { \
       let info = {}; \
       info.type = record.type; \
       info.relativePathComponents = record.relativePathComponents; \
       if (record.relativePathMovedFrom) { \
         info.relativePathMovedFrom = record.relativePathMovedFrom; \
       } \
       return info; \
     }); \
     promiseResolve(serializedRecords); \
   };"

#define GET_LOCAL_FILE "const [file] = await self.showOpenFilePicker();"
#define GET_BUCKET_FILE                                  \
  "const root = await navigator.storage.getDirectory();" \
  "const file = await root.getFileHandle('file', { create: true });"
#define GET_LOCAL_DIRECTORY "const dir = await self.showDirectoryPicker();"
#define GET_BUCKET_DIRECTORY \
  "const dir = await navigator.storage.getDirectory();"

#define GET_FILE(file_system_type)                            \
  +std::string(file_system_type == TestFileSystemType::kLocal \
                   ? GET_LOCAL_FILE                           \
                   : GET_BUCKET_FILE) +

#define GET_DIRECTORY(file_system_type)                       \
  +std::string(file_system_type == TestFileSystemType::kLocal \
                   ? GET_LOCAL_DIRECTORY                      \
                   : GET_BUCKET_DIRECTORY) +

#define START_OBSERVING_FILE(file_system_type)                \
  +std::string(file_system_type == TestFileSystemType::kLocal \
                   ? GET_LOCAL_FILE                           \
                   : GET_BUCKET_FILE) +                       \
      "const observer = new FileSystemObserver(onChange);  \
       await observer.observe(file);"

#define START_OBSERVING_DIRECTORY(file_system_type, recursive) \
  +std::string(file_system_type == TestFileSystemType::kLocal  \
                   ? GET_LOCAL_DIRECTORY                       \
                   : GET_BUCKET_DIRECTORY) +                   \
      JsReplace(                                               \
          "const observer = new FileSystemObserver(onChange);  \
           await observer.observe(dir, { recursive: $1 });",   \
          recursive) +

#define WRITE_TO_FILE \
  "const writable = await file.createWritable();  \
   await writable.write('blah');                  \
   await writable.close();"

#define SET_CHANGE_TIMEOUT     \
  +JsReplace(                  \
      "setTimeout(() => {      \
         promiseResolve([]);   \
       }, $1);                 \
       return await promise;", \
      base::Int64ToValue(TestTimeouts::action_timeout().InMilliseconds())) +

// TODO(crbug.com/341136316): Consider making these WPTs, and adding a
// lot more of them. For example:
//   - change types
//   - observing a handle without permission should fail
//   - changes should not be reported to swap files
//     (see https://crbug.com/321980149)
//   - changes should not be reported if permission to the handle is lost
//     (see https://crbug.com/321980366)
//   - moving an observed handle

class FileSystemAccessObserverBrowserTestBase : public ContentBrowserTest {
 public:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
#if BUILDFLAG(IS_WIN)
    // Convert path to long format to avoid mixing long and 8.3 formats in test.
    ASSERT_TRUE(temp_dir_.Set(base::MakeLongFilePath(temp_dir_.Take())));
#endif  // BUILDFLAG(IS_WIN)

    ASSERT_TRUE(embedded_test_server()->Start());
    test_url_ = embedded_test_server()->GetURL("/title1.html");

    ContentBrowserTest::SetUp();
  }

  void TearDown() override {
    ContentBrowserTest::TearDown();
    ASSERT_TRUE(temp_dir_.Delete());
    ui::SelectFileDialog::SetFactory(nullptr);
  }

  base::FilePath CreateFileToBePicked() {
    base::FilePath file_path;
    {
      base::ScopedAllowBlockingForTesting allow_blocking;
      EXPECT_TRUE(
          base::CreateTemporaryFileInDir(temp_dir_.GetPath(), &file_path));
      EXPECT_TRUE(base::WriteFile(file_path, "observe me"));
    }

    ui::SelectFileDialog::SetFactory(
        std::make_unique<FakeSelectFileDialogFactory>(
            std::vector<base::FilePath>{file_path}));
    EXPECT_TRUE(NavigateToURL(shell(), test_url_));
    return file_path;
  }

  base::FilePath CreateDirectoryToBePicked() {
    base::FilePath dir_path;
    {
      base::ScopedAllowBlockingForTesting allow_blocking;
      EXPECT_TRUE(base::CreateTemporaryDirInDir(
          temp_dir_.GetPath(), FILE_PATH_LITERAL("test"), &dir_path));
    }

    ui::SelectFileDialog::SetFactory(
        std::make_unique<FakeSelectFileDialogFactory>(
            std::vector<base::FilePath>{dir_path}));
    EXPECT_TRUE(NavigateToURL(shell(), test_url_));
    return dir_path;
  }

 protected:
  base::ScopedTempDir temp_dir_;
  GURL test_url_;
};

class FileSystemAccessObserverDefaultBrowserTest
    : public FileSystemAccessObserverBrowserTestBase {};

IN_PROC_BROWSER_TEST_F(FileSystemAccessObserverDefaultBrowserTest,
                       DisabledByDefault) {
  EXPECT_TRUE(NavigateToURL(shell(), test_url_));

  auto result =
      EvalJs(shell(),
             "(async () => {"
             "const observer = new FileSystemObserver(() => {}); })()");
  EXPECT_TRUE(result.error.find("not defined") != std::string::npos)
      << result.error;
}

class FileSystemAccessObserveWithFlagBrowserTest
    : public FileSystemAccessObserverBrowserTestBase {
 public:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    // Enable the flag to use the FileSystemObserver interface.
    command_line->AppendSwitchASCII(switches::kEnableBlinkFeatures,
                                    "FileSystemObserver");
  }
};

IN_PROC_BROWSER_TEST_F(FileSystemAccessObserveWithFlagBrowserTest,
                       CreateObserver) {
  EXPECT_TRUE(NavigateToURL(shell(), test_url_));

  EXPECT_TRUE(
      ExecJs(shell(),
             "(async () => {"
             "const observer = new FileSystemObserver(() => {}); })()"));
}

IN_PROC_BROWSER_TEST_F(FileSystemAccessObserveWithFlagBrowserTest,
                       NothingToUnobserve) {
  EXPECT_TRUE(NavigateToURL(shell(), test_url_));

  // Calling unobserve() without a corresponding observe() should be a no-op.
  EXPECT_TRUE(ExecJs(shell(),
                     "(async () => {"
                     "const observer = new FileSystemObserver(() => {});"
                     "const root = await navigator.storage.getDirectory();"
                     "observer.unobserve(root); })()"));
}

IN_PROC_BROWSER_TEST_F(FileSystemAccessObserveWithFlagBrowserTest,
                       NothingToDisconnect) {
  EXPECT_TRUE(NavigateToURL(shell(), test_url_));

  // Calling disconnect() multiple times should be a no-op.
  EXPECT_TRUE(ExecJs(shell(),
                     "(async () => {"
                     "const observer = new FileSystemObserver(() => {});"
                     "observer.disconnect();"
                     "observer.disconnect(); })()"));
}

IN_PROC_BROWSER_TEST_F(FileSystemAccessObserveWithFlagBrowserTest,
                       UnobserveIsIdempotent) {
  EXPECT_TRUE(NavigateToURL(shell(), test_url_));

  // unobserve() may be called several times without crashing.
  EXPECT_TRUE(ExecJs(shell(),
                     "(async () => {"
                     "const observer = new FileSystemObserver(() => {});"
                     "const root = await navigator.storage.getDirectory();"
                     "observer.unobserve(root);"
                     "observer.unobserve(root);"
                     "observer.unobserve(root); })()"));
}

IN_PROC_BROWSER_TEST_F(FileSystemAccessObserveWithFlagBrowserTest,
                       DisconnectIsIdempotent) {
  EXPECT_TRUE(NavigateToURL(shell(), test_url_));

  // disconnect() may be called several times without crashing.
  EXPECT_TRUE(ExecJs(shell(),
                     "(async () => {"
                     "const observer = new FileSystemObserver(() => {});"
                     "observer.disconnect();"
                     "observer.disconnect();"
                     "observer.disconnect(); })()"));
}

IN_PROC_BROWSER_TEST_F(FileSystemAccessObserveWithFlagBrowserTest,
                       ObserveSyncAccessHandleWrite) {
  const GURL& test_url =
      embedded_test_server()->GetURL("/run_async_code_on_worker.html");
  NavigateToURLBlockUntilNavigationsComplete(shell(), test_url,
                                             /*number_of_navigations=*/1);
  const std::string script =
      // clang-format off
      R"(runOnWorkerAndWaitForResult(`)"
         CREATE_PROMISE_AND_RESOLVERS
         START_OBSERVING_FILE(TestFileSystemType::kBucket)
         "const accessHandle = await file.createSyncAccessHandle();"
         "const writeBuffer = new TextEncoder().encode('contents');"
         "accessHandle.write(writeBuffer);"
         SET_CHANGE_TIMEOUT
         "accessHandle.close();"
      R"(`);)";
  // clang-format on
  auto records = EvalJs(shell(), script).ExtractList();
  ASSERT_THAT(records.GetList(), testing::Not(testing::IsEmpty()));
  EXPECT_THAT(*records.GetList().front().GetDict().FindString("type"),
              testing::StrEq("modified"));
}

IN_PROC_BROWSER_TEST_F(FileSystemAccessObserveWithFlagBrowserTest,
                       ObserveSyncAccessHandleMultipleWrites) {
  const GURL& test_url =
      embedded_test_server()->GetURL("/run_async_code_on_worker.html");
  NavigateToURLBlockUntilNavigationsComplete(shell(), test_url,
                                             /*number_of_navigations=*/1);
  const std::string script =
      // clang-format off
      R"(runOnWorkerAndWaitForResult(`)"
         CREATE_PROMISE_AND_RESOLVERS
         "let timesCallbackFired = 0;"
         "let serializedRecords = [];"
         "async function onChange(records, observer) {"
         "  timesCallbackFired++;"
         "  for (const record of records) {"
         "    let info = {};"
         "    info.type = record.type;"
         "    serializedRecords.push(info);"
         "  }"
         "  if (timesCallbackFired >= 3) {"  // Expect three events.
         "    promiseResolve(serializedRecords);"
         "  }"
         "}"
         START_OBSERVING_FILE(TestFileSystemType::kBucket)
         "const accessHandle = await file.createSyncAccessHandle();"
         "const writeBuffer = new TextEncoder().encode('contents');"
         "accessHandle.write(writeBuffer);"
         "accessHandle.write(writeBuffer);"
         "accessHandle.write(writeBuffer);"  // Write thrice.
         SET_CHANGE_TIMEOUT
         "accessHandle.close();"
      R"(`);)";
  // clang-format on
  auto records = EvalJs(shell(), script).ExtractList();
  ASSERT_THAT(records.GetList(), testing::SizeIs(3));
  EXPECT_THAT(*records.GetList().front().GetDict().FindString("type"),
              testing::StrEq("modified"));
}

IN_PROC_BROWSER_TEST_F(FileSystemAccessObserveWithFlagBrowserTest,
                       ObserveSyncAccessHandleTruncate) {
  const GURL& test_url =
      embedded_test_server()->GetURL("/run_async_code_on_worker.html");
  NavigateToURLBlockUntilNavigationsComplete(shell(), test_url,
                                             /*number_of_navigations=*/1);
  const std::string script =
      // clang-format off
      R"(runOnWorkerAndWaitForResult(`)"
         CREATE_PROMISE_AND_RESOLVERS
         START_OBSERVING_FILE(TestFileSystemType::kBucket)
         "const accessHandle = await file.createSyncAccessHandle();"
         "accessHandle.truncate(20);"
         SET_CHANGE_TIMEOUT
         "accessHandle.close();"
      R"(`);)";
  // clang-format on
  auto records = EvalJs(shell(), script).ExtractList();
  ASSERT_THAT(records.GetList(), testing::Not(testing::IsEmpty()));
  EXPECT_THAT(*records.GetList().front().GetDict().FindString("type"),
              testing::StrEq("modified"));
}

IN_PROC_BROWSER_TEST_F(FileSystemAccessObserveWithFlagBrowserTest,
                       DoNotObserveSyncAccessHandleWithNoChanges) {
  const GURL& test_url =
      embedded_test_server()->GetURL("/run_async_code_on_worker.html");
  NavigateToURLBlockUntilNavigationsComplete(shell(), test_url,
                                             /*number_of_navigations=*/1);
  const std::string script =
      // clang-format off
      R"(runOnWorkerAndWaitForResult(`)"
         CREATE_PROMISE_AND_RESOLVERS
         START_OBSERVING_FILE(TestFileSystemType::kBucket)
         "const accessHandle = await file.createSyncAccessHandle();"
         "const readBuffer = new Uint8Array(24);"
         "accessHandle.read(readBuffer);"
         "accessHandle.flush();"
         "accessHandle.close();"
         SET_CHANGE_TIMEOUT
      R"(`);)";
  // clang-format on
  auto records = EvalJs(shell(), script).ExtractList();
  EXPECT_THAT(records.GetList(), testing::IsEmpty());
}

class FileSystemAccessObserverBrowserTest
    : public FileSystemAccessObserverBrowserTestBase,
      public testing::WithParamInterface<TestFileSystemType> {
 public:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    // Enable experimental web platform features to enable read/write access.
    command_line->AppendSwitch(
        switches::kEnableExperimentalWebPlatformFeatures);
  }

  TestFileSystemType GetTestFileSystemType() const { return GetParam(); }

  bool SupportsReportingModifiedPath() const {
    if (GetTestFileSystemType() == TestFileSystemType::kBucket) {
      return true;
    }

    // TODO(crbug.com/321980270): Some platforms do not support reporting the
    // modified path.
#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_WIN)
    return true;
#else
    return false;
#endif  // BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_WIN)
  }

  bool SupportsChangeInfo() const {
    // TODO(crbug.com/321980270): Reporting change info and the modified path
    // are both only supported on inotify, for now.
    return SupportsReportingModifiedPath();
  }
};

// `base::FilePatchWatcher` is not implemented on Fuchsia. See
// https://crbug.com/851641. Instead, just check that attempting to observe
// a handle does not crash.
#if BUILDFLAG(IS_FUCHSIA)
IN_PROC_BROWSER_TEST_F(FileSystemAccessObserveWithFlagBrowserTest,
                       ObservingLocalFileIsNotSupportedOnFuchsia) {
  base::FilePath file_path = CreateFileToBePicked();

  const std::string script =
      // clang-format off
      "(async () => {"
         CREATE_PROMISE_AND_RESOLVERS
         START_OBSERVING_FILE(TestFileSystemType::kLocal)
         WRITE_TO_FILE
         SET_CHANGE_TIMEOUT
      "})()";
  // clang-format on
  auto result = EvalJs(shell(), script);
  EXPECT_TRUE(result.error.find("did not support") != std::string::npos)
      << result.error;
}
#endif  // BUILDFLAG(IS_FUCHSIA)

IN_PROC_BROWSER_TEST_P(FileSystemAccessObserverBrowserTest, CreateObserver) {
  EXPECT_TRUE(NavigateToURL(shell(), test_url_));

  EXPECT_TRUE(
      ExecJs(shell(),
             "(async () => {"
             "const observer = new FileSystemObserver(() => {}); })()"));
}

IN_PROC_BROWSER_TEST_P(FileSystemAccessObserverBrowserTest, ObserveFile) {
  base::FilePath file_path = CreateFileToBePicked();

  const std::string script =
      // clang-format off
      "(async () => {"
         CREATE_PROMISE_AND_RESOLVERS
         START_OBSERVING_FILE(GetTestFileSystemType())
         WRITE_TO_FILE
         SET_CHANGE_TIMEOUT
      "})()";
  // clang-format on
  auto records = EvalJs(shell(), script).ExtractList();
  EXPECT_THAT(records.GetList(), testing::Not(testing::IsEmpty()));
}

IN_PROC_BROWSER_TEST_P(FileSystemAccessObserverBrowserTest, ObserveFileRename) {
  base::FilePath file_path = CreateFileToBePicked();

  const std::string script =
      // clang-format off
      "(async () => {"
         CREATE_PROMISE_AND_RESOLVERS
         START_OBSERVING_FILE(GetTestFileSystemType())
         "await file.move('newName.txt');"
         SET_CHANGE_TIMEOUT
      "})()";
  // clang-format on
  auto records = EvalJs(shell(), script).ExtractList();
  EXPECT_THAT(records.GetList(), testing::Not(testing::IsEmpty()));
  // The `relativePathComponents` should be an empty array, since the change
  // occurred on the path corresponding to the handle passed to `observe()`.
  EXPECT_THAT(
      *records.GetList().front().GetDict().FindList("relativePathComponents"),
      testing::IsEmpty());
  // Similarly, optional `relativePathMovedFrom` is not specified, since the
  // change occurred on the path corresponding to the handle passed to
  // `observe()`.
  EXPECT_FALSE(
      records.GetList().front().GetDict().FindList("relativePathMovedFrom"));
}

IN_PROC_BROWSER_TEST_P(FileSystemAccessObserverBrowserTest, ObserveDirectory) {
  base::FilePath dir_path = CreateDirectoryToBePicked();

  const std::string script =
      // clang-format off
      "(async () => {"
         CREATE_PROMISE_AND_RESOLVERS
         START_OBSERVING_DIRECTORY(GetTestFileSystemType(), /*recursive=*/false)
         "await dir.getFileHandle('newFile.txt', { create: true });"
         SET_CHANGE_TIMEOUT
      "})()";
  // clang-format on
  auto records = EvalJs(shell(), script).ExtractList();
  EXPECT_THAT(records.GetList(), testing::Not(testing::IsEmpty()));
}

IN_PROC_BROWSER_TEST_P(FileSystemAccessObserverBrowserTest,
                       ObserveDirectoryRecursively) {
  base::FilePath dir_path = CreateDirectoryToBePicked();

  // Set up the directory structure.
  const std::string pre_script =
      // clang-format off
      "(async () => {"
         GET_DIRECTORY(GetTestFileSystemType())
         "const s1 = await dir.getDirectoryHandle('sub1', { create: true });"
         "await s1.getDirectoryHandle('sub2', { create: true });"
      "})()";
  // clang-format on
  ASSERT_TRUE(ExecJs(shell(), pre_script));

  // The creation of 'newFile.txt' should trigger a change record.
  const std::string script =
      // clang-format off
      "(async () => {"
         CREATE_PROMISE_AND_RESOLVERS
         START_OBSERVING_DIRECTORY(GetTestFileSystemType(), /*recursive=*/true)
         "const subDir1 = await dir.getDirectoryHandle('sub1');"
         "const subDir2 = await subDir1.getDirectoryHandle('sub2');"
         // Creating the file should trigger an event.
         "await subDir2.getFileHandle('newFile.txt', { create: true });"
         SET_CHANGE_TIMEOUT
      "})()";
  // clang-format on
  auto records = EvalJs(shell(), script).ExtractList();
  EXPECT_THAT(records.GetList(), testing::Not(testing::IsEmpty()));
}

IN_PROC_BROWSER_TEST_P(FileSystemAccessObserverBrowserTest,
                       ObserveThenUnobserve) {
  base::FilePath file_path = CreateFileToBePicked();

  // Calling unobserve() on an active observation should not crash.
  const std::string script =
      // clang-format off
      "(async () => {"
         CREATE_PROMISE_AND_RESOLVERS
         START_OBSERVING_FILE(GetTestFileSystemType())
         "observer.unobserve(file);"
      "})()";
  // clang-format on
  EXPECT_TRUE(ExecJs(shell(), script));
}

IN_PROC_BROWSER_TEST_P(FileSystemAccessObserverBrowserTest,
                       ObserveThenUnobserveUnrelated) {
  base::FilePath file_path = CreateFileToBePicked();

  // Calling unobserve() with a handle unrelated to any active observations
  // should not crash.
  const std::string script =
      // clang-format off
      "(async () => {"
         CREATE_PROMISE_AND_RESOLVERS
         START_OBSERVING_FILE(GetTestFileSystemType())
         "const r = await navigator.storage.getDirectory();"
         "const otherFile = await r.getFileHandle('other', { create: true });"
         "observer.unobserve(otherFile);"
      "})()";
  // clang-format on
  EXPECT_TRUE(ExecJs(shell(), script));
}

IN_PROC_BROWSER_TEST_P(FileSystemAccessObserverBrowserTest,
                       NoChangesAfterUnobserve) {
  base::FilePath file_path = CreateFileToBePicked();

  const std::string script =
      // clang-format off
      "(async () => {"
         CREATE_PROMISE_AND_RESOLVERS
         START_OBSERVING_FILE(GetTestFileSystemType())
         "observer.unobserve(file);"
         WRITE_TO_FILE
         SET_CHANGE_TIMEOUT
      "})()";
  // clang-format on
  auto records = EvalJs(shell(), script).ExtractList();
  EXPECT_THAT(records.GetList(), testing::IsEmpty());
}

IN_PROC_BROWSER_TEST_P(FileSystemAccessObserverBrowserTest,
                       ObserveThenDisconnect) {
  base::FilePath file_path = CreateFileToBePicked();

  // Calling disconnect() with active observations should not crash.
  const std::string script =
      // clang-format off
      "(async () => {"
         CREATE_PROMISE_AND_RESOLVERS
         START_OBSERVING_FILE(GetTestFileSystemType())
         "observer.disconnect();"
      "})()";
  // clang-format on
  EXPECT_TRUE(ExecJs(shell(), script));
}

IN_PROC_BROWSER_TEST_P(FileSystemAccessObserverBrowserTest,
                       NoChangesAfterDisconnect) {
  base::FilePath file_path = CreateFileToBePicked();

  const std::string script =
      // clang-format off
      "(async () => {"
         CREATE_PROMISE_AND_RESOLVERS
         START_OBSERVING_FILE(GetTestFileSystemType())
         "observer.disconnect();"
         WRITE_TO_FILE
         SET_CHANGE_TIMEOUT
      "})()";
  // clang-format on
  auto records = EvalJs(shell(), script).ExtractList();
  EXPECT_THAT(records.GetList(), testing::IsEmpty());
}

// TODO(crbug.com/321980469): Add a ReObserveAfterUnobserve test once the
// unobserve() method is no longer racy. See https://crrev.com/c/4814709.
IN_PROC_BROWSER_TEST_P(FileSystemAccessObserverBrowserTest,
                       ReObserveAfterDisconnect) {
  base::FilePath file_path = CreateFileToBePicked();

  // We should see changes after re-observing the file.
  const std::string script =
      // clang-format off
      "(async () => {"
         CREATE_PROMISE_AND_RESOLVERS
         START_OBSERVING_FILE(GetTestFileSystemType())
         "observer.disconnect();"
         "await observer.observe(file);"
         WRITE_TO_FILE
         SET_CHANGE_TIMEOUT
      "})()";
  // clang-format on
  auto records = EvalJs(shell(), script).ExtractList();
  EXPECT_THAT(records.GetList(), testing::Not(testing::IsEmpty()));
}

// TODO(crbug.com/343961295): Windows reports two events when a swap file is
// closed: a "disappear" for the target file being overwritten, and a "move" for
// the swap file being moved to the target file.
#if !BUILDFLAG(IS_WIN)
IN_PROC_BROWSER_TEST_P(FileSystemAccessObserverBrowserTest,
                       ObserveFileReportsType) {
  base::FilePath file_path = CreateFileToBePicked();

  const std::string script =
      // clang-format off
      "(async () => {"
         CREATE_PROMISE_AND_RESOLVERS
         START_OBSERVING_FILE(GetTestFileSystemType())
         WRITE_TO_FILE
         SET_CHANGE_TIMEOUT
      "})()";
  // clang-format on
  auto records = EvalJs(shell(), script).ExtractList();
  ASSERT_THAT(records.GetList(), testing::Not(testing::IsEmpty()));
  // TODO(crbug.com/321980270): Support change types for the local file system
  // on more platforms.
  //
  // TODO(crbug.com/340584120): Consider reporting a consistent change type when
  // writing to a file via a WritableFileStream. On the local file system,
  // changes are naively considered "created" events because the swap file is
  // moved over the target file. Meanwhile, the BucketFS intentionally reports
  // the move as a modification if the move overwrote an existing file.
  const std::string expected_change_type =
      SupportsChangeInfo()
          ? (GetTestFileSystemType() == TestFileSystemType::kBucket
                 ? "modified"
                 : "appeared")
          : "unknown";
  EXPECT_THAT(*records.GetList().front().GetDict().FindString("type"),
              testing::StrEq(expected_change_type));
}
#endif  // !BUILDFLAG(IS_WIN)

IN_PROC_BROWSER_TEST_P(FileSystemAccessObserverBrowserTest,
                       ObserveFileReportsCorrectHandle) {
  base::FilePath file_path = CreateFileToBePicked();

  // The `changedHandle` should the same as `root`, which is the same as the
  // handle passed to `observe()` (in this case, `file`).
  const std::string script =
      // clang-format off
      "(async () => {"
         CREATE_PROMISE_AND_RESOLVERS
         "async function onChange(records, observer) {"
         "  const record = records[0];"
         "  promiseResolve(await file.isSameEntry(record.root) &&"
         "                 await file.isSameEntry(record.changedHandle));"
         "};"
         START_OBSERVING_FILE(GetTestFileSystemType())
         WRITE_TO_FILE
         SET_CHANGE_TIMEOUT
      "})()";
  // clang-format on
  EXPECT_TRUE(EvalJs(shell(), script).ExtractBool());
}

IN_PROC_BROWSER_TEST_P(FileSystemAccessObserverBrowserTest,
                       ObserveFileReportsCorrectRelativePathComponents) {
  base::FilePath file_path = CreateFileToBePicked();

  const std::string script =
      // clang-format off
      "(async () => {"
         CREATE_PROMISE_AND_RESOLVERS
         START_OBSERVING_FILE(GetTestFileSystemType())
         WRITE_TO_FILE
         SET_CHANGE_TIMEOUT
      "})()";
  // clang-format on
  auto records = EvalJs(shell(), script).ExtractList();
  ASSERT_THAT(records.GetList(), testing::Not(testing::IsEmpty()));
  // The `relativePathComponents` should be an empty array, since the change
  // occurred on the path corresponding to the handle passed to `observe()`.
  EXPECT_THAT(
      *records.GetList().front().GetDict().FindList("relativePathComponents"),
      testing::IsEmpty());
}

IN_PROC_BROWSER_TEST_P(FileSystemAccessObserverBrowserTest,
                       ObserveDirectoryReportsCorrectHandle) {
  base::FilePath dir_path = CreateDirectoryToBePicked();

  // TODO(crbug.com/321980270): Some platforms do not report the modified path.
  // In these cases, `changedHandle` will always be the handle passed to
  // observe().
  const std::string changed_handle =
      SupportsReportingModifiedPath() ? "subDir" : "dir";

  const std::string script =
      // clang-format off
      "(async () => {"
         CREATE_PROMISE_AND_RESOLVERS
         "async function onChange(records, observer) {"
         "  const record = records[0];"
         "  promiseResolve(await dir.isSameEntry(record.root) &&"
         "await "+ changed_handle +".isSameEntry(record.changedHandle));"
         "};"
         GET_DIRECTORY(GetTestFileSystemType())
         // Create and declare `subDir` before starting the observation, to
         // ensure it's declared by the time the `onChange` callback is called.
         "const subDir = await dir.getDirectoryHandle('sub', { create: true });"
         "const observer = new FileSystemObserver(onChange);"
         "await observer.observe(dir, { recursive: false });;"
         "await subDir.remove();"
         SET_CHANGE_TIMEOUT
      "})()";
  // clang-format on
  EXPECT_TRUE(EvalJs(shell(), script).ExtractBool());
}

// There is no way to know the correct handle type on Windows in this scenario.
//
// Window's content::FilePathWatcher uses base::GetFileInfo to figure out the
// file path type. Since `fileInDir` is deleted, there is nothing to call
// base::GetFileInfo on.
#if !BUILDFLAG(IS_WIN)
IN_PROC_BROWSER_TEST_P(FileSystemAccessObserverBrowserTest,
                       ObserveDirectoryReportsCorrectHandleType) {
  base::FilePath dir_path = CreateDirectoryToBePicked();

  // The modified handle is a file, so the change record should contain a
  // FileSystemFileHandle.
  //
  // TODO(crbug.com/321980270): Some platforms do not report the modified path.
  // In these cases, `changedHandle` will always be the handle passed to
  // observe().
  const std::string changed_handle =
      SupportsReportingModifiedPath() ? "fileInDir" : "dir";

  const std::string script =
      // clang-format off
      "(async () => {"
         CREATE_PROMISE_AND_RESOLVERS
         "async function onChange(records, observer) {"
         "  const record = records[0];"
         "  promiseResolve(await dir.isSameEntry(record.root) &&"
         "await "+ changed_handle +".isSameEntry(record.changedHandle));"
         "};"
         GET_DIRECTORY(GetTestFileSystemType())
         // Create and declare `fileInDir` before starting the observation, to
         // ensure it's declared by the time the `onChange` callback is called.
         "const fileInDir = await dir.getFileHandle('file', { create: true });"
         "const observer = new FileSystemObserver(onChange);"
         "await observer.observe(dir, { recursive: false });;"
         "await fileInDir.remove();"
         SET_CHANGE_TIMEOUT
      "})()";
  // clang-format on
  EXPECT_TRUE(EvalJs(shell(), script).ExtractBool());
}
#endif  // !BUILDFLAG(IS_WIN)

IN_PROC_BROWSER_TEST_P(FileSystemAccessObserverBrowserTest,
                       ObserveDirectoryReportsCorrectRelativePathComponents) {
  base::FilePath dir_path = CreateDirectoryToBePicked();

  const std::string script =
      // clang-format off
      "(async () => {"
         CREATE_PROMISE_AND_RESOLVERS
         START_OBSERVING_DIRECTORY(GetTestFileSystemType(), /*recursive=*/false)
         "await dir.getDirectoryHandle('sub', { create: true });"
         SET_CHANGE_TIMEOUT
      "})()";
  // clang-format on
  auto records = EvalJs(shell(), script).ExtractList();
  ASSERT_THAT(records.GetList(), testing::Not(testing::IsEmpty()));
  const auto relative_path_component_matcher = testing::Conditional(
      SupportsReportingModifiedPath(), testing::SizeIs(1), testing::IsEmpty());
  EXPECT_THAT(
      *records.GetList().front().GetDict().FindList("relativePathComponents"),
      relative_path_component_matcher);
}

IN_PROC_BROWSER_TEST_P(FileSystemAccessObserverBrowserTest,
                       ObserveDirectoryReportsMoveChangeInfo) {
  base::FilePath dir_path = CreateDirectoryToBePicked();

  const std::string script =
      // clang-format off
      "(async () => {"
         CREATE_PROMISE_AND_RESOLVERS
         GET_DIRECTORY(GetTestFileSystemType())
         // Move dir/subdir/oldFile.txt to dir/subdir/newFile.txt while watching
         // dir/
         "const subdir = "
         "    await dir.getDirectoryHandle('subdir', { create: true });"
         "const oldFile = "
         "    await subdir.getFileHandle('oldFile.txt', { create: true });"
         "const observer = new FileSystemObserver(onChange);"
         "await observer.observe(dir, { recursive: true });"
         "await oldFile.move(subdir, 'newFile.txt');"
         SET_CHANGE_TIMEOUT
      "})()";
  // clang-format on
  auto records = EvalJs(shell(), script).ExtractList();
  ASSERT_THAT(records.GetList(), testing::Not(testing::IsEmpty()));
  auto& record_dict = records.GetList().front().GetDict();
  const std::string expected_change_type =
      SupportsChangeInfo() ? "moved" : "unknown";
  EXPECT_THAT(*record_dict.FindString("type"),
              testing::StrEq(expected_change_type));
  if (SupportsReportingModifiedPath()) {
    EXPECT_THAT(*record_dict.FindList("relativePathComponents"),
                testing::ElementsAre("subdir", "newFile.txt"));
    EXPECT_THAT(*record_dict.FindList("relativePathMovedFrom"),
                testing::ElementsAre("subdir", "oldFile.txt"));
  } else {
    EXPECT_THAT(*record_dict.FindList("relativePathComponents"),
                testing::IsEmpty());
    EXPECT_FALSE(record_dict.FindList("relativePathMovedFrom"));
  }
}

IN_PROC_BROWSER_TEST_P(FileSystemAccessObserverBrowserTest,
                       ObserveDirectoryReportsAppearedOnMoveIntoScope) {
  base::FilePath dir_path = CreateDirectoryToBePicked();

  const std::string script =
      // clang-format off
      "(async () => {"
         CREATE_PROMISE_AND_RESOLVERS
         GET_DIRECTORY(GetTestFileSystemType())
         // Move dir/oldFile.txt to dir/subdir/newFile.txt while watching
         // dir/subdir/
         "const subdir = "
         "    await dir.getDirectoryHandle('subdir', { create: true });"
         "const oldFile = "
         "    await dir.getFileHandle('oldFile.txt', { create: true });"
         "const observer = new FileSystemObserver(onChange);"
         "await observer.observe(subdir, { recursive: false });"
         "await oldFile.move(subdir, 'newFile.txt');"
         SET_CHANGE_TIMEOUT
      "})()";
  // clang-format on
  auto records = EvalJs(shell(), script).ExtractList();
  ASSERT_THAT(records.GetList(), testing::Not(testing::IsEmpty()));
  auto& record_dict = records.GetList().front().GetDict();
  const std::string expected_change_type =
      SupportsChangeInfo()
          ? (GetTestFileSystemType() == TestFileSystemType::kBucket
                 ? "moved"
                 : "appeared")
          : "unknown";
  EXPECT_THAT(*record_dict.FindString("type"),
              testing::StrEq(expected_change_type));
  if (SupportsReportingModifiedPath()) {
    EXPECT_THAT(*record_dict.FindList("relativePathComponents"),
                testing::ElementsAre("newFile.txt"));
  } else {
    EXPECT_THAT(*record_dict.FindList("relativePathComponents"),
                testing::IsEmpty());
  }
  EXPECT_FALSE(record_dict.FindList("relativePathMovedFrom"));
}

IN_PROC_BROWSER_TEST_P(FileSystemAccessObserverBrowserTest,
                       ObserveDirectoryReportsDisappearedOnMoveOutsideScope) {
  base::FilePath dir_path = CreateDirectoryToBePicked();

  const std::string script =
      // clang-format off
      "(async () => {"
         CREATE_PROMISE_AND_RESOLVERS
         GET_DIRECTORY(GetTestFileSystemType())
         // Move dir/subdir/oldFile.txt to dir/newFile.txt while watching
         // dir/subdir/
         "const subdir = "
         "    await dir.getDirectoryHandle('subdir', { create: true });"
         "const oldFile = "
         "    await subdir.getFileHandle('oldFile.txt', { create: true });"
         "const observer = new FileSystemObserver(onChange);"
         "await observer.observe(subdir, { recursive: false });"
         "await oldFile.move(dir, 'newFile.txt');"
         SET_CHANGE_TIMEOUT
      "})()";
  // clang-format on
  auto records = EvalJs(shell(), script).ExtractList();
  ASSERT_THAT(records.GetList(), testing::Not(testing::IsEmpty()));
  auto& record_dict = records.GetList().front().GetDict();
  // TODO(crbug.com/40105284): Consider reporting a consistent change
  // type when moving a file out of the watched scope. On the BucketFS,
  // changes are considered "disappeared" events while on local file system, it
  // is reported as "moved".
  const std::string expected_change_type =
      SupportsChangeInfo() ? "disappeared" : "unknown";
  EXPECT_THAT(*record_dict.FindString("type"),
              testing::StrEq(expected_change_type));
  if (SupportsReportingModifiedPath()) {
    // Moved-to path is out of the watched scope, so moved-from path is reported
    // as `relativePathComponents`.
    EXPECT_THAT(*record_dict.FindList("relativePathComponents"),
                testing::ElementsAre("oldFile.txt"));
  } else {
    EXPECT_THAT(*record_dict.FindList("relativePathComponents"),
                testing::IsEmpty());
  }
  EXPECT_FALSE(record_dict.FindList("relativePathMovedFrom"));
}

INSTANTIATE_TEST_SUITE_P(
    /* no prefix */,
    FileSystemAccessObserverBrowserTest,
#if BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_IOS) || BUILDFLAG(IS_FUCHSIA)
    // Local file system access - including the open*Picker() methods used here
    // - is not supported on Android or iOS. See https://crbug.com/1011535.
    // Meanwhile, `base::FilePatchWatcher` is not implemented on Fuchsia. See
    // https://crbug.com/851641.
    testing::Values(TestFileSystemType::kBucket)
#else
    testing::Values(TestFileSystemType::kBucket, TestFileSystemType::kLocal)
#endif  // BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_IOS) || BUILDFLAG(IS_FUCHSIA)
);

// Local file system access - including the open*Picker() methods used here
// - is not supported on Android or iOS. See https://crbug.com/1011535.
// Meanwhile, `FilePathWatcher` is not implemented on Fuchsia. See
// https://crbug.com/851641.
#if !BUILDFLAG(IS_ANDROID) && !BUILDFLAG(IS_IOS) && !BUILDFLAG(IS_FUCHSIA)
class FileSystemAccessObserverWithBFCacheBrowserTest
    : public FileSystemAccessObserverBrowserTestBase {
 public:
  FileSystemAccessObserverWithBFCacheBrowserTest() {
    InitBackForwardCacheFeature(&feature_list_for_back_forward_cache_,
                                /*enable_back_forward_cache=*/true);
  }

  void SetUpCommandLine(base::CommandLine* command_line) override {
    // Enable experimental web platform features to enable read/write access.
    command_line->AppendSwitch(
        switches::kEnableExperimentalWebPlatformFeatures);
  }

 protected:
  base::test::ScopedFeatureList feature_list_for_back_forward_cache_;
};

IN_PROC_BROWSER_TEST_F(FileSystemAccessObserverWithBFCacheBrowserTest,
                       ReceivesFileUpdatesAfterReturningFromBFCache) {
  base::FilePath file_path = CreateFileToBePicked();

  // Start observing the file.
  std::string script =
      // clang-format off
      "(async () => {"
         CREATE_PROMISE_AND_RESOLVERS
         "self.promise = promise;"
         "self.promiseResolve = promiseResolve;"
         "self.numRecords = 0;"
         "async function onChange(records, observer) {"
         "  numRecords += records.length;"
         "};"
         START_OBSERVING_FILE(TestFileSystemType::kLocal)
         "self.entry = file;"
         "self.obs = observer;"
      "})()";
  // clang-format on
  EXPECT_TRUE(ExecJs(shell(), script));

  RenderFrameHostWrapper initial_rfh(
      shell()->web_contents()->GetPrimaryMainFrame());

  // Navigate to another page and expect the previous RenderFrameHost to be
  // in the BFCache.
  ASSERT_TRUE(
      NavigateToURL(shell(), embedded_test_server()->GetURL("/title2.html")));
  EXPECT_TRUE(static_cast<RenderFrameHostImpl*>(initial_rfh.get())
                  ->IsInBackForwardCache());

  // Write to the file from the new origin and validate that change
  // notifications were sent.
  script =
      // clang-format off
      "(async () => {"
         CREATE_PROMISE_AND_RESOLVERS
        "numRecords = 0;"
         "async function onChange(records, observer) {"
         "  numRecords += records.length;"
         "};"
         START_OBSERVING_FILE(TestFileSystemType::kLocal)
         WRITE_TO_FILE
         "setTimeout(() => {promiseResolve(numRecords);}, $1);"
         "return await promise;"
      "})()";
  // clang-format on
  EXPECT_GE(EvalJs(shell(),
                   JsReplace(script, base::Int64ToValue(kBFCacheTestTimeoutMs)))
                .ExtractInt(),
            1);

  // Navigate back and restore `initial_rfh` as the primary main frame.
  ASSERT_TRUE(HistoryGoBack(shell()->web_contents()));
  EXPECT_EQ(initial_rfh.get(), shell()->web_contents()->GetPrimaryMainFrame());

  // We should have a single record from when the page was restored from
  // BFCache.
  script =
      // clang-format off
      "(async () => {"
         "setTimeout(() => {promiseResolve(self.numRecords);}, $1);"
         "return await self.promise;"
      "})()";
  // clang-format on
  EXPECT_EQ(EvalJs(shell(),
                   JsReplace(script, base::Int64ToValue(kBFCacheTestTimeoutMs)))
                .ExtractInt(),
            1);

  // Write to the file again. These changes should be reported.
  script =
      // clang-format off
      "(async () => {"
         CREATE_PROMISE_AND_RESOLVERS
         "const file = self.entry;"
         WRITE_TO_FILE
         "setTimeout(() => {promiseResolve(self.numRecords);}, $1);"
         "return await promise;"
      "})()";
  // clang-format on
  EXPECT_GT(EvalJs(shell(),
                   JsReplace(script, base::Int64ToValue(kBFCacheTestTimeoutMs)))
                .ExtractInt(),
            1);
}

IN_PROC_BROWSER_TEST_F(FileSystemAccessObserverWithBFCacheBrowserTest,
                       NotifyOnReturnFromBFCacheWhenFileUpdates) {
  base::FilePath file_path = CreateFileToBePicked();

  // Start observing the file.
  std::string script =
      // clang-format off
      "(async () => {"
         CREATE_PROMISE_AND_RESOLVERS
         "self.promise = promise;"
         "self.promiseResolve = promiseResolve;"
         START_OBSERVING_FILE(TestFileSystemType::kLocal)
         "self.obs = observer;"
      "})()";
  // clang-format on
  EXPECT_TRUE(ExecJs(shell(), script));

  RenderFrameHostWrapper initial_rfh(
      shell()->web_contents()->GetPrimaryMainFrame());

  // Navigate to another page and expect the previous RenderFrameHost to be
  // in the BFCache.
  ASSERT_TRUE(
      NavigateToURL(shell(), embedded_test_server()->GetURL("/title2.html")));
  EXPECT_TRUE(static_cast<RenderFrameHostImpl*>(initial_rfh.get())
                  ->IsInBackForwardCache());

  // Write to the file from the new origin and validate that change
  // notifications were sent.
  script =
      // clang-format off
      "(async () => {"
         CREATE_PROMISE_AND_RESOLVERS
         START_OBSERVING_FILE(TestFileSystemType::kLocal)
         WRITE_TO_FILE
         SET_CHANGE_TIMEOUT
      "})()";
  // clang-format on
  auto records = EvalJs(shell(), script).ExtractList();
  EXPECT_THAT(records.GetList(), testing::Not(testing::IsEmpty()));

  // Navigate back and restore `initial_rfh` as the primary main frame.
  ASSERT_TRUE(HistoryGoBack(shell()->web_contents()));
  EXPECT_EQ(initial_rfh.get(), shell()->web_contents()->GetPrimaryMainFrame());

  // We should have a single record from when the page was restored from
  // BFCache.
  script =
      // clang-format off
      "(async () => {"
         SET_CHANGE_TIMEOUT
      "})()";
  // clang-format on
  records = EvalJs(shell(), script).ExtractList();
  EXPECT_THAT(records.GetList(), testing::SizeIs(1));
  EXPECT_THAT(*records.GetList().front().GetDict().FindString("type"),
              testing::StrEq("unknown"));
}

IN_PROC_BROWSER_TEST_F(FileSystemAccessObserverWithBFCacheBrowserTest,
                       DoNotNotifyOnReturnFromBFCacheWhenNoFileUpdates) {
  base::FilePath file_path = CreateFileToBePicked();

  // Start observing the file.
  std::string script =
      // clang-format off
      "(async () => {"
         CREATE_PROMISE_AND_RESOLVERS
         "self.promise = promise;"
         "self.promiseResolve = promiseResolve;"
         START_OBSERVING_FILE(TestFileSystemType::kLocal)
         "self.obs = observer;"
      "})()";
  // clang-format on
  EXPECT_TRUE(ExecJs(shell(), script));

  RenderFrameHostWrapper initial_rfh(
      shell()->web_contents()->GetPrimaryMainFrame());

  // Navigate to another page and expect the previous RenderFrameHost to be
  // in the BFCache.
  ASSERT_TRUE(
      NavigateToURL(shell(), embedded_test_server()->GetURL("/title2.html")));
  EXPECT_TRUE(static_cast<RenderFrameHostImpl*>(initial_rfh.get())
                  ->IsInBackForwardCache());

  // Navigate back and restore `initial_rfh` as the primary main frame.
  ASSERT_TRUE(HistoryGoBack(shell()->web_contents()));
  EXPECT_EQ(initial_rfh.get(), shell()->web_contents()->GetPrimaryMainFrame());

  // We shouldn't have any records as no changes were made to the file while the
  // page was in BFCache.
  script =
      // clang-format off
      "(async () => {"
         SET_CHANGE_TIMEOUT
      "})()";
  // clang-format on
  auto records = EvalJs(shell(), script).ExtractList();
  ASSERT_THAT(records.GetList(), testing::IsEmpty());
}
#endif  // !BUILDFLAG(IS_ANDROID) && !BUILDFLAG(IS_IOS) &&
        // !BUILDFLAG(IS_FUCHSIA)

}  // namespace content
