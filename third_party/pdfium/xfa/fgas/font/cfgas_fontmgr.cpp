// Copyright 2015 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original code copyright 2014 Foxit Software Inc. http://www.foxitsoftware.com

#include "xfa/fgas/font/cfgas_fontmgr.h"

#include <stdint.h>

#include <algorithm>
#include <iterator>
#include <memory>
#include <type_traits>
#include <utility>

#include "build/build_config.h"
#include "core/fxcrt/byteorder.h"
#include "core/fxcrt/cfx_read_only_vector_stream.h"
#include "core/fxcrt/check.h"
#include "core/fxcrt/compiler_specific.h"
#include "core/fxcrt/containers/contains.h"
#include "core/fxcrt/data_vector.h"
#include "core/fxcrt/fixed_size_data_vector.h"
#include "core/fxcrt/fx_codepage.h"
#include "core/fxcrt/fx_extension.h"
#include "core/fxcrt/fx_memcpy_wrappers.h"
#include "core/fxcrt/fx_system.h"
#include "core/fxcrt/numerics/safe_conversions.h"
#include "core/fxcrt/span.h"
#include "core/fxcrt/span_util.h"
#include "core/fxge/cfx_font.h"
#include "core/fxge/cfx_fontmapper.h"
#include "core/fxge/cfx_fontmgr.h"
#include "core/fxge/cfx_gemodule.h"
#include "core/fxge/fx_font.h"
#include "core/fxge/fx_fontencoding.h"
#include "xfa/fgas/font/cfgas_gefont.h"
#include "xfa/fgas/font/fgas_fontutils.h"

#if BUILDFLAG(IS_WIN)
#include "core/fxcrt/win/win_util.h"
#endif

namespace {

bool VerifyUnicode(const RetainPtr<CFGAS_GEFont>& pFont, wchar_t wcUnicode) {
  RetainPtr<CFX_Face> pFace = pFont->GetDevFont()->GetFace();
  if (!pFace)
    return false;

  CFX_Face::CharMap charmap = pFace->GetCurrentCharMap();
  if (!pFace->SelectCharMap(fxge::FontEncoding::kUnicode)) {
    return false;
  }

  if (pFace->GetCharIndex(wcUnicode) == 0) {
    pFace->SetCharMap(charmap);
    return false;
  }
  return true;
}

uint32_t ShortFormHash(FX_CodePage wCodePage,
                       uint32_t dwFontStyles,
                       WideStringView wsFontFamily) {
  ByteString bsHash = ByteString::Format("%d, %d", wCodePage, dwFontStyles);
  bsHash += FX_UTF8Encode(wsFontFamily);
  return FX_HashCode_GetA(bsHash.AsStringView());
}

uint32_t LongFormHash(FX_CodePage wCodePage,
                      uint16_t wBitField,
                      uint32_t dwFontStyles,
                      WideStringView wsFontFamily) {
  ByteString bsHash =
      ByteString::Format("%d, %d, %d", wCodePage, wBitField, dwFontStyles);
  bsHash += FX_UTF8Encode(wsFontFamily);
  return FX_HashCode_GetA(bsHash.AsStringView());
}

}  // namespace

#if BUILDFLAG(IS_WIN)

