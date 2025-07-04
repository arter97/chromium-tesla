// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PDF_PDF_INK_BRUSH_H_
#define PDF_PDF_INK_BRUSH_H_

#include <memory>
#include <optional>
#include <string>

#include "third_party/skia/include/core/SkColor.h"

namespace chrome_pdf {

class InkBrush;

// A wrapper class used to create ink brushes specifically for PDF annotation
// mode.
class PdfInkBrush {
 public:
  // The types of brushes supported in PDF annotation mode.
  enum class Type {
    kHighlighter,
    kPen,
  };

  // Parameters for the brush.
  struct Params {
    SkColor color;
    float size;
  };

  PdfInkBrush(Type brush_type, Params brush_params);

  PdfInkBrush(const PdfInkBrush&) = delete;
  PdfInkBrush& operator=(const PdfInkBrush&) = delete;
  ~PdfInkBrush();

  // Converts `brush_type` to a `Type`, returning `std::nullopt` if `brush_type`
  // does not correspond to any `Type`.
  static std::optional<Type> StringToType(const std::string& brush_type);

  // Returns the `InkBrush` that `this` represents.
  const InkBrush& GetInkBrush() const;

 private:
  // The ink brush of type `type_` with params` params_`. Always non-nullptr.
  std::unique_ptr<InkBrush> ink_brush_;
};

}  // namespace chrome_pdf

#endif  // PDF_PDF_INK_BRUSH_H_
