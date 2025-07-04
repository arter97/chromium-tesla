// Copyright 2016 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original code copyright 2014 Foxit Software Inc. http://www.foxitsoftware.com

#include "core/fpdfdoc/cpdf_pagelabel.h"

#include <utility>

#include "core/fpdfapi/parser/cpdf_dictionary.h"
#include "core/fpdfapi/parser/cpdf_document.h"
#include "core/fpdfapi/parser/fpdf_parser_decode.h"
#include "core/fpdfdoc/cpdf_numbertree.h"

namespace {

WideString MakeRoman(int num) {
  const int kArabic[] = {1000, 900, 500, 400, 100, 90, 50, 40, 10, 9, 5, 4, 1};
  const WideStringView kRoman[] = {L"m",  L"cm", L"d",  L"cd", L"c",
                                   L"xc", L"l",  L"xl", L"x",  L"ix",
                                   L"v",  L"iv", L"i"};
  const int kMaxNum = 1000000;

  num %= kMaxNum;
  int i = 0;
  WideString wsRomanNumber;
  while (num > 0) {
    UNSAFE_TODO({
      while (num >= kArabic[i]) {
        num = num - kArabic[i];
        wsRomanNumber += kRoman[i];
      }
      i = i + 1;
    });
  }
  return wsRomanNumber;
}

WideString MakeLetters(int num) {
  if (num == 0)
    return WideString();

  WideString wsLetters;
  const int nMaxCount = 1000;
  const int nLetterCount = 26;
  --num;

  int count = num / nLetterCount + 1;
  count %= nMaxCount;
  wchar_t ch = L'a' + num % nLetterCount;
  for (int i = 0; i < count; i++)
    wsLetters += ch;
  return wsLetters;
}

WideString GetLabelNumPortion(int num, const ByteString& bsStyle) {
  if (bsStyle.IsEmpty())
    return WideString();
  if (bsStyle == "D")
    return WideString::FormatInteger(num);
  if (bsStyle == "R") {
    WideString wsNumPortion = MakeRoman(num);
    wsNumPortion.MakeUpper();
    return wsNumPortion;
  }
  if (bsStyle == "r")
    return MakeRoman(num);
  if (bsStyle == "A") {
    WideString wsNumPortion = MakeLetters(num);
    wsNumPortion.MakeUpper();
    return wsNumPortion;
  }
  if (bsStyle == "a")
    return MakeLetters(num);
  return WideString();
}

}  // namespace

CPDF_PageLabel::CPDF_PageLabel(CPDF_Document* pDocument)
    : m_pDocument(pDocument) {}

CPDF_PageLabel::~CPDF_PageLabel() = default;

std::optional<WideString> CPDF_PageLabel::GetLabel(int nPage) const {
  if (!m_pDocument)
    return std::nullopt;

  if (nPage < 0 || nPage >= m_pDocument->GetPageCount())
    return std::nullopt;

  const CPDF_Dictionary* pPDFRoot = m_pDocument->GetRoot();
  if (!pPDFRoot)
    return std::nullopt;

  RetainPtr<const CPDF_Dictionary> pLabels = pPDFRoot->GetDictFor("PageLabels");
  if (!pLabels)
    return std::nullopt;

  CPDF_NumberTree numberTree(std::move(pLabels));
  RetainPtr<const CPDF_Object> pValue;
  int n = nPage;
  while (n >= 0) {
    pValue = numberTree.LookupValue(n);
    if (pValue)
      break;
    n--;
  }

  if (pValue) {
    pValue = pValue->GetDirect();
    if (const CPDF_Dictionary* pLabel = pValue->AsDictionary()) {
      WideString label;
      if (pLabel->KeyExist("P"))
        label += pLabel->GetUnicodeTextFor("P");

      ByteString bsNumberingStyle = pLabel->GetByteStringFor("S", ByteString());
      int nLabelNum = nPage - n + pLabel->GetIntegerFor("St", 1);
      WideString wsNumPortion = GetLabelNumPortion(nLabelNum, bsNumberingStyle);
      label += wsNumPortion;
      return label;
    }
  }
  return WideString::FormatInteger(nPage + 1);
}