namespace {

struct FX_FONTMATCHPARAMS {
  const wchar_t* pwsFamily;
  uint32_t dwFontStyles;
  uint32_t dwUSB;
  bool matchParagraphStyle;
  wchar_t wUnicode;
  FX_CodePage wCodePage;
};

int32_t GetSimilarityScore(FX_FONTDESCRIPTOR const* pFont,
                           uint32_t dwFontStyles) {
  int32_t iValue = 0;
  if (FontStyleIsSymbolic(dwFontStyles) ==
      FontStyleIsSymbolic(pFont->dwFontStyles)) {
    iValue += 64;
  }
  if (FontStyleIsFixedPitch(dwFontStyles) ==
      FontStyleIsFixedPitch(pFont->dwFontStyles)) {
    iValue += 32;
  }
  if (FontStyleIsSerif(dwFontStyles) == FontStyleIsSerif(pFont->dwFontStyles))
    iValue += 16;
  if (FontStyleIsScript(dwFontStyles) == FontStyleIsScript(pFont->dwFontStyles))
    iValue += 8;
  return iValue;
}

const FX_FONTDESCRIPTOR* MatchDefaultFont(
    FX_FONTMATCHPARAMS* pParams,
    const std::deque<FX_FONTDESCRIPTOR>& fonts) {
  const FX_FONTDESCRIPTOR* pBestFont = nullptr;
  int32_t iBestSimilar = 0;
  for (const auto& font : fonts) {
    if (FontStyleIsForceBold(font.dwFontStyles) &&
        FontStyleIsItalic(font.dwFontStyles)) {
      continue;
    }

    if (pParams->pwsFamily) {
      if (FXSYS_wcsicmp(pParams->pwsFamily, font.wsFontFace))
        continue;
      if (font.uCharSet == FX_Charset::kSymbol)
        return &font;
    }
    if (font.uCharSet == FX_Charset::kSymbol)
      continue;
    if (pParams->wCodePage != FX_CodePage::kFailure) {
      if (FX_GetCodePageFromCharset(font.uCharSet) != pParams->wCodePage)
        continue;
    } else {
      if (pParams->dwUSB < 128) {
        uint32_t dwByte = pParams->dwUSB / 32;
        uint32_t dwUSB = 1 << (pParams->dwUSB % 32);
        if ((font.FontSignature.fsUsb[dwByte] & dwUSB) == 0)
          continue;
      }
    }
    if (pParams->matchParagraphStyle) {
      if ((font.dwFontStyles & 0x0F) == (pParams->dwFontStyles & 0x0F))
        return &font;
      continue;
    }
    if (pParams->pwsFamily) {
      if (FXSYS_wcsicmp(pParams->pwsFamily, font.wsFontFace) == 0)
        return &font;
    }
    int32_t iSimilarValue = GetSimilarityScore(&font, pParams->dwFontStyles);
    if (iBestSimilar < iSimilarValue) {
      iBestSimilar = iSimilarValue;
      pBestFont = &font;
    }
  }
  return iBestSimilar < 1 ? nullptr : pBestFont;
}

uint32_t GetGdiFontStyles(const LOGFONTW& lf) {
  uint32_t dwStyles = 0;
  if ((lf.lfPitchAndFamily & 0x03) == FIXED_PITCH)
    dwStyles |= FXFONT_FIXED_PITCH;
  uint8_t nFamilies = lf.lfPitchAndFamily & 0xF0;
  if (nFamilies == FF_ROMAN)
    dwStyles |= FXFONT_SERIF;
  if (nFamilies == FF_SCRIPT)
    dwStyles |= FXFONT_SCRIPT;
  if (lf.lfCharSet == SYMBOL_CHARSET)
    dwStyles |= FXFONT_SYMBOLIC;
  return dwStyles;
}

int32_t CALLBACK GdiFontEnumProc(ENUMLOGFONTEX* lpelfe,
                                 NEWTEXTMETRICEX* lpntme,
                                 DWORD dwFontType,
                                 LPARAM lParam) {
  if (dwFontType != TRUETYPE_FONTTYPE)
    return 1;
  const LOGFONTW& lf = ((LPENUMLOGFONTEXW)lpelfe)->elfLogFont;
  if (lf.lfFaceName[0] == L'@')
    return 1;
  FX_FONTDESCRIPTOR font = {};  // Aggregate initialization.
  static_assert(std::is_aggregate_v<decltype(font)>);
  font.uCharSet = FX_GetCharsetFromInt(lf.lfCharSet);
  font.dwFontStyles = GetGdiFontStyles(lf);
  UNSAFE_TODO({
    FXSYS_wcsncpy(font.wsFontFace, (const wchar_t*)lf.lfFaceName, 31);
    font.wsFontFace[31] = 0;
    FXSYS_memcpy(&font.FontSignature, &lpntme->ntmFontSig,
                 sizeof(lpntme->ntmFontSig));
  });
  reinterpret_cast<std::deque<FX_FONTDESCRIPTOR>*>(lParam)->push_back(font);
  return 1;
}

std::deque<FX_FONTDESCRIPTOR> EnumGdiFonts(const wchar_t* pwsFaceName,
                                           wchar_t wUnicode) {
  std::deque<FX_FONTDESCRIPTOR> fonts;
  if (!pdfium::IsUser32AndGdi32Available()) {
    // Without GDI32 and User32, GetDC / EnumFontFamiliesExW / ReleaseDC all
    // fail.
    return fonts;
  }

  LOGFONTW lfFind = {};  // Aggregate initialization.
  static_assert(std::is_aggregate_v<decltype(lfFind)>);
  lfFind.lfCharSet = DEFAULT_CHARSET;
  if (pwsFaceName) {
    UNSAFE_TODO({
      FXSYS_wcsncpy(lfFind.lfFaceName, pwsFaceName, 31);
      lfFind.lfFaceName[31] = 0;
    });
  }
  HDC hDC = ::GetDC(nullptr);
  EnumFontFamiliesExW(hDC, (LPLOGFONTW)&lfFind, (FONTENUMPROCW)GdiFontEnumProc,
                      (LPARAM)&fonts, 0);
  ::ReleaseDC(nullptr, hDC);
  return fonts;
}

}  // namespace

CFGAS_FontMgr::CFGAS_FontMgr() : m_FontFaces(EnumGdiFonts(nullptr, 0xFEFF)) {}

CFGAS_FontMgr::~CFGAS_FontMgr() = default;

