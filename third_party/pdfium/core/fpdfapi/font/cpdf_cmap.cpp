// Copyright 2017 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original code copyright 2014 Foxit Software Inc. http://www.foxitsoftware.com

#include "core/fpdfapi/font/cpdf_cmap.h"

#include <array>
#include <utility>
#include <vector>

#include "core/fpdfapi/cmaps/fpdf_cmaps.h"
#include "core/fpdfapi/font/cpdf_cmapparser.h"
#include "core/fpdfapi/font/cpdf_fontglobals.h"
#include "core/fpdfapi/parser/cpdf_simple_parser.h"
#include "core/fxcrt/check.h"
#include "core/fxcrt/compiler_specific.h"
#include "core/fxcrt/fx_memcpy_wrappers.h"

namespace {

struct ByteRange {
  uint8_t m_First;
  uint8_t m_Last;  // Inclusive.
};

struct PredefinedCMap {
  const char* m_pName;  // Raw, POD struct.
  CIDSet m_Charset;
  CIDCoding m_Coding;
  CPDF_CMap::CodingScheme m_CodingScheme;
  uint8_t m_LeadingSegCount;
  ByteRange m_LeadingSegs[2];
};

constexpr PredefinedCMap kPredefinedCMaps[] = {
    {"GB-EUC",
     CIDSET_GB1,
     CIDCoding::kGB,
     CPDF_CMap::MixedTwoBytes,
     1,
     {{0xa1, 0xfe}}},
    {"GBpc-EUC",
     CIDSET_GB1,
     CIDCoding::kGB,
     CPDF_CMap::MixedTwoBytes,
     1,
     {{0xa1, 0xfc}}},
    {"GBK-EUC",
     CIDSET_GB1,
     CIDCoding::kGB,
     CPDF_CMap::MixedTwoBytes,
     1,
     {{0x81, 0xfe}}},
    {"GBKp-EUC",
     CIDSET_GB1,
     CIDCoding::kGB,
     CPDF_CMap::MixedTwoBytes,
     1,
     {{0x81, 0xfe}}},
    {"GBK2K-EUC",
     CIDSET_GB1,
     CIDCoding::kGB,
     CPDF_CMap::MixedTwoBytes,
     1,
     {{0x81, 0xfe}}},
    {"GBK2K",
     CIDSET_GB1,
     CIDCoding::kGB,
     CPDF_CMap::MixedTwoBytes,
     1,
     {{0x81, 0xfe}}},
    {"UniGB-UCS2", CIDSET_GB1, CIDCoding::kUCS2, CPDF_CMap::TwoBytes, 0, {}},
    {"UniGB-UTF16", CIDSET_GB1, CIDCoding::kUTF16, CPDF_CMap::TwoBytes, 0, {}},
    {"B5pc",
     CIDSET_CNS1,
     CIDCoding::kBIG5,
     CPDF_CMap::MixedTwoBytes,
     1,
     {{0xa1, 0xfc}}},
    {"HKscs-B5",
     CIDSET_CNS1,
     CIDCoding::kBIG5,
     CPDF_CMap::MixedTwoBytes,
     1,
     {{0x88, 0xfe}}},
    {"ETen-B5",
     CIDSET_CNS1,
     CIDCoding::kBIG5,
     CPDF_CMap::MixedTwoBytes,
     1,
     {{0xa1, 0xfe}}},
    {"ETenms-B5",
     CIDSET_CNS1,
     CIDCoding::kBIG5,
     CPDF_CMap::MixedTwoBytes,
     1,
     {{0xa1, 0xfe}}},
    {"UniCNS-UCS2", CIDSET_CNS1, CIDCoding::kUCS2, CPDF_CMap::TwoBytes, 0, {}},
    {"UniCNS-UTF16",
     CIDSET_CNS1,
     CIDCoding::kUTF16,
     CPDF_CMap::TwoBytes,
     0,
     {}},
    {"83pv-RKSJ",
     CIDSET_JAPAN1,
     CIDCoding::kJIS,
     CPDF_CMap::MixedTwoBytes,
     2,
     {{0x81, 0x9f}, {0xe0, 0xfc}}},
    {"90ms-RKSJ",
     CIDSET_JAPAN1,
     CIDCoding::kJIS,
     CPDF_CMap::MixedTwoBytes,
     2,
     {{0x81, 0x9f}, {0xe0, 0xfc}}},
    {"90msp-RKSJ",
     CIDSET_JAPAN1,
     CIDCoding::kJIS,
     CPDF_CMap::MixedTwoBytes,
     2,
     {{0x81, 0x9f}, {0xe0, 0xfc}}},
    {"90pv-RKSJ",
     CIDSET_JAPAN1,
     CIDCoding::kJIS,
     CPDF_CMap::MixedTwoBytes,
     2,
     {{0x81, 0x9f}, {0xe0, 0xfc}}},
    {"Add-RKSJ",
     CIDSET_JAPAN1,
     CIDCoding::kJIS,
     CPDF_CMap::MixedTwoBytes,
     2,
     {{0x81, 0x9f}, {0xe0, 0xfc}}},
    {"EUC",
     CIDSET_JAPAN1,
     CIDCoding::kJIS,
     CPDF_CMap::MixedTwoBytes,
     2,
     {{0x8e, 0x8e}, {0xa1, 0xfe}}},
    {"H",
     CIDSET_JAPAN1,
     CIDCoding::kJIS,
     CPDF_CMap::TwoBytes,
     1,
     {{0x21, 0x7e}}},
    {"V",
     CIDSET_JAPAN1,
     CIDCoding::kJIS,
     CPDF_CMap::TwoBytes,
     1,
     {{0x21, 0x7e}}},
    {"Ext-RKSJ",
     CIDSET_JAPAN1,
     CIDCoding::kJIS,
     CPDF_CMap::MixedTwoBytes,
     2,
     {{0x81, 0x9f}, {0xe0, 0xfc}}},
    {"UniJIS-UCS2",
     CIDSET_JAPAN1,
     CIDCoding::kUCS2,
     CPDF_CMap::TwoBytes,
     0,
     {}},
    {"UniJIS-UCS2-HW",
     CIDSET_JAPAN1,
     CIDCoding::kUCS2,
     CPDF_CMap::TwoBytes,
     0,
     {}},
    {"UniJIS-UTF16",
     CIDSET_JAPAN1,
     CIDCoding::kUTF16,
     CPDF_CMap::TwoBytes,
     0,
     {}},
    {"KSC-EUC",
     CIDSET_KOREA1,
     CIDCoding::kKOREA,
     CPDF_CMap::MixedTwoBytes,
     1,
     {{0xa1, 0xfe}}},
    {"KSCms-UHC",
     CIDSET_KOREA1,
     CIDCoding::kKOREA,
     CPDF_CMap::MixedTwoBytes,
     1,
     {{0x81, 0xfe}}},
    {"KSCms-UHC-HW",
     CIDSET_KOREA1,
     CIDCoding::kKOREA,
     CPDF_CMap::MixedTwoBytes,
     1,
     {{0x81, 0xfe}}},
    {"KSCpc-EUC",
     CIDSET_KOREA1,
     CIDCoding::kKOREA,
     CPDF_CMap::MixedTwoBytes,
     1,
     {{0xa1, 0xfd}}},
    {"UniKS-UCS2", CIDSET_KOREA1, CIDCoding::kUCS2, CPDF_CMap::TwoBytes, 0, {}},
    {"UniKS-UTF16",
     CIDSET_KOREA1,
     CIDCoding::kUTF16,
     CPDF_CMap::TwoBytes,
     0,
     {}},
};

const PredefinedCMap* GetPredefinedCMap(ByteStringView cmapid) {
  if (cmapid.GetLength() > 2)
    cmapid = cmapid.First(cmapid.GetLength() - 2);
  for (const auto& map : kPredefinedCMaps) {
    if (cmapid == map.m_pName)
      return &map;
  }
  return nullptr;
}

std::vector<bool> LoadLeadingSegments(const PredefinedCMap& map) {
  std::vector<bool> segments(256);
  for (uint32_t i = 0; i < map.m_LeadingSegCount; ++i) {
    const ByteRange& seg = UNSAFE_TODO(map.m_LeadingSegs[i]);
    for (int b = seg.m_First; b <= seg.m_Last; ++b)
      segments[b] = true;
  }
  return segments;
}

int CheckFourByteCodeRange(uint8_t* codes,
                           size_t size,
                           const std::vector<CPDF_CMap::CodeRange>& ranges) {
  for (size_t i = ranges.size(); i > 0; i--) {
    size_t seg = i - 1;
    if (ranges[seg].m_CharSize < size)
      continue;
    size_t iChar = 0;
    while (iChar < size) {
      if (UNSAFE_TODO(codes[iChar] < ranges[seg].m_Lower[iChar]) ||
          UNSAFE_TODO(codes[iChar] > ranges[seg].m_Upper[iChar])) {
        break;
      }
      ++iChar;
    }
    if (iChar == ranges[seg].m_CharSize)
      return 2;
    if (iChar)
      return (size == ranges[seg].m_CharSize) ? 2 : 1;
  }
  return 0;
}

size_t GetFourByteCharSizeImpl(
    uint32_t charcode,
    const std::vector<CPDF_CMap::CodeRange>& ranges) {
  if (ranges.empty())
    return 1;

  uint8_t codes[4];
  codes[0] = codes[1] = 0x00;
  codes[2] = static_cast<uint8_t>(charcode >> 8 & 0xFF);
  codes[3] = static_cast<uint8_t>(charcode);
  for (size_t offset = 0; offset < 4; offset++) {
    size_t size = 4 - offset;
    for (size_t j = 0; j < ranges.size(); j++) {
      size_t iSeg = (ranges.size() - 1) - j;
      if (ranges[iSeg].m_CharSize < size)
        continue;
      size_t iChar = 0;
      while (iChar < size) {
        if (UNSAFE_TODO(codes[offset + iChar] < ranges[iSeg].m_Lower[iChar]) ||
            UNSAFE_TODO(codes[offset + iChar] > ranges[iSeg].m_Upper[iChar])) {
          break;
        }
        ++iChar;
      }
      if (iChar == ranges[iSeg].m_CharSize)
        return size;
    }
  }
  return 1;
}

const fxcmap::CMap* FindEmbeddedCMap(pdfium::span<const fxcmap::CMap> pCMaps,
                                     ByteStringView bsName) {
  for (size_t i = 0; i < pCMaps.size(); i++) {
    if (bsName == pCMaps[i].m_Name)
      return &pCMaps[i];
  }
  return nullptr;
}

}  // namespace

