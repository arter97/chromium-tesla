// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/picker/views/picker_strings.h"

#include <string>

#include "ash/picker/model/picker_search_results_section.h"
#include "ash/picker/views/picker_category_type.h"
#include "ash/public/cpp/picker/picker_category.h"
#include "ash/strings/grit/ash_strings.h"
#include "base/notreached.h"
#include "build/branding_buildflags.h"
#include "chromeos/strings/grit/chromeos_strings.h"
#include "ui/base/l10n/l10n_util.h"

#if BUILDFLAG(GOOGLE_CHROME_BRANDING)
#include "chromeos/ash/resources/internal/strings/grit/ash_internal_strings.h"
#endif  // BUILDFLAG(GOOGLE_CHROME_BRANDING)

namespace ash {

std::u16string GetLabelForPickerCategory(PickerCategory category) {
  switch (category) {
    case PickerCategory::kEditorWrite:
#if BUILDFLAG(GOOGLE_CHROME_BRANDING)
      return l10n_util::GetStringUTF16(IDS_EDITOR_MENU_WRITE_CARD_TITLE);
#else
      return u"";
#endif  // BUILDFLAG(GOOGLE_CHROME_BRANDING)
    case PickerCategory::kEditorRewrite:
#if BUILDFLAG(GOOGLE_CHROME_BRANDING)
      return l10n_util::GetStringUTF16(IDS_EDITOR_MENU_REWRITE_CARD_TITLE);
#else
      return u"";
#endif  // BUILDFLAG(GOOGLE_CHROME_BRANDING)
    case PickerCategory::kLinks:
      return l10n_util::GetStringUTF16(IDS_PICKER_LINKS_CATEGORY_LABEL);
    case PickerCategory::kExpressions:
      return l10n_util::GetStringUTF16(IDS_PICKER_EXPRESSIONS_CATEGORY_LABEL);
    case PickerCategory::kClipboard:
      return l10n_util::GetStringUTF16(IDS_PICKER_CLIPBOARD_CATEGORY_LABEL);
    case PickerCategory::kDriveFiles:
      return l10n_util::GetStringUTF16(IDS_PICKER_DRIVE_FILES_CATEGORY_LABEL);
    case PickerCategory::kLocalFiles:
      return l10n_util::GetStringUTF16(IDS_PICKER_LOCAL_FILES_CATEGORY_LABEL);
    case PickerCategory::kDatesTimes:
      return l10n_util::GetStringUTF16(IDS_PICKER_DATES_TIMES_CATEGORY_LABEL);
    case PickerCategory::kUnitsMaths:
      return l10n_util::GetStringUTF16(IDS_PICKER_UNITS_MATHS_CATEGORY_LABEL);
    case PickerCategory::kUpperCase:
      return l10n_util::GetStringUTF16(IDS_PICKER_UPPER_CASE_CATEGORY_LABEL);
    case PickerCategory::kLowerCase:
      return l10n_util::GetStringUTF16(IDS_PICKER_LOWER_CASE_CATEGORY_LABEL);
    case PickerCategory::kSentenceCase:
      return l10n_util::GetStringUTF16(IDS_PICKER_SENTENCE_CASE_CATEGORY_LABEL);
    case PickerCategory::kTitleCase:
      return l10n_util::GetStringUTF16(IDS_PICKER_TITLE_CASE_CATEGORY_LABEL);
    case PickerCategory::kCapsOn:
      return l10n_util::GetStringUTF16(IDS_PICKER_CAPS_ON_CATEGORY_LABEL);
    case PickerCategory::kCapsOff:
      return l10n_util::GetStringUTF16(IDS_PICKER_CAPS_OFF_CATEGORY_LABEL);
  }
}

std::u16string GetSearchFieldPlaceholderTextForPickerCategory(
    PickerCategory category) {
  switch (category) {
    case PickerCategory::kLinks:
      return l10n_util::GetStringUTF16(
          IDS_PICKER_LINKS_CATEGORY_SEARCH_FIELD_PLACEHOLDER_TEXT);
    case PickerCategory::kClipboard:
      return l10n_util::GetStringUTF16(
          IDS_PICKER_CLIPBOARD_CATEGORY_SEARCH_FIELD_PLACEHOLDER_TEXT);
    case PickerCategory::kDriveFiles:
      return l10n_util::GetStringUTF16(
          IDS_PICKER_DRIVE_FILES_CATEGORY_SEARCH_FIELD_PLACEHOLDER_TEXT);
    case PickerCategory::kLocalFiles:
      return l10n_util::GetStringUTF16(
          IDS_PICKER_LOCAL_FILES_CATEGORY_SEARCH_FIELD_PLACEHOLDER_TEXT);
    case PickerCategory::kDatesTimes:
      return l10n_util::GetStringUTF16(
          IDS_PICKER_DATES_TIMES_CATEGORY_SEARCH_FIELD_PLACEHOLDER_TEXT);
    case PickerCategory::kUnitsMaths:
      return l10n_util::GetStringUTF16(
          IDS_PICKER_UNITS_MATHS_CATEGORY_SEARCH_FIELD_PLACEHOLDER_TEXT);
    case PickerCategory::kEditorWrite:
    case PickerCategory::kEditorRewrite:
    case PickerCategory::kExpressions:
    case PickerCategory::kUpperCase:
    case PickerCategory::kLowerCase:
    case PickerCategory::kSentenceCase:
    case PickerCategory::kTitleCase:
    case PickerCategory::kCapsOn:
    case PickerCategory::kCapsOff:
      NOTREACHED_NORETURN();
  }
}

std::u16string GetSectionTitleForPickerCategoryType(
    PickerCategoryType category_type) {
  switch (category_type) {
    case PickerCategoryType::kEditorWrite:
#if BUILDFLAG(GOOGLE_CHROME_BRANDING)
      return l10n_util::GetStringUTF16(
          IDS_PICKER_EDITOR_WRITE_CATEGORY_TYPE_SECTION_TITLE);
#else
      return u"";
#endif  // BUILDFLAG(GOOGLE_CHROME_BRANDING)
    case PickerCategoryType::kEditorRewrite:
#if BUILDFLAG(GOOGLE_CHROME_BRANDING)
      return l10n_util::GetStringUTF16(
          IDS_PICKER_EDITOR_REWRITE_CATEGORY_TYPE_SECTION_TITLE);
#else
      return u"";
#endif  // BUILDFLAG(GOOGLE_CHROME_BRANDING)
    case PickerCategoryType::kGeneral:
      return l10n_util::GetStringUTF16(
          IDS_PICKER_GENERAL_CATEGORY_TYPE_SECTION_TITLE);
    case PickerCategoryType::kCalculations:
      return l10n_util::GetStringUTF16(
          IDS_PICKER_CALCULATIONS_CATEGORY_TYPE_SECTION_TITLE);
    case PickerCategoryType::kCaseTransformations:
      return l10n_util::GetStringUTF16(
          IDS_PICKER_CASE_TRANSFORMATIONS_CATEGORY_TYPE_SECTION_TITLE);
    case PickerCategoryType::kNone:
      return u"";
  }
}

std::u16string GetSectionTitleForPickerSectionType(
    PickerSectionType section_type) {
  // TODO: b/325870358 - Finalize strings and use a GRD file.
  switch (section_type) {
    case PickerSectionType::kNone:
      return u"";
    case PickerSectionType::kCategories:
      return u"Matching categories";
    case PickerSectionType::kSuggestions:
      return u"Suggested";
    case PickerSectionType::kExpressions:
      return u"Matching expressions";
    case PickerSectionType::kLinks:
      return u"Matching links";
    case PickerSectionType::kFiles:
      return u"Matching files";
    case PickerSectionType::kDriveFiles:
      return u"Matching Google Drive files";
    case PickerSectionType::kGifs:
      return u"Other expressions";
    case PickerSectionType::kEditorWrite:
#if BUILDFLAG(GOOGLE_CHROME_BRANDING)
      return l10n_util::GetStringUTF16(
          IDS_PICKER_EDITOR_WRITE_CATEGORY_TYPE_SECTION_TITLE);
#else
      return u"";
#endif  // BUILDFLAG(GOOGLE_CHROME_BRANDING)
    case PickerSectionType::kEditorRewrite:
#if BUILDFLAG(GOOGLE_CHROME_BRANDING)
      return l10n_util::GetStringUTF16(
          IDS_PICKER_EDITOR_REWRITE_CATEGORY_TYPE_SECTION_TITLE);
#else
      return u"";
#endif  // BUILDFLAG(GOOGLE_CHROME_BRANDING)
  }
}

}  // namespace ash