bool CFGAS_FontMgr::EnumFonts() {
  return true;
}

RetainPtr<CFGAS_GEFont> CFGAS_FontMgr::GetFontByUnicodeImpl(
    wchar_t wUnicode,
    uint32_t dwFontStyles,
    const wchar_t* pszFontFamily,
    uint32_t dwHash,
    FX_CodePage wCodePage,
    uint16_t wBitField) {
  const FX_FONTDESCRIPTOR* pFD = FindFont(pszFontFamily, dwFontStyles, false,
                                          wCodePage, wBitField, wUnicode);
  if (!pFD && pszFontFamily) {
    pFD =
        FindFont(nullptr, dwFontStyles, false, wCodePage, wBitField, wUnicode);
  }
  if (!pFD)
    return nullptr;

  FX_CodePage newCodePage = FX_GetCodePageFromCharset(pFD->uCharSet);
  RetainPtr<CFGAS_GEFont> pFont =
      CFGAS_GEFont::LoadFont(pFD->wsFontFace, dwFontStyles, newCodePage);
  if (!pFont)
    return nullptr;

  pFont->SetLogicalFontStyle(dwFontStyles);
  if (!VerifyUnicode(pFont, wUnicode)) {
    m_FailedUnicodesSet.insert(wUnicode);
    return nullptr;
  }

  m_Hash2Fonts[dwHash].push_back(pFont);
  return pFont;
}

const FX_FONTDESCRIPTOR* CFGAS_FontMgr::FindFont(const wchar_t* pszFontFamily,
                                                 uint32_t dwFontStyles,
                                                 bool matchParagraphStyle,
                                                 FX_CodePage wCodePage,
                                                 uint32_t dwUSB,
                                                 wchar_t wUnicode) {
  FX_FONTMATCHPARAMS params = {};  // Aggregate initialization.
  static_assert(std::is_aggregate_v<decltype(params)>);
  params.dwUSB = dwUSB;
  params.wUnicode = wUnicode;
  params.wCodePage = wCodePage;
  params.pwsFamily = pszFontFamily;
  params.dwFontStyles = dwFontStyles;
  params.matchParagraphStyle = matchParagraphStyle;

  const FX_FONTDESCRIPTOR* pDesc = MatchDefaultFont(&params, m_FontFaces);
  if (pDesc)
    return pDesc;

  if (!pszFontFamily)
    return nullptr;

  // Use a named object to store the returned value of EnumGdiFonts() instead
  // of using a temporary object. This can prevent use-after-free issues since
  // pDesc may point to one of std::deque object's elements.
  std::deque<FX_FONTDESCRIPTOR> namedFonts =
      EnumGdiFonts(pszFontFamily, wUnicode);
  params.pwsFamily = nullptr;
  pDesc = MatchDefaultFont(&params, namedFonts);
  if (!pDesc)
    return nullptr;

  auto it = std::find(m_FontFaces.rbegin(), m_FontFaces.rend(), *pDesc);
  if (it != m_FontFaces.rend())
    return &*it;

  m_FontFaces.push_back(*pDesc);
  return &m_FontFaces.back();
}

#else  // BUILDFLAG(IS_WIN)