CPDF_CMap::CPDF_CMap(ByteStringView bsPredefinedName)
    : m_bVertical(bsPredefinedName.Back() == 'V') {
  if (bsPredefinedName == "Identity-H" || bsPredefinedName == "Identity-V") {
    m_Coding = CIDCoding::kCID;
    m_bLoaded = true;
    return;
  }

  const PredefinedCMap* map = GetPredefinedCMap(bsPredefinedName);
  if (!map)
    return;

  m_Charset = map->m_Charset;
  m_Coding = map->m_Coding;
  m_CodingScheme = map->m_CodingScheme;
  if (m_CodingScheme == MixedTwoBytes)
    m_MixedTwoByteLeadingBytes = LoadLeadingSegments(*map);
  m_pEmbedMap = FindEmbeddedCMap(
      CPDF_FontGlobals::GetInstance()->GetEmbeddedCharset(m_Charset),
      bsPredefinedName);
  if (!m_pEmbedMap)
    return;

  m_bLoaded = true;
}

CPDF_CMap::CPDF_CMap(pdfium::span<const uint8_t> spEmbeddedData)
    : m_DirectCharcodeToCIDTable(
          FixedSizeDataVector<uint16_t>::Zeroed(kDirectMapTableSize)) {
  CPDF_CMapParser parser(this);
  CPDF_SimpleParser syntax(spEmbeddedData);
  while (true) {
    ByteStringView word = syntax.GetWord();
    if (word.IsEmpty()) {
      break;
    }
    parser.ParseWord(word);
  }
}

