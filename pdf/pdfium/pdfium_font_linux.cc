// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if defined(UNSAFE_BUFFERS_BUILD)
// TODO(crbug.com/40284755): Remove this and spanify to fix the errors.
#pragma allow_unsafe_buffers
#endif

#include "pdf/pdfium/pdfium_font_linux.h"

#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <limits>
#include <memory>
#include <string>

#include "base/check_op.h"
#include "base/containers/heap_array.h"
#include "base/containers/span.h"
#include "base/files/file.h"
#include "base/i18n/encoding_detection.h"
#include "base/i18n/icu_string_conversions.h"
#include "base/no_destructor.h"
#include "base/numerics/byte_conversions.h"
#include "base/numerics/safe_conversions.h"
#include "base/posix/eintr_wrapper.h"
#include "base/sequence_checker.h"
#include "base/strings/string_util.h"
#include "components/services/font/public/cpp/font_loader.h"
#include "pdf/pdfium/pdfium_engine.h"
#include "third_party/blink/public/platform/web_font_description.h"
#include "third_party/pdfium/public/fpdf_sysfontinfo.h"

namespace chrome_pdf {

namespace {

// GetFontTable loads a specified font table from an open SFNT file.
//   fd: a file descriptor to the SFNT file. The position doesn't matter.
//   table_tag: the table tag in *big-endian* format, or 0 for the entire font.
//   output: a buffer of size output_length that gets the data.  can be 0, in
//     which case output_length will be set to the required size in bytes.
//   output_length: size of output, if it's not 0.
//
//   returns: true on success.
//
// TODO(drott): This should be should be replaced with using FreeType for the
// purpose instead of reimplementing table parsing.
bool GetFontTable(int fd,
                  uint32_t table_tag,
                  uint8_t* output,
                  size_t* output_length) {
  size_t data_length = 0u;  // the length of the file data.
  off_t data_offset = 0;    // the offset of the data in the file.
  if (table_tag == 0) {
    // Get the entire font file.
    struct stat st;
    if (fstat(fd, &st) < 0) {
      return false;
    }
    data_length = base::checked_cast<size_t>(st.st_size);
  } else {
    // Get a font table. Read the header to find its offset in the file.
    uint8_t bytes[2];
    ssize_t n = HANDLE_EINTR(
        pread(fd, bytes, sizeof(bytes), 4 /* skip the font type */));
    if (n != sizeof(bytes)) {
      return false;
    }
    // Font data is stored in net (big-endian) order.
    uint16_t num_tables = base::numerics::U16FromBigEndian(bytes);

    // Read the table directory.
    static const size_t kTableEntrySize = 16u;
    auto table_entries =
        base::HeapArray<uint8_t>::WithSize(num_tables * kTableEntrySize);

    n = HANDLE_EINTR(pread(fd, table_entries.data(), table_entries.size(),
                           12 /* skip the SFNT header */));
    if (n != base::checked_cast<ssize_t>(table_entries.size())) {
      return false;
    }

    for (uint16_t i = 0u; i < num_tables; ++i) {
      auto entry = table_entries.subspan(i * kTableEntrySize, kTableEntrySize);
      // The `table_tag` is encoded in the same endian as the tag in the table.
      auto tag = base::numerics::U32FromNativeEndian(entry.first<4u>());
      if (tag == table_tag) {
        // Font data is stored in net (big-endian) order.
        data_offset = base::numerics::U32FromBigEndian(entry.subspan<8u, 4u>());
        data_length =
            base::numerics::U32FromBigEndian(entry.subspan<12u, 4u>());
        break;
      }
    }
  }

  if (!data_length) {
    return false;
  }

  if (output) {
    // 'output_length' holds the maximum amount of data the caller can accept.
    data_length = std::min(data_length, *output_length);
    ssize_t n = HANDLE_EINTR(pread(fd, output, data_length, data_offset));
    if (n != base::checked_cast<ssize_t>(data_length)) {
      return false;
    }
  }
  *output_length = data_length;

  return true;
}

// Maps font description and charset to `FontId` as requested by PDFium, with
// `FontId` as an opaque type that PDFium works with. Based on the `FontId`,
// PDFium can read from the font files using GetFontData(). Properly frees the
// underlying resource type when PDFium is done with the mapped font.
class BlinkFontMapper {
 public:
  // Defined as the type most convenient for use with PDFium's
  // `FPDF_SYSFONTINFO` functions.
  using FontId = void*;

  BlinkFontMapper() = default;
  ~BlinkFontMapper() = delete;