namespace {

const FX_CodePage kCodePages[] = {FX_CodePage::kMSWin_WesternEuropean,
                                  FX_CodePage::kMSWin_EasternEuropean,
                                  FX_CodePage::kMSWin_Cyrillic,
                                  FX_CodePage::kMSWin_Greek,
                                  FX_CodePage::kMSWin_Turkish,
                                  FX_CodePage::kMSWin_Hebrew,
                                  FX_CodePage::kMSWin_Arabic,
                                  FX_CodePage::kMSWin_Baltic,
                                  FX_CodePage::kMSWin_Vietnamese,
                                  FX_CodePage::kDefANSI,
                                  FX_CodePage::kDefANSI,
                                  FX_CodePage::kDefANSI,
                                  FX_CodePage::kDefANSI,
                                  FX_CodePage::kDefANSI,
                                  FX_CodePage::kDefANSI,
                                  FX_CodePage::kDefANSI,
                                  FX_CodePage::kMSDOS_Thai,
                                  FX_CodePage::kShiftJIS,
                                  FX_CodePage::kChineseSimplified,
                                  FX_CodePage::kHangul,
                                  FX_CodePage::kChineseTraditional,
                                  FX_CodePage::kJohab,
                                  FX_CodePage::kDefANSI,
                                  FX_CodePage::kDefANSI,
                                  FX_CodePage::kDefANSI,
                                  FX_CodePage::kDefANSI,
                                  FX_CodePage::kDefANSI,
                                  FX_CodePage::kDefANSI,
                                  FX_CodePage::kDefANSI,
                                  FX_CodePage::kDefANSI,
                                  FX_CodePage::kDefANSI,
                                  FX_CodePage::kDefANSI,
                                  FX_CodePage::kDefANSI,
                                  FX_CodePage::kDefANSI,
                                  FX_CodePage::kDefANSI,
                                  FX_CodePage::kDefANSI,
                                  FX_CodePage::kDefANSI,
                                  FX_CodePage::kDefANSI,
                                  FX_CodePage::kDefANSI,
                                  FX_CodePage::kDefANSI,
                                  FX_CodePage::kDefANSI,
                                  FX_CodePage::kDefANSI,
                                  FX_CodePage::kDefANSI,
                                  FX_CodePage::kDefANSI,
                                  FX_CodePage::kDefANSI,
                                  FX_CodePage::kDefANSI,
                                  FX_CodePage::kDefANSI,
                                  FX_CodePage::kDefANSI,
                                  FX_CodePage::kMSDOS_Greek2,
                                  FX_CodePage::kMSDOS_Russian,
                                  FX_CodePage::kMSDOS_Norwegian,
                                  FX_CodePage::kMSDOS_Arabic,
                                  FX_CodePage::kMSDOS_FrenchCanadian,
                                  FX_CodePage::kMSDOS_Hebrew,
                                  FX_CodePage::kMSDOS_Icelandic,
                                  FX_CodePage::kMSDOS_Portuguese,
                                  FX_CodePage::kMSDOS_Turkish,
                                  FX_CodePage::kMSDOS_Cyrillic,
                                  FX_CodePage::kMSDOS_EasternEuropean,
                                  FX_CodePage::kMSDOS_Baltic,
                                  FX_CodePage::kMSDOS_Greek1,
                                  FX_CodePage::kArabic_ASMO708,
                                  FX_CodePage::kMSDOS_WesternEuropean,
                                  FX_CodePage::kMSDOS_US};

uint16_t FX_GetCodePageBit(FX_CodePage wCodePage) {
  for (size_t i = 0; i < std::size(kCodePages); ++i) {
    if (UNSAFE_TODO(kCodePages[i]) == wCodePage) {
      return static_cast<uint16_t>(i);
    }
  }
  return static_cast<uint16_t>(-1);
}

uint16_t FX_GetUnicodeBit(wchar_t wcUnicode) {
  const FGAS_FONTUSB* x = FGAS_GetUnicodeBitField(wcUnicode);
  return x ? x->wBitField : FGAS_FONTUSB::kNoBitField;
}

uint16_t ReadUInt16FromSpanAtOffset(pdfium::span<const uint8_t> data,
                                    size_t offset) {
  return fxcrt::GetUInt16MSBFirst(data.subspan(offset));
}

extern "C" {

unsigned long ftStreamRead(FXFT_StreamRec* stream,
                           unsigned long offset,
                           unsigned char* buffer,
                           unsigned long count) {
  if (count == 0)
    return 0;

  IFX_SeekableReadStream* pFile =
      static_cast<IFX_SeekableReadStream*>(stream->descriptor.pointer);

  // SAFETY: required from caller.
  if (!pFile->ReadBlockAtOffset(
          UNSAFE_BUFFERS(pdfium::make_span(buffer, count), offset))) {
    return 0;
  }
  return count;
}

void ftStreamClose(FXFT_StreamRec* stream) {}

}  // extern "C"

std::vector<WideString> GetNames(pdfium::span<const uint8_t> name_table) {
  std::vector<WideString> results;
  if (name_table.empty())
    return results;

  uint16_t nNameCount = ReadUInt16FromSpanAtOffset(name_table, 2);
  pdfium::span<const uint8_t> str =
      name_table.subspan(ReadUInt16FromSpanAtOffset(name_table, 4));
  pdfium::span<const uint8_t> name_record = name_table.subspan(6);
  for (uint16_t i = 0; i < nNameCount; ++i) {
    uint16_t nNameID = ReadUInt16FromSpanAtOffset(name_table, i * 12 + 6);
    if (nNameID != 1)
      continue;

    uint16_t nPlatformID = ReadUInt16FromSpanAtOffset(name_record, i * 12);
    uint16_t nNameLength = ReadUInt16FromSpanAtOffset(name_record, i * 12 + 8);
    uint16_t nNameOffset = ReadUInt16FromSpanAtOffset(name_record, i * 12 + 10);
    if (nPlatformID != 1) {
      WideString wsFamily;
      for (uint16_t j = 0; j < nNameLength / 2; ++j) {
        wchar_t wcTemp = ReadUInt16FromSpanAtOffset(str, nNameOffset + j * 2);
        wsFamily += wcTemp;
      }
      results.push_back(wsFamily);
      continue;
    }

    WideString wsFamily;
    for (uint16_t j = 0; j < nNameLength; ++j) {
      wchar_t wcTemp = str[nNameOffset + j];
      wsFamily += wcTemp;
    }
    results.push_back(wsFamily);
  }
  return results;
}

uint32_t GetFlags(const RetainPtr<CFX_Face>& face) {
  uint32_t flags = 0;
  if (face->IsBold()) {
    flags |= FXFONT_FORCE_BOLD;
  }
  if (face->IsItalic()) {
    flags |= FXFONT_ITALIC;
  }
  if (face->IsFixedWidth()) {
    flags |= FXFONT_FIXED_PITCH;
  }

  std::optional<std::array<uint32_t, 2>> code_page_range =
      face->GetOs2CodePageRange();
  if (code_page_range.has_value() && (code_page_range.value()[0] & (1 << 31))) {
    flags |= FXFONT_SYMBOLIC;
  }

  std::optional<std::array<uint8_t, 2>> panose = face->GetOs2Panose();
  if (panose.has_value() && panose.value()[0] == 2) {
    uint8_t serif = panose.value()[1];
    if ((serif > 1 && serif < 10) || serif > 13) {
      flags |= FXFONT_SERIF;
    }
  }
  return flags;
}

RetainPtr<IFX_SeekableReadStream> CreateFontStream(CFX_FontMapper* pFontMapper,
                                                   size_t index) {
  FixedSizeDataVector<uint8_t> buffer = pFontMapper->RawBytesForIndex(index);
  if (buffer.empty()) {
    return nullptr;
  }
  return pdfium::MakeRetain<CFX_ReadOnlyVectorStream>(std::move(buffer));
}

RetainPtr<IFX_SeekableReadStream> CreateFontStream(
    const ByteString& bsFaceName) {
  CFX_FontMgr* pFontMgr = CFX_GEModule::Get()->GetFontMgr();
  CFX_FontMapper* pFontMapper = pFontMgr->GetBuiltinMapper();
  pFontMapper->LoadInstalledFonts();

  for (size_t i = 0; i < pFontMapper->GetFaceSize(); ++i) {
    if (pFontMapper->GetFaceName(i) == bsFaceName)
      return CreateFontStream(pFontMapper, i);
  }
  return nullptr;
}

RetainPtr<CFX_Face> LoadFace(
    const RetainPtr<IFX_SeekableReadStream>& pFontStream,
    int32_t iFaceIndex) {
  if (!pFontStream)
    return nullptr;

  CFX_FontMgr* pFontMgr = CFX_GEModule::Get()->GetFontMgr();
  FXFT_LibraryRec* library = pFontMgr->GetFTLibrary();
  if (!library)
    return nullptr;

  // TODO(palmer): This memory will be freed with |ft_free| (which is |free|).
  // Ultimately, we want to change this to:
  //   FXFT_Stream ftStream = FX_Alloc(FXFT_StreamRec, 1);
  // https://bugs.chromium.org/p/pdfium/issues/detail?id=690
  FXFT_StreamRec* ftStream =
      static_cast<FXFT_StreamRec*>(ft_scalloc(sizeof(FXFT_StreamRec), 1));
  UNSAFE_TODO(FXSYS_memset(ftStream, 0, sizeof(FXFT_StreamRec)));
  ftStream->base = nullptr;
  ftStream->descriptor.pointer = static_cast<void*>(pFontStream.Get());
  ftStream->pos = 0;
  ftStream->size = static_cast<unsigned long>(pFontStream->GetSize());
  ftStream->read = ftStreamRead;
  ftStream->close = ftStreamClose;

  FT_Open_Args ftArgs = {};  // Aggregate initialization.
  static_assert(std::is_aggregate_v<decltype(ftArgs)>);
  ftArgs.flags |= FT_OPEN_STREAM;
  ftArgs.stream = ftStream;

  RetainPtr<CFX_Face> pFace = CFX_Face::Open(library, &ftArgs, iFaceIndex);
  if (!pFace) {
    ft_sfree(ftStream);
    return nullptr;
  }
  pFace->SetPixelSize(0, 64);
  return pFace;
}

bool VerifyUnicodeForFontDescriptor(CFGAS_FontDescriptor* pDesc,
                                    wchar_t wcUnicode) {
  RetainPtr<IFX_SeekableReadStream> pFileRead =
      CreateFontStream(pDesc->m_wsFaceName.ToUTF8());
  if (!pFileRead)
    return false;

  RetainPtr<CFX_Face> pFace = LoadFace(pFileRead, pDesc->m_nFaceIndex);
  if (!pFace)
    return false;

  bool select_charmap_result =
      pFace->SelectCharMap(fxge::FontEncoding::kUnicode);
  int ret_index = pFace->GetCharIndex(wcUnicode);

  pFace->ClearExternalStream();

  return select_charmap_result && ret_index;
}

bool IsPartName(const WideString& name1, const WideString& name2) {
  return name1.Contains(name2.AsStringView());
}

int32_t CalcPenalty(CFGAS_FontDescriptor* pInstalled,
                    FX_CodePage wCodePage,
                    uint32_t dwFontStyles,
                    const WideString& FontName,
                    wchar_t wcUnicode) {
  int32_t nPenalty = 30000;
  if (FontName.GetLength() != 0) {
    if (FontName != pInstalled->m_wsFaceName) {
      size_t i;
      for (i = 0; i < pInstalled->m_wsFamilyNames.size(); ++i) {
        if (pInstalled->m_wsFamilyNames[i] == FontName)
          break;
      }
      if (i == pInstalled->m_wsFamilyNames.size())
        nPenalty += 0xFFFF;
      else
        nPenalty -= 28000;
    } else {
      nPenalty -= 30000;
    }
    if (nPenalty == 30000 && !IsPartName(pInstalled->m_wsFaceName, FontName)) {
      size_t i;
      for (i = 0; i < pInstalled->m_wsFamilyNames.size(); i++) {
        if (IsPartName(pInstalled->m_wsFamilyNames[i], FontName))
          break;
      }
      if (i == pInstalled->m_wsFamilyNames.size())
        nPenalty += 0xFFFF;
      else
        nPenalty -= 26000;
    } else {
      nPenalty -= 27000;
    }
  }
  uint32_t dwStyleMask = pInstalled->m_dwFontStyles ^ dwFontStyles;
  if (FontStyleIsForceBold(dwStyleMask))
    nPenalty += 4500;
  if (FontStyleIsFixedPitch(dwStyleMask))
    nPenalty += 10000;
  if (FontStyleIsItalic(dwStyleMask))
    nPenalty += 10000;
  if (FontStyleIsSerif(dwStyleMask))
    nPenalty += 500;
  if (FontStyleIsSymbolic(dwStyleMask))
    nPenalty += 0xFFFF;
  if (nPenalty >= 0xFFFF)
    return 0xFFFF;

  uint16_t wBit =
      (wCodePage == FX_CodePage::kDefANSI || wCodePage == FX_CodePage::kFailure)
          ? static_cast<uint16_t>(-1)
          : FX_GetCodePageBit(wCodePage);
  if (wBit != static_cast<uint16_t>(-1)) {
    DCHECK(wBit < 64);
    if ((pInstalled->m_dwCsb[wBit / 32] & (1 << (wBit % 32))) == 0)
      nPenalty += 0xFFFF;
    else
      nPenalty -= 60000;
  }
  wBit = (wcUnicode == 0 || wcUnicode == 0xFFFE) ? FGAS_FONTUSB::kNoBitField
                                                 : FX_GetUnicodeBit(wcUnicode);
  if (wBit != FGAS_FONTUSB::kNoBitField) {
    DCHECK(wBit < 128);
    if ((pInstalled->m_dwUsb[wBit / 32] & (1 << (wBit % 32))) == 0)
      nPenalty += 0xFFFF;
    else
      nPenalty -= 60000;
  }
  return nPenalty;
}

}  // namespace