CPDF_CMap::~CPDF_CMap() = default;

uint16_t CPDF_CMap::CIDFromCharCode(uint32_t charcode) const {
  if (m_Coding == CIDCoding::kCID)
    return static_cast<uint16_t>(charcode);

  if (m_pEmbedMap)
    return fxcmap::CIDFromCharCode(m_pEmbedMap, charcode);

  if (m_DirectCharcodeToCIDTable.empty())
    return static_cast<uint16_t>(charcode);

  auto table_span = m_DirectCharcodeToCIDTable.span();
  if (charcode < table_span.size())
    return table_span[charcode];

  auto it = std::lower_bound(m_AdditionalCharcodeToCIDMappings.begin(),
                             m_AdditionalCharcodeToCIDMappings.end(), charcode,
                             [](const CPDF_CMap::CIDRange& arg, uint32_t val) {
                               return arg.m_EndCode < val;
                             });
  if (it == m_AdditionalCharcodeToCIDMappings.end() ||
      it->m_StartCode > charcode) {
    return 0;
  }
  return it->m_StartCID + charcode - it->m_StartCode;
}

uint32_t CPDF_CMap::GetNextChar(ByteStringView pString, size_t* pOffset) const {
  size_t& offset = *pOffset;
  auto pBytes = pString.unsigned_span();
  switch (m_CodingScheme) {
    case OneByte: {
      return offset < pBytes.size() ? pBytes[offset++] : 0;
    }
    case TwoBytes: {
      uint8_t byte1 = offset < pBytes.size() ? pBytes[offset++] : 0;
      uint8_t byte2 = offset < pBytes.size() ? pBytes[offset++] : 0;
      return 256 * byte1 + byte2;
    }
    case MixedTwoBytes: {
      uint8_t byte1 = offset < pBytes.size() ? pBytes[offset++] : 0;
      if (!m_MixedTwoByteLeadingBytes[byte1])
        return byte1;
      uint8_t byte2 = offset < pBytes.size() ? pBytes[offset++] : 0;
      return 256 * byte1 + byte2;
    }
    case MixedFourBytes: {
      std::array<uint8_t, 4> codes;
      int char_size = 1;
      codes[0] = offset < pBytes.size() ? pBytes[offset++] : 0;
      while (true) {
        int ret = CheckFourByteCodeRange(codes.data(), char_size,
                                         m_MixedFourByteLeadingRanges);
        if (ret == 0)
          return 0;
        if (ret == 2) {
          uint32_t charcode = 0;
          for (int i = 0; i < char_size; i++)
            charcode = (charcode << 8) + codes[i];
          return charcode;
        }
        if (char_size == 4 || offset == pBytes.size())
          return 0;
        codes[char_size++] = pBytes[offset++];
      }
    }
  }
  return 0;
}