  // Returns a handle to the font mapped based on `desc` and `charset`, for use
  // as the `font_id` in GetFontData() and DeleteFont() below. Returns nullptr
  // on failure.
  // TODO(thestig): Document how this handles TTC files.
  FontId MapFont(const blink::WebFontDescription& desc, int charset) {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

    // If there was never a SkFontConfigInterface::SetGlobal() call, then `fci`
    // defaults to the direct interface, which is not suitable, as it does not
    // provide MatchFontWithFallback(). This only happens in unit tests, so just
    // refuse to map fonts there.
    sk_sp<SkFontConfigInterface> fci = SkFontConfigInterface::RefGlobal();
    if (fci.get() == SkFontConfigInterface::GetSingletonDirectInterface()) {
      return nullptr;
    }

    auto font_file = std::make_unique<base::File>();
    // In RendererBlinkPlatform, SkFontConfigInterface::SetGlobal() only ever
    // sets the global to a FontLoader. Thus it is safe to assume the returned
    // result is just that.
    auto* font_loader = reinterpret_cast<font_service::FontLoader*>(fci.get());
    font_loader->MatchFontWithFallback(
        desc.family.Utf8(),
        desc.weight >= blink::WebFontDescription::kWeightBold, desc.italic,
        charset, desc.generic_family, font_file.get());
    if (!font_file->IsValid()) {
      return nullptr;
    }

    // Release to PDFium. PDFium will free `font_file` in DeleteFont() below.
    return font_file.release();
  }

  // Releases the font file that `font_id` points to.
  void DeleteFont(FontId font_id) {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

    delete FileFromFontId(font_id);
  }

  // Reads data from the `font_id` handle for `table` into a `buffer` of
  // `buf_size`. Returns the amount of data read on success, or 0 on failure.
  // If `buffer` is null, then just return the required size for the buffer.
  // See content::GetFontTable() for information on the `table_tag` parameter.
  unsigned long GetFontData(FontId font_id,
                            unsigned int table_tag,
                            unsigned char* buffer,
                            unsigned long buf_size) const {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

    // TODO(thestig): cache?
    base::PlatformFile platform_file =
        FileFromFontId(font_id)->GetPlatformFile();
    size_t size = buf_size;
    if (!GetFontTable(platform_file, table_tag, buffer, &size)) {
      return 0;
    }
    return size;
  }

 private:
  static base::File* FileFromFontId(FontId font_id) {
    return reinterpret_cast<base::File*>(font_id);
  }