CFGAS_FontDescriptor::CFGAS_FontDescriptor() = default;

CFGAS_FontDescriptor::~CFGAS_FontDescriptor() = default;

CFGAS_FontMgr::CFGAS_FontMgr() = default;

CFGAS_FontMgr::~CFGAS_FontMgr() = default;

bool CFGAS_FontMgr::EnumFontsFromFontMapper() {
  CFX_FontMapper* pFontMapper =
      CFX_GEModule::Get()->GetFontMgr()->GetBuiltinMapper();
  pFontMapper->LoadInstalledFonts();

  for (size_t i = 0; i < pFontMapper->GetFaceSize(); ++i) {
    RetainPtr<IFX_SeekableReadStream> pFontStream =
        CreateFontStream(pFontMapper, i);
    if (!pFontStream)
      continue;

    WideString wsFaceName =
        WideString::FromDefANSI(pFontMapper->GetFaceName(i).AsStringView());
    RegisterFaces(pFontStream, wsFaceName);
  }

  return !m_InstalledFonts.empty();
}

bool CFGAS_FontMgr::EnumFonts() {
  return EnumFontsFromFontMapper();
}

RetainPtr<CFGAS_GEFont> CFGAS_FontMgr::GetFontByUnicodeImpl(
    wchar_t wUnicode,
    uint32_t dwFontStyles,
    const wchar_t* pszFontFamily,
    uint32_t dwHash,
    FX_CodePage wCodePage,
    uint16_t /* wBitField*/) {
  if (!pdfium::Contains(m_Hash2CandidateList, dwHash)) {
    m_Hash2CandidateList[dwHash] =
        MatchFonts(wCodePage, dwFontStyles, pszFontFamily, wUnicode);
  }
  for (const auto& info : m_Hash2CandidateList[dwHash]) {
    CFGAS_FontDescriptor* pDesc = info.pFont;
    if (!VerifyUnicodeForFontDescriptor(pDesc, wUnicode))
      continue;
    RetainPtr<CFGAS_GEFont> pFont =
        LoadFontInternal(pDesc->m_wsFaceName, pDesc->m_nFaceIndex);
    if (!pFont)
      continue;
    pFont->SetLogicalFontStyle(dwFontStyles);
    m_Hash2Fonts[dwHash].push_back(pFont);
    return pFont;
  }
  if (!pszFontFamily)
    m_FailedUnicodesSet.insert(wUnicode);
  return nullptr;
}

