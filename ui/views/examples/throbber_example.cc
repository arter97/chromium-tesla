// Copyright 2011 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/views/examples/throbber_example.h"

#include <memory>

#include "base/memory/raw_ptr.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/views/controls/throbber.h"
#include "ui/views/examples/grit/views_examples_resources.h"
#include "ui/views/layout/delegating_layout_manager.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/view.h"

namespace views::examples {

namespace {

class ThrobberView : public View, public LayoutDelegate {
  METADATA_HEADER(ThrobberView, View)

 public:
  ThrobberView() {
    throbber_ = AddChildView(std::make_unique<Throbber>());
    throbber_->Start();
    SetLayoutManager(std::make_unique<DelegatingLayoutManager>(this));
  }

  ThrobberView(const ThrobberView&) = delete;
  ThrobberView& operator=(const ThrobberView&) = delete;

  // View::
  gfx::Size CalculatePreferredSize(
      const SizeBounds& available_size) const override {
    return gfx::Size(available_size.width().value_or(width()),
                     available_size.height().value_or(height()));
  }

  // Overridden from LayoutDelegate:
  ProposedLayout CalculateProposedLayout(
      const SizeBounds& size_bounds) const override {
    constexpr int kDiameter = 16;
    ProposedLayout layout;
    if (!size_bounds.is_fully_bounded()) {
      layout.host_size = GetPreferredSize();
    } else {
      layout.host_size =
          gfx::Size(size_bounds.width().value(), size_bounds.height().value());
    }
    layout.child_layouts.emplace_back(
        throbber_.get(), throbber_->GetVisible(),
        gfx::Rect((layout.host_size.width() - kDiameter) / 2,
                  (layout.host_size.height() - kDiameter) / 2, kDiameter,
                  kDiameter));
    return layout;
  }

  bool OnMousePressed(const ui::MouseEvent& event) override {
    if (GetEventHandlerForPoint(event.location()) != throbber_)
      return false;

    if (is_checked_)
      throbber_->Start();
    else
      throbber_->Stop();
    throbber_->SetChecked(!is_checked_);
    is_checked_ = !is_checked_;
    return true;
  }

 private:
  raw_ptr<Throbber> throbber_;
  bool is_checked_ = false;
};

BEGIN_METADATA(ThrobberView)
END_METADATA

}  // namespace

ThrobberExample::ThrobberExample()
    : ExampleBase(l10n_util::GetStringUTF8(IDS_THROBBER_SELECT_LABEL).c_str()) {
}

ThrobberExample::~ThrobberExample() = default;

void ThrobberExample::CreateExampleView(View* container) {
  container->SetLayoutManager(std::make_unique<FillLayout>());
  container->AddChildView(std::make_unique<ThrobberView>());
}

}  // namespace views::examples