  SEQUENCE_CHECKER(sequence_checker_);
};

BlinkFontMapper& GetBlinkFontMapper() {
  static base::NoDestructor<BlinkFontMapper> mapper;
  return *mapper;
}

blink::WebFontDescription::Weight WeightToBlinkWeight(int weight) {
  static_assert(blink::WebFontDescription::kWeight100 == 0, "Blink Weight min");
  static_assert(blink::WebFontDescription::kWeight900 == 8, "Blink Weight max");
  constexpr int kMinimumWeight = 100;
  constexpr int kMaximumWeight = 900;
  int normalized_weight = std::clamp(weight, kMinimumWeight, kMaximumWeight);
  normalized_weight = (normalized_weight / 100) - 1;
  return static_cast<blink::WebFontDescription::Weight>(normalized_weight);
}

// This list is for CPWL_FontMap::GetDefaultFontByCharset().
// We pretend to have these font natively and let the browser (or underlying
// fontconfig) pick the proper font on the system.
void EnumFonts(FPDF_SYSFONTINFO* sysfontinfo, void* mapper) {
  FPDF_AddInstalledFont(mapper, "Arial", FXFONT_DEFAULT_CHARSET);

  const FPDF_CharsetFontMap* font_map = FPDF_GetDefaultTTFMap();
  for (; font_map->charset != -1; ++font_map) {
    FPDF_AddInstalledFont(mapper, font_map->fontname, font_map->charset);
  }
}

void* MapFont(FPDF_SYSFONTINFO*,
              int weight,
              int italic,
              int charset,
              int pitch_family,
              const char* face,
              int* exact) {
  // The code below is Blink-specific, return early in non-Blink mode to avoid
  // crashing.
  if (PDFiumEngine::GetFontMappingMode() != FontMappingMode::kBlink) {
    DCHECK_EQ(PDFiumEngine::GetFontMappingMode(), FontMappingMode::kNoMapping);
    return nullptr;
  }

  // Pretend the system does not have the Symbol font to force a fallback to
  // the built in Symbol font in CFX_FontMapper::FindSubstFont().
  if (strcmp(face, "Symbol") == 0) {
    return nullptr;
  }

  blink::WebFontDescription desc;
  if (pitch_family & FXFONT_FF_FIXEDPITCH) {
    desc.generic_family = blink::WebFontDescription::kGenericFamilyMonospace;
  } else if (pitch_family & FXFONT_FF_ROMAN) {
    desc.generic_family = blink::WebFontDescription::kGenericFamilySerif;
  } else {
    desc.generic_family = blink::WebFontDescription::kGenericFamilyStandard;
  }

  static const struct {
    const char* pdf_name;
    const char* face;
    bool bold;
    bool italic;
  } kPdfFontSubstitutions[] = {
      {"Courier", "Courier New", false, false},
      {"Courier-Bold", "Courier New", true, false},
      {"Courier-BoldOblique", "Courier New", true, true},
      {"Courier-Oblique", "Courier New", false, true},
      {"Helvetica", "Arial", false, false},
      {"Helvetica-Bold", "Arial", true, false},
      {"Helvetica-BoldOblique", "Arial", true, true},
      {"Helvetica-Oblique", "Arial", false, true},
      {"Times-Roman", "Times New Roman", false, false},
      {"Times-Bold", "Times New Roman", true, false},
      {"Times-BoldItalic", "Times New Roman", true, true},
      {"Times-Italic", "Times New Roman", false, true},

      // MS P?(Mincho|Gothic) are the most notable fonts in Japanese PDF files
      // without embedding the glyphs. Sometimes the font names are encoded
      // in Japanese Windows's locale (CP932/Shift_JIS) without space.
      // Most Linux systems don't have the exact font, but for outsourcing
      // fontconfig to find substitutable font in the system, we pass ASCII
      // font names to it.
      {"MS-PGothic", "MS PGothic", false, false},
      {"MS-Gothic", "MS Gothic", false, false},
      {"MS-PMincho", "MS PMincho", false, false},
      {"MS-Mincho", "MS Mincho", false, false},
      // MS PGothic in Shift_JIS encoding.
      {"\x82\x6C\x82\x72\x82\x6F\x83\x53\x83\x56\x83\x62\x83\x4E", "MS PGothic",
       false, false},
      // MS Gothic in Shift_JIS encoding.
      {"\x82\x6C\x82\x72\x83\x53\x83\x56\x83\x62\x83\x4E", "MS Gothic", false,
       false},
      // MS PMincho in Shift_JIS encoding.
      {"\x82\x6C\x82\x72\x82\x6F\x96\xBE\x92\xA9", "MS PMincho", false, false},
      // MS Mincho in Shift_JIS encoding.
      {"\x82\x6C\x82\x72\x96\xBE\x92\xA9", "MS Mincho", false, false},
  };

  // Similar logic exists in PDFium's CFX_FolderFontInfo::FindFont().
  if (charset == FXFONT_ANSI_CHARSET && (pitch_family & FXFONT_FF_FIXEDPITCH)) {
    face = "Courier New";
  }

  // Map from the standard PDF fonts to TrueType font names.
  size_t i;
  for (i = 0; i < std::size(kPdfFontSubstitutions); ++i) {
    if (strcmp(face, kPdfFontSubstitutions[i].pdf_name) == 0) {
      desc.family = blink::WebString::FromUTF8(kPdfFontSubstitutions[i].face);
      if (kPdfFontSubstitutions[i].bold) {
        desc.weight = blink::WebFontDescription::kWeightBold;
      }
      if (kPdfFontSubstitutions[i].italic) {
        desc.italic = true;
      }
      break;
    }
  }

  if (i == std::size(kPdfFontSubstitutions)) {
    // Convert to UTF-8 and make sure it is valid.
    std::string face_utf8;
    if (base::IsStringUTF8(face)) {
      face_utf8 = face;
    } else {
      std::string encoding;
      if (base::DetectEncoding(face, &encoding)) {
        // ConvertToUtf8AndNormalize() clears `face_utf8` on failure.
        base::ConvertToUtf8AndNormalize(face, encoding, &face_utf8);
      }
    }

    if (face_utf8.empty()) {
      return nullptr;
    }

    desc.family = blink::WebString::FromUTF8(face_utf8);
    desc.weight = WeightToBlinkWeight(weight);
    desc.italic = italic > 0;
  }

  return GetBlinkFontMapper().MapFont(desc, charset);
}

unsigned long GetFontData(FPDF_SYSFONTINFO*,
                          void* font_id,
                          unsigned int table,
                          unsigned char* buffer,
                          unsigned long buf_size) {
  DCHECK_EQ(PDFiumEngine::GetFontMappingMode(), FontMappingMode::kBlink);
  return GetBlinkFontMapper().GetFontData(font_id, table, buffer, buf_size);
}

void DeleteFont(FPDF_SYSFONTINFO*, void* font_id) {
  DCHECK_EQ(PDFiumEngine::GetFontMappingMode(), FontMappingMode::kBlink);
  GetBlinkFontMapper().DeleteFont(font_id);
}

FPDF_SYSFONTINFO g_font_info = {1,           0, EnumFonts, MapFont,   0,
                                GetFontData, 0, 0,         DeleteFont};

}  // namespace

void InitializeLinuxFontMapper() {
  FPDF_SetSystemFontInfo(&g_font_info);
}

}  // namespace chrome_pdf