RetainPtr<CFGAS_GEFont> CFGAS_FontMgr::LoadFontInternal(
    const WideString& wsFaceName,
    int32_t iFaceIndex) {
  RetainPtr<IFX_SeekableReadStream> pFontStream =
      CreateFontStream(wsFaceName.ToUTF8());
  if (!pFontStream)
    return nullptr;

  auto pInternalFont = std::make_unique<CFX_Font>();
  if (!pInternalFont->LoadFile(std::move(pFontStream), iFaceIndex))
    return nullptr;

  return CFGAS_GEFont::LoadFont(std::move(pInternalFont));
}

std::vector<CFGAS_FontDescriptorInfo> CFGAS_FontMgr::MatchFonts(
    FX_CodePage wCodePage,
    uint32_t dwFontStyles,
    const WideString& FontName,
    wchar_t wcUnicode) {
  std::vector<CFGAS_FontDescriptorInfo> matched_fonts;
  for (const auto& pFont : m_InstalledFonts) {
    int32_t nPenalty =
        CalcPenalty(pFont.get(), wCodePage, dwFontStyles, FontName, wcUnicode);
    if (nPenalty >= 0xffff)
      continue;
    matched_fonts.push_back({pFont.get(), nPenalty});
    if (matched_fonts.size() == 0xffff)
      break;
  }
  std::stable_sort(matched_fonts.begin(), matched_fonts.end());
  return matched_fonts;
}

