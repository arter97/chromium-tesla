// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMEOS_COMPONENTS_MAHI_PUBLIC_CPP_MAHI_MANAGER_H_
#define CHROMEOS_COMPONENTS_MAHI_PUBLIC_CPP_MAHI_MANAGER_H_

#include <stdint.h>

#include <string>
#include <vector>

#include "base/component_export.h"
#include "base/functional/callback_forward.h"
#include "base/memory/raw_ptr.h"
#include "base/unguessable_token.h"
#include "chromeos/crosapi/mojom/mahi.mojom.h"
#include "ui/gfx/image/image_skia.h"

class GURL;

namespace chromeos {

struct COMPONENT_EXPORT(MAHI_PUBLIC_CPP) MahiOutline {
  int id;
  std::u16string outline_content;

  bool operator==(const MahiOutline&) const;
};

// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
// List os possible response statuses for a Mahi request.
enum class COMPONENT_EXPORT(MAHI_PUBLIC_CPP) MahiResponseStatus {
  kSuccess = 0,
  kUnknownError = 1,
  kInappropriate = 2,
  kLowQuota = 3,
  kQuotaLimitHit = 4,
  kResourceExhausted = 5,
  kContentExtractionError = 6,
  kCantFindOutputData = 7,
  kRestrictedCountry = 8,
  kUnsupportedLanguage = 9,
  kMaxValue = kUnsupportedLanguage,
};

// An interface serves as the connection between mahi system and the UI.
class COMPONENT_EXPORT(MAHI_PUBLIC_CPP) MahiManager {
 public:
  MahiManager(const MahiManager&) = delete;
  MahiManager& operator=(const MahiManager&) = delete;

  virtual ~MahiManager();

  static MahiManager* Get();

  static bool IsSupportedWithCorrectFeatureKey();

  // Gets information about the content on the corresponding surface.
  virtual std::u16string GetContentTitle() = 0;
  virtual gfx::ImageSkia GetContentIcon() = 0;
  virtual GURL GetContentUrl() = 0;

  // Returns the quick summary of the current active content on the
  // corresponding surface.
  using MahiSummaryCallback =
      base::OnceCallback<void(std::u16string, MahiResponseStatus)>;
  virtual void GetSummary(MahiSummaryCallback callback) = 0;

  // Returns the outlines of the current active content on the corresponding
  // surface.
  using MahiOutlinesCallback =
      base::OnceCallback<void(std::vector<MahiOutline>, MahiResponseStatus)>;
  virtual void GetOutlines(MahiOutlinesCallback callback) = 0;

  // Goes to the content that is associated with `outline_id`.
  virtual void GoToOutlineContent(int outline_id) = 0;

  // Answers the provided `question`. `current_panel_content` is a boolean to
  // determine if the question is regarding the current content displayed on
  // the panel.
  using MahiAnswerQuestionCallback =
      base::OnceCallback<void(std::optional<std::u16string>,
                              MahiResponseStatus)>;
  virtual void AnswerQuestion(const std::u16string& question,
                              bool current_panel_content,
                              MahiAnswerQuestionCallback callback) = 0;

  // Gets suggested question for the content currently displayed in the panel.
  using MahiGetSuggestedQuestionCallback =
      base::OnceCallback<void(std::u16string, MahiResponseStatus)>;
  virtual void GetSuggestedQuestion(
      MahiGetSuggestedQuestionCallback callback) = 0;

  // Set page info of current focused page
  virtual void SetCurrentFocusedPageInfo(
      crosapi::mojom::MahiPageInfoPtr info) = 0;

  virtual void OnContextMenuClicked(
      crosapi::mojom::MahiContextMenuRequestPtr context_menu_request) = 0;

  // Opens the feedback dialog.
  virtual void OpenFeedbackDialog() = 0;

  // Check if the feature is enabled.
  virtual bool IsEnabled() = 0;

  // Called when a Media app PDF window is focused, to notify Mahi about the
  // refresh avaiablity.
  virtual void SetMediaAppPDFFocused() = 0;

  // Called when a Media app PDF window is closed. This allows Mahi to hide the
  // refresh banner targeted to it.
  virtual void MediaAppPDFClosed(
      const base::UnguessableToken media_app_client_id) {}

  // If current page info is associated to a Media app PDF window, returns its
  // client id.
  virtual std::optional<base::UnguessableToken> GetMediaAppPDFClientId() const;

 protected:
  MahiManager();
};

// A scoped object that set the global instance of
// `chromeos::MahiManager::Get()` to the provided object pointer. The real
// instance will be restored when this scoped object is destructed. This class
// is used in testing and mocking.
class COMPONENT_EXPORT(MAHI_PUBLIC_CPP) ScopedMahiManagerSetter {
 public:
  explicit ScopedMahiManagerSetter(MahiManager* manager);
  ScopedMahiManagerSetter(const ScopedMahiManagerSetter&) = delete;
  ScopedMahiManagerSetter& operator=(const ScopedMahiManagerSetter&) = delete;
  ~ScopedMahiManagerSetter();

 private:
  static ScopedMahiManagerSetter* instance_;

  raw_ptr<MahiManager> real_manager_instance_ = nullptr;
};

}  // namespace chromeos

#endif  // CHROMEOS_COMPONENTS_MAHI_PUBLIC_CPP_MAHI_MANAGER_H_