int CPDF_CMap::GetCharSize(uint32_t charcode) const {
  switch (m_CodingScheme) {
    case OneByte:
      return 1;
    case TwoBytes:
      return 2;
    case MixedTwoBytes:
      if (charcode < 0x100)
        return 1;
      return 2;
    case MixedFourBytes:
      if (charcode < 0x100)
        return 1;
      if (charcode < 0x10000)
        return 2;
      if (charcode < 0x1000000)
        return 3;
      return 4;
  }
  return 1;
}

size_t CPDF_CMap::CountChar(ByteStringView pString) const {
  switch (m_CodingScheme) {
    case OneByte:
      return pString.GetLength();
    case TwoBytes:
      return (pString.GetLength() + 1) / 2;
    case MixedTwoBytes: {
      size_t count = 0;
      for (size_t i = 0; i < pString.GetLength(); i++) {
        count++;
        if (m_MixedTwoByteLeadingBytes[pString[i]])
          i++;
      }
      return count;
    }
    case MixedFourBytes: {
      size_t count = 0;
      size_t offset = 0;
      while (offset < pString.GetLength()) {
        GetNextChar(pString, &offset);
        count++;
      }
      return count;
    }
  }
  return pString.GetLength();
}

// TODO: should be UNSAFE_BUFFER_USAGE
int CPDF_CMap::AppendChar(char* str, uint32_t charcode) const {
  switch (m_CodingScheme) {
    case OneByte:
      UNSAFE_TODO(str[0]) = static_cast<char>(charcode);
      return 1;
    case TwoBytes:
      UNSAFE_TODO(str[0]) = static_cast<char>(charcode / 256);
      UNSAFE_TODO(str[1]) = static_cast<char>(charcode % 256);
      return 2;
    case MixedTwoBytes:
      if (charcode < 0x100 && !m_MixedTwoByteLeadingBytes[charcode]) {
        UNSAFE_TODO(str[0]) = static_cast<char>(charcode);
        return 1;
      }
      UNSAFE_TODO(str[0]) = static_cast<char>(charcode >> 8);
      UNSAFE_TODO(str[1]) = static_cast<char>(charcode);
      return 2;
    case MixedFourBytes:
      if (charcode < 0x100) {
        int iSize = static_cast<int>(
            GetFourByteCharSizeImpl(charcode, m_MixedFourByteLeadingRanges));
        if (iSize == 0)
          iSize = 1;
        UNSAFE_TODO(str[iSize - 1]) = static_cast<char>(charcode);
        if (iSize > 1) {
          UNSAFE_TODO(FXSYS_memset(str, 0, iSize - 1));
        }
        return iSize;
      }
      if (charcode < 0x10000) {
        UNSAFE_TODO(str[0]) = static_cast<char>(charcode >> 8);
        UNSAFE_TODO(str[1]) = static_cast<char>(charcode);
        return 2;
      }
      if (charcode < 0x1000000) {
        UNSAFE_TODO(str[0]) = static_cast<char>(charcode >> 16);
        UNSAFE_TODO(str[1]) = static_cast<char>(charcode >> 8);
        UNSAFE_TODO(str[2]) = static_cast<char>(charcode);
        return 3;
      }
      UNSAFE_TODO(str[0]) = static_cast<char>(charcode >> 24);
      UNSAFE_TODO(str[1]) = static_cast<char>(charcode >> 16);
      UNSAFE_TODO(str[2]) = static_cast<char>(charcode >> 8);
      UNSAFE_TODO(str[3]) = static_cast<char>(charcode);
      return 4;
  }
  return 0;
}

void CPDF_CMap::SetAdditionalMappings(std::vector<CIDRange> mappings) {
  DCHECK(m_AdditionalCharcodeToCIDMappings.empty());
  if (m_CodingScheme != MixedFourBytes || mappings.empty())
    return;

  std::sort(
      mappings.begin(), mappings.end(),
      [](const CPDF_CMap::CIDRange& arg1, const CPDF_CMap::CIDRange& arg2) {
        return arg1.m_EndCode < arg2.m_EndCode;
      });
  m_AdditionalCharcodeToCIDMappings = std::move(mappings);
}

void CPDF_CMap::SetMixedFourByteLeadingRanges(std::vector<CodeRange> ranges) {
  m_MixedFourByteLeadingRanges = std::move(ranges);
}

void CPDF_CMap::SetDirectCharcodeToCIDTableRange(uint32_t start_code,
                                                 uint32_t end_code,
                                                 uint16_t start_cid) {
  pdfium::span<uint16_t> span = m_DirectCharcodeToCIDTable.span();
  for (uint32_t code = start_code; code <= end_code; ++code) {
    span[code] = static_cast<uint16_t>(start_cid + code - start_code);
  }
}