void CFGAS_FontMgr::RegisterFace(RetainPtr<CFX_Face> pFace,
                                 const WideString& wsFaceName) {
  if (!pFace->IsScalable()) {
    return;
  }

  auto pFont = std::make_unique<CFGAS_FontDescriptor>();
  pFont->m_dwFontStyles |= GetFlags(pFace);

  // TODO(crbug.com/pdfium/2085): Use make_span() in fewer places after updating
  // pdfium::span.
  std::optional<std::array<uint32_t, 4>> unicode_range =
      pFace->GetOs2UnicodeRange();
  auto usb_span = pdfium::make_span(pFont->m_dwUsb);
  if (unicode_range.has_value()) {
    fxcrt::spancpy(usb_span, pdfium::make_span(unicode_range.value()));
  } else {
    fxcrt::spanclr(usb_span);
  }

  std::optional<std::array<uint32_t, 2>> code_page_range =
      pFace->GetOs2CodePageRange();
  auto csb_span = pdfium::make_span(pFont->m_dwCsb);
  if (code_page_range.has_value()) {
    fxcrt::spancpy(csb_span, pdfium::make_span(code_page_range.value()));
  } else {
    fxcrt::spanclr(csb_span);
  }

  static constexpr uint32_t kNameTag =
      CFX_FontMapper::MakeTag('n', 'a', 'm', 'e');

  DataVector<uint8_t> table;
  size_t table_size = pFace->GetSfntTable(kNameTag, table);
  if (table_size) {
    table.resize(table_size);
    if (!pFace->GetSfntTable(kNameTag, table)) {
      table.clear();
    }
  }
  pFont->m_wsFamilyNames = GetNames(table);
  pFont->m_wsFamilyNames.push_back(
      WideString::FromUTF8(pFace->GetRec()->family_name));
  pFont->m_wsFaceName = wsFaceName;
  pFont->m_nFaceIndex =
      pdfium::checked_cast<int32_t>(pFace->GetRec()->face_index);
  m_InstalledFonts.push_back(std::move(pFont));
}

void CFGAS_FontMgr::RegisterFaces(
    const RetainPtr<IFX_SeekableReadStream>& pFontStream,
    const WideString& wsFaceName) {
  int32_t index = 0;
  int32_t num_faces = 0;
  do {
    RetainPtr<CFX_Face> pFace = LoadFace(pFontStream, index++);
    if (!pFace)
      continue;
    // All faces keep number of faces. It can be retrieved from any one face.
    if (num_faces == 0) {
      num_faces = pdfium::checked_cast<int32_t>(pFace->GetRec()->num_faces);
    }
    RegisterFace(pFace, wsFaceName);
    pFace->ClearExternalStream();
  } while (index < num_faces);
}

#endif  // BUILDFLAG(IS_WIN)

