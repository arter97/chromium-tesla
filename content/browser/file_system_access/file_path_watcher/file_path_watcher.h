// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This module provides a way to monitor a file or directory for changes.

#ifndef CONTENT_BROWSER_FILE_SYSTEM_ACCESS_FILE_PATH_WATCHER_FILE_PATH_WATCHER_H_
#define CONTENT_BROWSER_FILE_SYSTEM_ACCESS_FILE_PATH_WATCHER_FILE_PATH_WATCHER_H_

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "base/containers/enum_set.h"
#include "base/files/file_path.h"
#include "base/functional/callback_forward.h"
#include "base/memory/scoped_refptr.h"
#include "base/sequence_checker.h"
#include "base/task/sequenced_task_runner.h"
#include "build/build_config.h"
#include "content/common/content_export.h"

namespace content {

// This class lets you register interest in changes on a FilePath.
// The callback will get called whenever the file or directory referenced by the
// FilePath is changed, including created or deleted. Due to limitations in the
// underlying OS APIs, FilePathWatcher has slightly different semantics on OS X
// than on Windows or Linux. FilePathWatcher on Linux and Windows will detect
// modifications to files in a watched directory. FilePathWatcher on Mac will
// detect the creation and deletion of files in a watched directory, but will
// not detect modifications to those files. See file_path_watcher_kqueue.cc for
// details.
//
// Must be destroyed on the sequence that invokes Watch().
class CONTENT_EXPORT FilePathWatcher {
 public:
  // Type of change which occurred on the affected. Note that this may differ
  // from the watched path, e.g. in the case of recursive watches.
  enum class ChangeType {
    kUnknown,  // One or more changes occurred at the path or its descendants.
    kCreated,
    kDeleted,
    kModified,  // Includes modifications to either file contents or attributes.
    kMoved
  };

  // Path type of the affected path. Note that this may differ from the watched
  // path, e.g. in the case of recursive watches.
  enum class FilePathType {
    kUnknown,  // The implementation could not determine the path type or does
               // not support path types.
    kDirectory,
    kFile,
  };

  // Information about the file system change. This information should be as
  // specific as the underlying platform allows. For example, when watching
  // directory foo/, creating file foo/bar.txt should be reported as a change
  // with a `kCreated` change type and a `kFile` path type rather than as a
  // modification to directory foo/. Due to limitations on some platforms, this
  // is not always possible. Callers should treat this information a strong
  // hint, but still be capable of handling events where this information is not
  // known given the limitations on some platforms.
  struct CONTENT_EXPORT ChangeInfo {
    ChangeInfo();
    ChangeInfo(FilePathType file_path_type,
               ChangeType change_type,
               base::FilePath modified_path);
    ChangeInfo(FilePathType file_path_type,
               ChangeType change_type,
               base::FilePath modified_path,
               std::optional<base::FilePath> moved_from_path);
    ~ChangeInfo();

    // Copyable and movable.
    ChangeInfo(const ChangeInfo&);
    ChangeInfo(ChangeInfo&&);
    ChangeInfo& operator=(const ChangeInfo&);
    ChangeInfo& operator=(ChangeInfo&&) noexcept;

    friend bool operator==(const ChangeInfo&, const ChangeInfo&) = default;

    FilePathType file_path_type = FilePathType::kUnknown;
    ChangeType change_type = ChangeType::kUnknown;

    // Modified path of the changed file or directory. It is provided as
    // best-effort, and may not exist if the underlying platform is unable to
    // find information.
    base::FilePath modified_path;

    // Previous path that the file or the directory has been moved from. This
    // field is only set for `ChangeType:kMoved`, and exists if the file or the
    // directory is moved within the watched scope. It is provided as
    // best-effort, and may not exist if the underlying platform is unable to
    // find information.
    std::optional<base::FilePath> moved_from_path;
  };

  // TODO(crbug.com/40260973): Rename this now that this class declares
  // other types of Types.
  enum class Type {
    // Indicates that the watcher should watch the given path and its
    // ancestors for changes. If the path does not exist, its ancestors will
    // be watched in anticipation of it appearing later. If the path names a
    // directory, changes within the directory are not watched.
    kNonRecursive,

    // Indicates that the watcher should watch the given path, its ancestors,
    // and its descendants for changes. If the path names a directory, changes
    // within the directory are watched.
    kRecursive,

#if BUILDFLAG(IS_APPLE)
    // Indicates that the watcher should watch the given path only (neither
    // ancestors nor descendants). The watch fails if the path does not exist.
    kTrivial,
#endif  // BUILDFLAG(IS_APPLE)
  };

  // WatchOptions are a generalization of |Type|. They are used in the new
  // PlatformDelegate::WatchWithOptions.
  struct WatchOptions {
    Type type = Type::kNonRecursive;

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_ANDROID) || \
    BUILDFLAG(IS_FUCHSIA) || BUILDFLAG(IS_WIN)
    // The callback will return the full path to a changed file instead of
    // the watched path supplied as |path| when Watch is called.
    // So the full path can be different from the watched path when a folder is
    // watched. In case of any error, it behaves as the original Watch.
    bool report_modified_path = false;
#endif  // BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) ||
        // BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_FUCHSIA) || BUILDFLAG(IS_WIN)
  };

