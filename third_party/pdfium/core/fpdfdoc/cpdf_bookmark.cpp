// Copyright 2016 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original code copyright 2014 Foxit Software Inc. http://www.foxitsoftware.com

#include "core/fpdfdoc/cpdf_bookmark.h"

#include <utility>

#include "core/fpdfapi/parser/cpdf_array.h"
#include "core/fpdfapi/parser/cpdf_dictionary.h"
#include "core/fpdfapi/parser/cpdf_string.h"
#include "core/fxcrt/data_vector.h"
#include "core/fxge/dib/fx_dib.h"

CPDF_Bookmark::CPDF_Bookmark() = default;

CPDF_Bookmark::CPDF_Bookmark(const CPDF_Bookmark& that) = default;

CPDF_Bookmark::CPDF_Bookmark(RetainPtr<const CPDF_Dictionary> pDict)
    : m_pDict(std::move(pDict)) {}

CPDF_Bookmark::~CPDF_Bookmark() = default;

WideString CPDF_Bookmark::GetTitle() const {
  if (!m_pDict)
    return WideString();

  RetainPtr<const CPDF_String> pString =
      ToString(m_pDict->GetDirectObjectFor("Title"));
  if (!pString)
    return WideString();

  WideString title = pString->GetUnicodeText();
  size_t len = title.GetLength();
  if (!len)
    return WideString();

  DataVector<wchar_t> buf(len);
  for (size_t i = 0; i < len; i++) {
    wchar_t w = title[i];
    buf[i] = w > 0x20 ? w : 0x20;
  }
  return UNSAFE_TODO(WideString::Create(buf.data(), len));
}

CPDF_Dest CPDF_Bookmark::GetDest(CPDF_Document* pDocument) const {
  if (!m_pDict)
    return CPDF_Dest(nullptr);
  return CPDF_Dest::Create(pDocument, m_pDict->GetDirectObjectFor("Dest"));
}

CPDF_Action CPDF_Bookmark::GetAction() const {
  return CPDF_Action(m_pDict ? m_pDict->GetDictFor("A") : nullptr);
}

int CPDF_Bookmark::GetCount() const {
  return m_pDict->GetIntegerFor("Count");
}