RetainPtr<CFGAS_GEFont> CFGAS_FontMgr::GetFontByCodePage(
    FX_CodePage wCodePage,
    uint32_t dwFontStyles,
    const wchar_t* pszFontFamily) {
  uint32_t dwHash = ShortFormHash(wCodePage, dwFontStyles, pszFontFamily);
  auto* pFontVector = &m_Hash2Fonts[dwHash];
  if (!pFontVector->empty()) {
    for (auto iter = pFontVector->begin(); iter != pFontVector->end(); ++iter) {
      if (*iter != nullptr)
        return *iter;
    }
    return nullptr;
  }

#if BUILDFLAG(IS_WIN)
  const FX_FONTDESCRIPTOR* pFD =
      FindFont(pszFontFamily, dwFontStyles, true, wCodePage,
               FGAS_FONTUSB::kNoBitField, 0);
  if (!pFD) {
    pFD = FindFont(nullptr, dwFontStyles, true, wCodePage,
                   FGAS_FONTUSB::kNoBitField, 0);
  }
  if (!pFD) {
    pFD = FindFont(nullptr, dwFontStyles, false, wCodePage,
                   FGAS_FONTUSB::kNoBitField, 0);
  }
  if (!pFD)
    return nullptr;

  RetainPtr<CFGAS_GEFont> pFont =
      CFGAS_GEFont::LoadFont(pFD->wsFontFace, dwFontStyles, wCodePage);
#else   // BUILDFLAG(IS_WIN)
  if (!pdfium::Contains(m_Hash2CandidateList, dwHash)) {
    m_Hash2CandidateList[dwHash] =
        MatchFonts(wCodePage, dwFontStyles, WideString(pszFontFamily), 0);
  }
  if (m_Hash2CandidateList[dwHash].empty())
    return nullptr;

  CFGAS_FontDescriptor* pDesc = m_Hash2CandidateList[dwHash].front().pFont;
  RetainPtr<CFGAS_GEFont> pFont =
      LoadFontInternal(pDesc->m_wsFaceName, pDesc->m_nFaceIndex);
#endif  // BUILDFLAG(IS_WIN)

  if (!pFont)
    return nullptr;

  pFont->SetLogicalFontStyle(dwFontStyles);
  pFontVector->push_back(pFont);
  return pFont;
}

RetainPtr<CFGAS_GEFont> CFGAS_FontMgr::GetFontByUnicode(
    wchar_t wUnicode,
    uint32_t dwFontStyles,
    const wchar_t* pszFontFamily) {
  if (pdfium::Contains(m_FailedUnicodesSet, wUnicode))
    return nullptr;

  const FGAS_FONTUSB* x = FGAS_GetUnicodeBitField(wUnicode);
  FX_CodePage wCodePage = x ? x->wCodePage : FX_CodePage::kFailure;
  uint16_t wBitField = x ? x->wBitField : FGAS_FONTUSB::kNoBitField;
  uint32_t dwHash =
      wCodePage == FX_CodePage::kFailure
          ? LongFormHash(wCodePage, wBitField, dwFontStyles, pszFontFamily)
          : ShortFormHash(wCodePage, dwFontStyles, pszFontFamily);
  for (auto& pFont : m_Hash2Fonts[dwHash]) {
    if (VerifyUnicode(pFont, wUnicode))
      return pFont;
  }
  return GetFontByUnicodeImpl(wUnicode, dwFontStyles, pszFontFamily, dwHash,
                              wCodePage, wBitField);
}

RetainPtr<CFGAS_GEFont> CFGAS_FontMgr::LoadFont(const wchar_t* pszFontFamily,
                                                uint32_t dwFontStyles,
                                                FX_CodePage wCodePage) {
#if BUILDFLAG(IS_WIN)
  uint32_t dwHash = ShortFormHash(wCodePage, dwFontStyles, pszFontFamily);
  std::vector<RetainPtr<CFGAS_GEFont>>* pFontArray = &m_Hash2Fonts[dwHash];
  if (!pFontArray->empty())
    return pFontArray->front();

  const FX_FONTDESCRIPTOR* pFD =
      FindFont(pszFontFamily, dwFontStyles, true, wCodePage,
               FGAS_FONTUSB::kNoBitField, 0);
  if (!pFD) {
    pFD = FindFont(pszFontFamily, dwFontStyles, false, wCodePage,
                   FGAS_FONTUSB::kNoBitField, 0);
  }
  if (!pFD)
    return nullptr;

  RetainPtr<CFGAS_GEFont> pFont =
      CFGAS_GEFont::LoadFont(pFD->wsFontFace, dwFontStyles, wCodePage);
  if (!pFont)
    return nullptr;

  pFont->SetLogicalFontStyle(dwFontStyles);
  pFontArray->push_back(pFont);
  return pFont;
#else   // BUILDFLAG(IS_WIN)
  return GetFontByCodePage(wCodePage, dwFontStyles, pszFontFamily);
#endif  // BUILDFLAG(IS_WIN)
}