  // Callback type for Watch(). |path| points to the file that was updated,
  // and |error| is true if the platform specific code detected an error. In
  // that case, the callback won't be invoked again.
  using Callback =
      base::RepeatingCallback<void(const base::FilePath& path, bool error)>;
  // Same as above, but includes more information about the change, if known.
  // TODO(crbug.com/40105284): Remove path from this callback.
  using CallbackWithChangeInfo = base::RepeatingCallback<
      void(const ChangeInfo&, const base::FilePath& path, bool error)>;

  // Used internally to encapsulate different members on different platforms.
  class PlatformDelegate {
   public:
    using Type = FilePathWatcher::Type;
    using WatchOptions = FilePathWatcher::WatchOptions;

    PlatformDelegate();
    PlatformDelegate(const PlatformDelegate&) = delete;
    PlatformDelegate& operator=(const PlatformDelegate&) = delete;
    virtual ~PlatformDelegate();

    // Start watching for the given |path| and notify |delegate| about changes.
    [[nodiscard]] virtual bool Watch(const base::FilePath& path,
                                     Type type,
                                     const Callback& callback) = 0;

    // A more general API which can deal with multiple options.
    [[nodiscard]] virtual bool WatchWithOptions(const base::FilePath& path,
                                                const WatchOptions& options,
                                                const Callback& callback);

    // Watches the specified `path` according to the given `options`.
    // `callback` is invoked for each subsequent modification, with a
    // `ChangeInfo` populated with the fields supported by the implementation.
    [[nodiscard]] virtual bool WatchWithChangeInfo(
        const base::FilePath& path,
        const WatchOptions& options,
        const CallbackWithChangeInfo& callback);

    // Stop watching. This is called from FilePathWatcher's dtor in order to
    // allow to shut down properly while the object is still alive.
    virtual void Cancel() = 0;

#if BUILDFLAG(IS_WIN)
    // Gets the Lock associated with the content::FilePathWatcher
    // implementation's Watch thread. Tests can use this to block that thread
    // and cause a buffer overflow.
    virtual base::Lock& GetWatchThreadLockForTest() = 0;
#endif

   protected:
    friend class FilePathWatcher;

    scoped_refptr<base::SequencedTaskRunner> task_runner() const {
      return task_runner_;
    }

    void set_task_runner(scoped_refptr<base::SequencedTaskRunner> runner) {
      task_runner_ = std::move(runner);
    }

    // Must be called before the PlatformDelegate is deleted.
    void set_cancelled() { cancelled_ = true; }

    bool is_cancelled() const { return cancelled_; }

   private:
    scoped_refptr<base::SequencedTaskRunner> task_runner_;
    bool cancelled_ = false;
  };

  FilePathWatcher();
  FilePathWatcher(const FilePathWatcher&) = delete;
  FilePathWatcher& operator=(const FilePathWatcher&) = delete;
  ~FilePathWatcher();

  // Returns true if the platform and OS version support recursive watches.
  static bool RecursiveWatchAvailable();

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
  // Whether there are outstanding inotify watches.
  static bool HasWatchesForTest();
#endif  // BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)

  // Starts watching |path| (and its descendants if |type| is kRecursive) for
  // changes. |callback| will be run on the caller's sequence to report such
  // changes. Returns true if the watch was started successfully and |callback|
  // may one day be run, or false in case of failure (e.g., a recursive watch on
  // platforms that do not support such).
  //
  // On POSIX, this must be called from a thread that supports
  // FileDescriptorWatcher.
  bool Watch(const base::FilePath& path, Type type, const Callback& callback);

  // A more general API which can deal with multiple options.
  bool WatchWithOptions(const base::FilePath& path,
                        const WatchOptions& options,
                        const Callback& callback);

  // Same as above, but `callback` includes more information about the change,
  // if known. On platforms for which change information is not supported,
  // `callback` is called with a dummy `ChangeInfo`.
  bool WatchWithChangeInfo(const base::FilePath& path,
                           const WatchOptions& options,
                           const CallbackWithChangeInfo& callback);

#if BUILDFLAG(IS_WIN)
  // Gets the Lock associated with the content::FilePathWatcher implementation's
  // Watch thread. Tests can use this to block that thread and cause a buffer
  // overflow.
  base::Lock& GetWatchThreadLockForTest();
#endif

 private:
  explicit FilePathWatcher(std::unique_ptr<PlatformDelegate> delegate);

  std::unique_ptr<PlatformDelegate> impl_;

  SEQUENCE_CHECKER(sequence_checker_);
};

}  // namespace content

#endif  // CONTENT_BROWSER_FILE_SYSTEM_ACCESS_FILE_PATH_WATCHER_FILE_PATH_WATCHER_H_
