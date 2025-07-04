// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/accessibility/ax_role_properties.h"

#include "build/build_config.h"
#include "build/chromeos_buildflags.h"
#include "ui/accessibility/ax_enums.mojom.h"

namespace ui {

namespace {

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_CHROMEOS_ASH)
constexpr bool kExposeLayoutTableAsDataTable = true;
#else
constexpr bool kExposeLayoutTableAsDataTable = false;
#endif  // BUILDFLAG(IS_WIN)

}  // namespace

bool CanHaveInlineTextBoxChildren(ax::mojom::Role role) {
  switch (role) {
    case ax::mojom::Role::kLineBreak:
    case ax::mojom::Role::kStaticText:
      return true;
    default:
      return false;
  }
}

bool HasPresentationalChildren(const ax::mojom::Role role) {
  // See http://www.w3.org/TR/core-aam-1.1/#exclude_elements2.
  if (IsImage(role))
    return true;

  switch (role) {
    case ax::mojom::Role::kButton:
    case ax::mojom::Role::kCheckBox:
    case ax::mojom::Role::kMath:
    // Return false for kMathMLMath, since the children of a <math> tag should
    // be exposed to make MathML accessible.
    case ax::mojom::Role::kMenuItemCheckBox:
    case ax::mojom::Role::kMenuItemRadio:
    case ax::mojom::Role::kMenuListOption:
    case ax::mojom::Role::kProgressIndicator:
    case ax::mojom::Role::kScrollBar:
    case ax::mojom::Role::kSlider:
    case ax::mojom::Role::kSwitch:
    case ax::mojom::Role::kTab:
      return true;
    default:
      return false;
  }
}

bool IsAlert(const ax::mojom::Role role) {
  switch (role) {
    case ax::mojom::Role::kAlert:        // For simple or hidden alerts.
    case ax::mojom::Role::kAlertDialog:  // For alerts that must be dismissed.
      return true;
    default:
      return false;
  }
}

bool IsAndroidTextViewCandidate(const ax::mojom::Role role) {
  switch (role) {
    case ax::mojom::Role::kGenericContainer:
    case ax::mojom::Role::kHeading:
    case ax::mojom::Role::kParagraph:
      return true;
    default:
      return false;
  }
}

bool IsButton(const ax::mojom::Role role) {
  // According to the WAI-ARIA spec, native button or role="button"
  // supports |aria-expanded| and |aria-pressed|.
  // If the button has |aria-expanded| set, then it takes on
  // Role::kPopUpButton.
  // If the button has |aria-pressed| set, then it takes on
  // Role::kToggleButton.
  // https://www.w3.org/TR/wai-aria-1.1/#button
  return role == ax::mojom::Role::kButton ||
         // TODO(crbug.com/40864556): Treat kComboBoxSelect like a combobox.
         // When removing this, update ChromeVox's AutomationPredicate wherever
         // it's looking at isButton.
         role == ax::mojom::Role::kComboBoxSelect ||
         role == ax::mojom::Role::kPopUpButton ||
         role == ax::mojom::Role::kToggleButton;
}

bool IsCellOrTableHeader(const ax::mojom::Role role) {
  switch (role) {
    case ax::mojom::Role::kCell:
    case ax::mojom::Role::kGridCell:
    case ax::mojom::Role::kColumnHeader:
    case ax::mojom::Role::kMathMLTableCell:
    case ax::mojom::Role::kRowHeader:
      return true;
    case ax::mojom::Role::kLayoutTableCell:
      return kExposeLayoutTableAsDataTable;
    default:
      return false;
  }
}

bool IsClickable(const ax::mojom::Role role) {
  switch (role) {
    case ax::mojom::Role::kButton:
    case ax::mojom::Role::kCheckBox:
    case ax::mojom::Role::kColorWell:
    case ax::mojom::Role::kComboBoxMenuButton:
    case ax::mojom::Role::kComboBoxSelect:
    case ax::mojom::Role::kDate:
    case ax::mojom::Role::kDateTime:
    case ax::mojom::Role::kDisclosureTriangle:
    case ax::mojom::Role::kDisclosureTriangleGrouped:
    case ax::mojom::Role::kDocBackLink:
    case ax::mojom::Role::kDocBiblioRef:
    case ax::mojom::Role::kDocGlossRef:
    case ax::mojom::Role::kDocNoteRef:
    case ax::mojom::Role::kImeCandidate:
    case ax::mojom::Role::kInputTime:
    case ax::mojom::Role::kLink:
    case ax::mojom::Role::kListBox:
    case ax::mojom::Role::kListBoxOption:
    case ax::mojom::Role::kMenuItem:
    case ax::mojom::Role::kMenuItemCheckBox:
    case ax::mojom::Role::kMenuItemRadio:
    case ax::mojom::Role::kMenuListOption:
    case ax::mojom::Role::kPdfActionableHighlight:
    case ax::mojom::Role::kPopUpButton:
    case ax::mojom::Role::kPortal:
    case ax::mojom::Role::kRadioButton:
    case ax::mojom::Role::kSearchBox:
    case ax::mojom::Role::kSpinButton:
    case ax::mojom::Role::kSwitch:
    case ax::mojom::Role::kTab:
    case ax::mojom::Role::kTextField:
    case ax::mojom::Role::kTextFieldWithComboBox:
    // kTree and related roles are not included because they are not natively
    // supported by HTML and so their "clickable" behavior is uncertain.
    case ax::mojom::Role::kToggleButton:
      return true;
    default:
      return false;
  }
}

bool IsCheckBox(const ax::mojom::Role role) {
  switch (role) {
    case ax::mojom::Role::kCheckBox:
    case ax::mojom::Role::kMenuItemCheckBox:
    case ax::mojom::Role::kSwitch:
      return true;
    default:
      return false;
  }
}

bool IsComboBox(const ax::mojom::Role role) {
  // TODO(crbug.com/40864556): Treat kComboBoxSelect like a combobox.
  switch (role) {
    case ax::mojom::Role::kComboBoxMenuButton:
    case ax::mojom::Role::kComboBoxGrouping:
    case ax::mojom::Role::kTextFieldWithComboBox:
      return true;
    default:
      return false;
  }
}

bool IsComboBoxContainer(const ax::mojom::Role role) {
  switch (role) {
    case ax::mojom::Role::kDialog:
    case ax::mojom::Role::kGrid:
    case ax::mojom::Role::kListBox:
    case ax::mojom::Role::kTree:
    case ax::mojom::Role::kTreeGrid:
      return true;
    default:
      return false;
  }
}

bool IsContainerWithSelectableChildren(const ax::mojom::Role role) {
  switch (role) {
    case ax::mojom::Role::kComboBoxGrouping:
    case ax::mojom::Role::kComboBoxMenuButton:
    case ax::mojom::Role::kGrid:
    case ax::mojom::Role::kListBox:
    case ax::mojom::Role::kListGrid:
    case ax::mojom::Role::kMenu:
    case ax::mojom::Role::kMenuBar:
    case ax::mojom::Role::kMenuListPopup:
    case ax::mojom::Role::kRadioGroup:
    case ax::mojom::Role::kTabList:
    case ax::mojom::Role::kToolbar:
    case ax::mojom::Role::kTree:
    case ax::mojom::Role::kTreeGrid:
      return true;
    default:
      return false;
  }
}

bool IsControl(const ax::mojom::Role role) {
  switch (role) {
    case ax::mojom::Role::kButton:
    case ax::mojom::Role::kCheckBox:
    case ax::mojom::Role::kColorWell:
    case ax::mojom::Role::kComboBoxMenuButton:
    case ax::mojom::Role::kComboBoxSelect:
    case ax::mojom::Role::kDate:
    case ax::mojom::Role::kDateTime:
    case ax::mojom::Role::kDisclosureTriangle:
    case ax::mojom::Role::kDisclosureTriangleGrouped:
    case ax::mojom::Role::kInputTime:
    case ax::mojom::Role::kListBox:
    case ax::mojom::Role::kListGrid:
    case ax::mojom::Role::kMenuItem:
    case ax::mojom::Role::kMenuItemCheckBox:
    case ax::mojom::Role::kMenuItemRadio:
    case ax::mojom::Role::kMenuListOption:
    case ax::mojom::Role::kPdfActionableHighlight:
    case ax::mojom::Role::kPopUpButton:
    case ax::mojom::Role::kRadioButton:
    case ax::mojom::Role::kScrollBar:
    case ax::mojom::Role::kSearchBox:
    case ax::mojom::Role::kSlider:
    case ax::mojom::Role::kSpinButton:
    case ax::mojom::Role::kSwitch:
    case ax::mojom::Role::kTab:
    case ax::mojom::Role::kTextField:
    case ax::mojom::Role::kTextFieldWithComboBox:
    case ax::mojom::Role::kToggleButton:
    case ax::mojom::Role::kTree:
    case ax::mojom::Role::kTreeGrid:
    case ax::mojom::Role::kTreeItem:
      return true;
    default:
      return false;
  }
}

bool IsControlOnAndroid(const ax::mojom::Role role, bool isFocusable) {
  switch (role) {
    case ax::mojom::Role::kSplitter:
      return isFocusable;
    case ax::mojom::Role::kDate:
    case ax::mojom::Role::kDateTime:
    case ax::mojom::Role::kDocBackLink:
    case ax::mojom::Role::kDocBiblioRef:
    case ax::mojom::Role::kDocGlossRef:
    case ax::mojom::Role::kDocNoteRef:
    case ax::mojom::Role::kInputTime:
    case ax::mojom::Role::kLink:
    case ax::mojom::Role::kTreeItem:
      return true;
    case ax::mojom::Role::kAlert:
    case ax::mojom::Role::kDialog:
    case ax::mojom::Role::kMenu:
    case ax::mojom::Role::kMenuBar:
    case ax::mojom::Role::kTree:
    case ax::mojom::Role::kUnknown:
      return false;
    default:
      return IsControl(role);
  }
}

bool IsDateOrTimeInput(const ax::mojom::Role role) {
  switch (role) {
    case ax::mojom::Role::kDate:
    case ax::mojom::Role::kDateTime:
    case ax::mojom::Role::kInputTime:
      return true;
    default:
      return false;
  }
}

bool IsDialog(const ax::mojom::Role role) {
  switch (role) {
    case ax::mojom::Role::kAlertDialog:
    case ax::mojom::Role::kDialog:
      return true;
    default:
      return false;
  }
}

bool IsEmbeddingElement(const ax::mojom::Role role) {
  switch (role) {
    case ax::mojom::Role::kEmbeddedObject:
    case ax::mojom::Role::kIframe:
    case ax::mojom::Role::kIframePresentational:
    case ax::mojom::Role::kPluginObject:
    case ax::mojom::Role::kPortal:
      return true;
    default:
      return false;
  }
}

bool IsGridLike(const ax::mojom::Role role) {
  switch (role) {
    case ax::mojom::Role::kGrid:
    case ax::mojom::Role::kListGrid:
    case ax::mojom::Role::kTree:
    case ax::mojom::Role::kTreeGrid:
      return true;
    default:
      return false;
  }
}

bool IsForm(const ax::mojom::Role role) {
  switch (role) {
    case ax::mojom::Role::kForm:
      return true;
    default:
      return false;
  }
}

bool IsFormatBoundary(const ax::mojom::Role role) {
  return IsControl(role) || IsHeading(role) || IsImageOrVideo(role);
}

bool IsHeading(const ax::mojom::Role role) {
  switch (role) {
    case ax::mojom::Role::kHeading:
    case ax::mojom::Role::kDocSubtitle:
      return true;
    default:
      return false;
  }
}

bool IsIframe(ax::mojom::Role role) {
  switch (role) {
    case ax::mojom::Role::kIframe:
    case ax::mojom::Role::kIframePresentational:
      return true;
    default:
      return false;
  }
}

bool IsImageOrVideo(const ax::mojom::Role role) {
  return IsImage(role) || role == ax::mojom::Role::kVideo;
}

bool IsImage(const ax::mojom::Role role) {
  switch (role) {
    case ax::mojom::Role::kCanvas:
    case ax::mojom::Role::kDocCover:
    case ax::mojom::Role::kGraphicsSymbol:
    case ax::mojom::Role::kImage:
    case ax::mojom::Role::kSvgRoot:
      return true;
    default:
      return false;
  }
}

bool IsItemLike(const ax::mojom::Role role) {
  switch (role) {
    case ax::mojom::Role::kArticle:
    case ax::mojom::Role::kComment:
    case ax::mojom::Role::kDisclosureTriangleGrouped:
    case ax::mojom::Role::kListItem:
    case ax::mojom::Role::kMenuItem:
    case ax::mojom::Role::kMenuItemRadio:
    case ax::mojom::Role::kTab:
    case ax::mojom::Role::kMenuItemCheckBox:
    case ax::mojom::Role::kTreeItem:
    case ax::mojom::Role::kListBoxOption:
    case ax::mojom::Role::kMenuListOption:
    case ax::mojom::Role::kRadioButton:
    case ax::mojom::Role::kTerm:
      DCHECK(!IsSetLike(role)) << "Role cannot be both item-like and set-like.";
      return true;
    default:
      return false;
  }
}

bool IsLikelyActiveDescendantRole(const ax::mojom::Role role) {
  switch (role) {
    case ax::mojom::Role::kButton:
    case ax::mojom::Role::kCell:
    case ax::mojom::Role::kCheckBox:
    case ax::mojom::Role::kComment:
    case ax::mojom::Role::kGridCell:
    case ax::mojom::Role::kListBoxOption:
    case ax::mojom::Role::kMenuItem:
    case ax::mojom::Role::kMenuItemCheckBox:
    case ax::mojom::Role::kMenuItemRadio:
    case ax::mojom::Role::kMenuListOption:
    case ax::mojom::Role::kRadioButton:
    case ax::mojom::Role::kRow:
    case ax::mojom::Role::kTab:
    case ax::mojom::Role::kToggleButton:
    case ax::mojom::Role::kTreeItem:
      return true;
    default:
      return false;
  }
}

bool IsLandmark(const ax::mojom::Role role) {
  switch (role) {
    case ax::mojom::Role::kBanner:
    case ax::mojom::Role::kComplementary:
    case ax::mojom::Role::kContentInfo:
    case ax::mojom::Role::kForm:
    case ax::mojom::Role::kMain:
    case ax::mojom::Role::kNavigation:
    case ax::mojom::Role::kRegion:
    case ax::mojom::Role::kSearch:
      return true;
    default:
      return false;
  }
}

bool IsLink(const ax::mojom::Role role) {
  switch (role) {
    case ax::mojom::Role::kDocBackLink:
    case ax::mojom::Role::kDocBiblioRef:
    case ax::mojom::Role::kDocGlossRef:
    case ax::mojom::Role::kDocNoteRef:
    case ax::mojom::Role::kLink:
      return true;
    default:
      return false;
  }
}

bool IsList(const ax::mojom::Role role) {
  switch (role) {
    case ax::mojom::Role::kDescriptionList:
    case ax::mojom::Role::kDocBibliography:
    case ax::mojom::Role::kList:
    case ax::mojom::Role::kListBox:
    case ax::mojom::Role::kListGrid:
      return true;
    default:
      return false;
  }
}

bool IsListItem(const ax::mojom::Role role) {
  switch (role) {
    case ax::mojom::Role::kDocBiblioEntry:
    case ax::mojom::Role::kDocEndnote:
    case ax::mojom::Role::kListBoxOption:
    case ax::mojom::Role::kListItem:
    case ax::mojom::Role::kTerm:
      return true;
    default:
      return false;
  }
}

bool IsMenuItem(ax::mojom::Role role) {
  switch (role) {
    case ax::mojom::Role::kMenuItem:
    case ax::mojom::Role::kMenuItemCheckBox:
    case ax::mojom::Role::kMenuItemRadio:
      return true;
    default:
      return false;
  }
}

bool IsMenuRelated(const ax::mojom::Role role) {
  switch (role) {
    case ax::mojom::Role::kMenu:
    case ax::mojom::Role::kMenuBar:
    case ax::mojom::Role::kMenuItem:
    case ax::mojom::Role::kMenuItemCheckBox:
    case ax::mojom::Role::kMenuItemRadio:
    case ax::mojom::Role::kMenuListOption:
    case ax::mojom::Role::kMenuListPopup:
      return true;
    default:
      return false;
  }
}

bool IsPlatformDocument(const ax::mojom::Role role) {
  // "ax::mojom::Role::kDocument" is excluded because it refers to nodes with
  // the ARIA document role. These are not at the root of an HTML document. They
  // can appear anywhere within an HTML document, but never at its root.
  switch (role) {
    case ax::mojom::Role::kPdfRoot:
    case ax::mojom::Role::kRootWebArea:
      return true;
    default:
      return false;
  }
}

bool IsPresentational(const ax::mojom::Role role) {
  switch (role) {
    case ax::mojom::Role::kNone:
      return true;
    default:
      return false;
  }
}

bool IsRadio(const ax::mojom::Role role) {
  switch (role) {
    case ax::mojom::Role::kRadioButton:
    case ax::mojom::Role::kMenuItemRadio:
      return true;
    default:
      return false;
  }
}

bool IsRangeValueSupported(const ax::mojom::Role role) {
  // https://www.w3.org/TR/wai-aria-1.1/#aria-valuenow
  // https://www.w3.org/TR/wai-aria-1.1/#aria-valuetext
  // Roles that support aria-valuetext / aria-valuenow
  switch (role) {
    case ax::mojom::Role::kMeter:
    case ax::mojom::Role::kProgressIndicator:
    case ax::mojom::Role::kScrollBar:
    case ax::mojom::Role::kSlider:
    case ax::mojom::Role::kSpinButton:
    case ax::mojom::Role::kSplitter:
      return true;
    default:
      return false;
  }
}

bool IsReadOnlySupported(const ax::mojom::Role role) {
  // https://www.w3.org/TR/wai-aria-1.1/#aria-readonly
  // Roles that support aria-readonly
  switch (role) {
    case ax::mojom::Role::kCheckBox:
    case ax::mojom::Role::kColorWell:
    case ax::mojom::Role::kComboBoxGrouping:
    case ax::mojom::Role::kComboBoxMenuButton:
    case ax::mojom::Role::kComboBoxSelect:
    case ax::mojom::Role::kDate:
    case ax::mojom::Role::kDateTime:
    case ax::mojom::Role::kGrid:
    case ax::mojom::Role::kGridCell:
    case ax::mojom::Role::kInputTime:
    case ax::mojom::Role::kListBox:
    case ax::mojom::Role::kMenuItemCheckBox:
    case ax::mojom::Role::kMenuItemRadio:
    case ax::mojom::Role::kMenuListPopup:
    case ax::mojom::Role::kRadioButton:
    case ax::mojom::Role::kRadioGroup:
    case ax::mojom::Role::kSearchBox:
    case ax::mojom::Role::kSlider:
    case ax::mojom::Role::kSpinButton:
    case ax::mojom::Role::kSwitch:
    case ax::mojom::Role::kTextField:
    case ax::mojom::Role::kTextFieldWithComboBox:
    case ax::mojom::Role::kTreeGrid:
      return true;
    // https://www.w3.org/TR/wai-aria-1.1/#columnheader
    // https://www.w3.org/TR/wai-aria-1.1/#rowheader
    // While the [columnheader|rowheader] role can be used in both interactive
    // grids and non-interactive tables, the use of aria-readonly and
    // aria-required is only applicable to interactive elements.
    // Therefore, [...] user agents SHOULD NOT expose either property to
    // assistive technologies unless the columnheader descends from a grid.
    case ax::mojom::Role::kCell:
    case ax::mojom::Role::kColumnHeader:
    case ax::mojom::Role::kMathMLTableCell:
    case ax::mojom::Role::kRowHeader:
      return false;
    default:
      return false;
  }
}

bool IsRootLike(ax::mojom::Role role) {
  if (IsDialog(role) || IsPlatformDocument(role))
    return true;

  switch (role) {
    case ax::mojom::Role::kDesktop:
    case ax::mojom::Role::kWindow:
      return true;
    default:
      return false;
  }
}

bool IsRowContainer(const ax::mojom::Role role) {
  switch (role) {
    case ax::mojom::Role::kGrid:
    case ax::mojom::Role::kListGrid:
    case ax::mojom::Role::kTable:
    case ax::mojom::Role::kTree:
    case ax::mojom::Role::kTreeGrid:
      return true;
    case ax::mojom::Role::kLayoutTable:
      return kExposeLayoutTableAsDataTable;
    default:
      return false;
  }
}

bool IsSection(const ax::mojom::Role role) {
  if (IsLandmark(role) || IsSelect(role))
    return true;

  switch (role) {
    case ax::mojom::Role::kAlert:
    case ax::mojom::Role::kAlertDialog:  // Subclass of kAlert.
    case ax::mojom::Role::kCell:
    case ax::mojom::Role::kColumnHeader:  // Subclass of kCell.
    case ax::mojom::Role::kDefinition:
    case ax::mojom::Role::kFeed:       // Subclass of kList.
    case ax::mojom::Role::kFigure:
    case ax::mojom::Role::kGrid:  // Subclass of kTable.
    case ax::mojom::Role::kGridCell:  // Subclass of kCell.
    case ax::mojom::Role::kGroup:
    case ax::mojom::Role::kImage:
    case ax::mojom::Role::kList:
    case ax::mojom::Role::kListItem:
    case ax::mojom::Role::kLog:
    case ax::mojom::Role::kMarquee:
    case ax::mojom::Role::kMath:
    case ax::mojom::Role::kMathMLMath:
    case ax::mojom::Role::kMathMLTableCell:
    case ax::mojom::Role::kNote:
    case ax::mojom::Role::kProgressIndicator:  // Subclass of kStatus.
    case ax::mojom::Role::kRow:                // Subclass of kGroup.
    case ax::mojom::Role::kRowHeader:          // Subclass of kCell.
    case ax::mojom::Role::kSection:
    case ax::mojom::Role::kStatus:
    case ax::mojom::Role::kTable:
    case ax::mojom::Role::kTabPanel:
    case ax::mojom::Role::kTerm:
    case ax::mojom::Role::kTimer:    // Subclass of kStatus.
    case ax::mojom::Role::kToolbar:  // Subclass of kGroup.
    case ax::mojom::Role::kTooltip:
    case ax::mojom::Role::kTreeItem:  // Subclass of kListItem.
      return true;
    default:
      return false;
  }
}

bool IsSectionhead(const ax::mojom::Role role) {
  switch (role) {
    case ax::mojom::Role::kColumnHeader:
    case ax::mojom::Role::kHeading:
    case ax::mojom::Role::kRowHeader:
    case ax::mojom::Role::kTab:
      return true;
    default:
      return false;
  }
}

bool IsSelect(const ax::mojom::Role role) {
  switch (role) {
    case ax::mojom::Role::kComboBoxGrouping:
    case ax::mojom::Role::kListBox:
    case ax::mojom::Role::kMenu:
    case ax::mojom::Role::kMenuBar:  // Subclass of kMenu.
    case ax::mojom::Role::kRadioGroup:
    case ax::mojom::Role::kTree:
    case ax::mojom::Role::kTreeGrid:  // Subclass of kTree.
      return true;
    default:
      return false;
  }
}

bool IsSelectElement(const ax::mojom::Role role) {
  // Depending on their "size" attribute, <select> elements come in two flavors:
  // the first appears like a list box and the second a specific combobox type.
  switch (role) {
    case ax::mojom::Role::kListBox:
    case ax::mojom::Role::kComboBoxSelect:
      return true;
    default:
      return false;
  }
}

bool IsSelectRequiredOrImplicit(const ax::mojom::Role role) {
  switch (role) {
    case ax::mojom::Role::kListBoxOption:
    case ax::mojom::Role::kMenuListOption:
    case ax::mojom::Role::kTab:
    case ax::mojom::Role::kTreeItem:
      return true;
    default:
      return false;
  }
}

bool IsSelectSupported(const ax::mojom::Role role) {
  switch (role) {
    case ax::mojom::Role::kGridCell:
    case ax::mojom::Role::kColumnHeader:
    case ax::mojom::Role::kListBoxOption:
    case ax::mojom::Role::kMathMLTableCell:
    case ax::mojom::Role::kMenuListOption:
    case ax::mojom::Role::kRow:
    case ax::mojom::Role::kRowHeader:
    case ax::mojom::Role::kTab:
    case ax::mojom::Role::kTreeItem:
      return true;
    default:
      return false;
  }
}

bool IsSetLike(const ax::mojom::Role role) {
  switch (role) {
    case ax::mojom::Role::kComboBoxSelect:
    case ax::mojom::Role::kDescriptionList:
    case ax::mojom::Role::kDocBibliography:
    case ax::mojom::Role::kFeed:
    case ax::mojom::Role::kGroup:
    case ax::mojom::Role::kList:
    case ax::mojom::Role::kListBox:
    case ax::mojom::Role::kListGrid:
    case ax::mojom::Role::kMenu:
    case ax::mojom::Role::kMenuBar:
    case ax::mojom::Role::kMenuListPopup:
    case ax::mojom::Role::kPopUpButton:
    case ax::mojom::Role::kRadioGroup:
    case ax::mojom::Role::kTabList:
    case ax::mojom::Role::kTree:
    case ax::mojom::Role::kTreeGrid:
      return true;
    default:
      return false;
  }
}

bool IsStaticList(const ax::mojom::Role role) {
  switch (role) {
    case ax::mojom::Role::kList:
    case ax::mojom::Role::kDescriptionList:
      return true;
    default:
      return false;
  }
}

bool IsStructure(const ax::mojom::Role role) {
  if (IsSection(role) || IsSectionhead(role) || IsPresentational(role))
    return true;

  switch (role) {
    case ax::mojom::Role::kApplication:
    case ax::mojom::Role::kDocument:
    case ax::mojom::Role::kArticle:  // Subclass of kDocument.
    case ax::mojom::Role::kRowGroup:
    case ax::mojom::Role::kSplitter:
    // Dpub roles.
    case ax::mojom::Role::kDocAbstract:
    case ax::mojom::Role::kDocAcknowledgments:
    case ax::mojom::Role::kDocAfterword:
    case ax::mojom::Role::kDocAppendix:
    case ax::mojom::Role::kDocBiblioEntry:
    case ax::mojom::Role::kDocBibliography:
    case ax::mojom::Role::kDocChapter:
    case ax::mojom::Role::kDocColophon:
    case ax::mojom::Role::kDocConclusion:
    case ax::mojom::Role::kDocCover:
    case ax::mojom::Role::kDocCredit:
    case ax::mojom::Role::kDocCredits:
    case ax::mojom::Role::kDocDedication:
    case ax::mojom::Role::kDocEndnote:
    case ax::mojom::Role::kDocEndnotes:
    case ax::mojom::Role::kDocEpigraph:
    case ax::mojom::Role::kDocEpilogue:
    case ax::mojom::Role::kDocErrata:
    case ax::mojom::Role::kDocExample:
    case ax::mojom::Role::kDocFootnote:
    case ax::mojom::Role::kDocForeword:
    case ax::mojom::Role::kDocGlossary:
    case ax::mojom::Role::kDocIndex:
    case ax::mojom::Role::kDocIntroduction:
    case ax::mojom::Role::kDocNotice:
    case ax::mojom::Role::kDocPageBreak:
    case ax::mojom::Role::kDocPageList:
    case ax::mojom::Role::kDocPart:
    case ax::mojom::Role::kDocPreface:
    case ax::mojom::Role::kDocPrologue:
    case ax::mojom::Role::kDocQna:
    case ax::mojom::Role::kDocSubtitle:
    case ax::mojom::Role::kDocTip:
    case ax::mojom::Role::kDocToc:
      return true;
    default:
      return false;
  }
}

bool IsTableColumn(ax::mojom::Role role) {
  return role == ax::mojom::Role::kColumn;
}

bool IsTableHeader(ax::mojom::Role role) {
  switch (role) {
    case ax::mojom::Role::kColumnHeader:
    case ax::mojom::Role::kRowHeader:
      return true;
    default:
      return false;
  }
}

bool IsTableItem(ax::mojom::Role role) {
  switch (role) {
    case ax::mojom::Role::kListBoxOption:
    case ax::mojom::Role::kListItem:
    case ax::mojom::Role::kTerm:
    case ax::mojom::Role::kTreeItem:
      return true;
    default:
      return IsCellOrTableHeader(role);
  }
}

#if BUILDFLAG(IS_ANDROID)
bool IsTableLike(const ax::mojom::Role role) {
  switch (role) {
    case ax::mojom::Role::kGrid:
    case ax::mojom::Role::kDescriptionList:
    case ax::mojom::Role::kList:
    case ax::mojom::Role::kListBox:
    case ax::mojom::Role::kListGrid:
    case ax::mojom::Role::kTable:
    case ax::mojom::Role::kTree:
    case ax::mojom::Role::kTreeGrid:
      return true;
    default:
      return false;
  }
}
#else
bool IsTableLike(const ax::mojom::Role role) {
  switch (role) {
    case ax::mojom::Role::kGrid:
    case ax::mojom::Role::kListGrid:
    case ax::mojom::Role::kTable:
    case ax::mojom::Role::kTreeGrid:
      return true;
    case ax::mojom::Role::kLayoutTable:
      return kExposeLayoutTableAsDataTable;
    default:
      return false;
  }
}
#endif

bool IsTableRow(ax::mojom::Role role) {
  switch (role) {
    case ax::mojom::Role::kRow:
      return true;
    case ax::mojom::Role::kLayoutTableRow:
      return kExposeLayoutTableAsDataTable;
    default:
      return false;
  }
}

bool IsText(ax::mojom::Role role) {
  switch (role) {
    case ax::mojom::Role::kInlineTextBox:
    case ax::mojom::Role::kLineBreak:
    case ax::mojom::Role::kStaticText:
      return true;
    default:
      return false;
  }
}

bool IsTextField(ax::mojom::Role role) {
  switch (role) {
    case ax::mojom::Role::kSearchBox:
    case ax::mojom::Role::kTextField:
    case ax::mojom::Role::kTextFieldWithComboBox:
      return true;
    default:
      return false;
  }
}

bool IsUIAEmbeddedObject(ax::mojom::Role role) {
  // A UIA embedded object "is any element that has non-textual boundaries such
  // as an image, hyperlink, table", etc. For more info, see
  // https://docs.microsoft.com/en-us/windows/win32/winauto/uiauto-textpattern-and-embedded-objects-overview.
  switch (role) {
    case ax::mojom::Role::kButton:
    case ax::mojom::Role::kCanvas:
    case ax::mojom::Role::kCell:
    case ax::mojom::Role::kCheckBox:
    case ax::mojom::Role::kColorWell:
    case ax::mojom::Role::kColumn:
    case ax::mojom::Role::kColumnHeader:
    case ax::mojom::Role::kComboBoxGrouping:
    case ax::mojom::Role::kComboBoxMenuButton:
    case ax::mojom::Role::kComboBoxSelect:
    case ax::mojom::Role::kDate:
    case ax::mojom::Role::kDateTime:
    case ax::mojom::Role::kDescriptionList:
    case ax::mojom::Role::kDisclosureTriangle:
    case ax::mojom::Role::kDisclosureTriangleGrouped:
    case ax::mojom::Role::kDocBackLink:
    case ax::mojom::Role::kDocBiblioEntry:
    case ax::mojom::Role::kDocBiblioRef:
    case ax::mojom::Role::kDocBibliography:
    case ax::mojom::Role::kDocCover:
    case ax::mojom::Role::kDocEndnote:
    case ax::mojom::Role::kDocGlossRef:
    case ax::mojom::Role::kDocNoteRef:
    case ax::mojom::Role::kForm:
    case ax::mojom::Role::kGraphicsSymbol:
    case ax::mojom::Role::kGrid:
    case ax::mojom::Role::kGridCell:
    case ax::mojom::Role::kIframe:
    case ax::mojom::Role::kIframePresentational:
    case ax::mojom::Role::kImage:
    case ax::mojom::Role::kInputTime:
    case ax::mojom::Role::kLink:
    case ax::mojom::Role::kList:
    case ax::mojom::Role::kListBox:
    case ax::mojom::Role::kListBoxOption:
    case ax::mojom::Role::kListGrid:
    case ax::mojom::Role::kListItem:
    case ax::mojom::Role::kMenu:
    case ax::mojom::Role::kMenuBar:
    case ax::mojom::Role::kMenuItem:
    case ax::mojom::Role::kMenuItemCheckBox:
    case ax::mojom::Role::kMenuItemRadio:
    case ax::mojom::Role::kMenuListOption:
    case ax::mojom::Role::kMenuListPopup:
    case ax::mojom::Role::kMeter:
    case ax::mojom::Role::kPdfActionableHighlight:
    case ax::mojom::Role::kPopUpButton:
    case ax::mojom::Role::kProgressIndicator:
    case ax::mojom::Role::kRadioButton:
    case ax::mojom::Role::kRadioGroup:
    case ax::mojom::Role::kRow:
    case ax::mojom::Role::kRowHeader:
    case ax::mojom::Role::kScrollBar:
    case ax::mojom::Role::kSearchBox:
    case ax::mojom::Role::kSlider:
    case ax::mojom::Role::kSpinButton:
    case ax::mojom::Role::kSplitter:
    case ax::mojom::Role::kSvgRoot:
    case ax::mojom::Role::kSwitch:
    case ax::mojom::Role::kTab:
    case ax::mojom::Role::kTabList:
    case ax::mojom::Role::kTable:
    case ax::mojom::Role::kTerm:
    case ax::mojom::Role::kTextField:
    case ax::mojom::Role::kTextFieldWithComboBox:
    case ax::mojom::Role::kToggleButton:
    case ax::mojom::Role::kToolbar:
    case ax::mojom::Role::kTree:
    case ax::mojom::Role::kTreeGrid:
    case ax::mojom::Role::kTreeItem:
    case ax::mojom::Role::kVideo:
      return true;
    case ax::mojom::Role::kLayoutTableCell:
      return kExposeLayoutTableAsDataTable;
    default:
      return false;
  }
}

bool IsUIATableLike(ax::mojom::Role role) {
  if (role == ax::mojom::Role::kLayoutTable)
    return false;

  return IsTableLike(role);
}

bool IsUIACellOrTableHeader(ax::mojom::Role role) {
  if (role == ax::mojom::Role::kLayoutTableCell)
    return false;

  return IsCellOrTableHeader(role);
}

bool IsWindow(ax::mojom::Role role) {
  switch (role) {
    case ax::mojom::Role::kWindow:
      return true;
    default:
      return false;
  }
}

bool ShouldHaveReadonlyStateByDefault(const ax::mojom::Role role) {
  switch (role) {
    case ax::mojom::Role::kArticle:
    case ax::mojom::Role::kDefinition:
    case ax::mojom::Role::kDescriptionList:
    case ax::mojom::Role::kDocument:
    case ax::mojom::Role::kGraphicsDocument:
    case ax::mojom::Role::kImage:
    case ax::mojom::Role::kList:
    case ax::mojom::Role::kListItem:
    case ax::mojom::Role::kPdfRoot:
    case ax::mojom::Role::kProgressIndicator:
    case ax::mojom::Role::kRootWebArea:
    case ax::mojom::Role::kTerm:
    case ax::mojom::Role::kTimer:
    case ax::mojom::Role::kToolbar:
    case ax::mojom::Role::kTooltip:
      return true;

    case ax::mojom::Role::kGrid:
      // TODO(aleventhal) this changed between ARIA 1.0 and 1.1.
      // We need to determine whether grids/treegrids should really be readonly
      // or editable by default.
    default:
      return false;
  }
}

bool SupportsExpandCollapse(const ax::mojom::Role role) {
  switch (role) {
    case ax::mojom::Role::kComboBoxGrouping:
    case ax::mojom::Role::kComboBoxMenuButton:
    case ax::mojom::Role::kComboBoxSelect:
    case ax::mojom::Role::kDisclosureTriangle:
    case ax::mojom::Role::kDisclosureTriangleGrouped:
    case ax::mojom::Role::kTextFieldWithComboBox:
    case ax::mojom::Role::kTreeItem:
      return true;
    default:
      return false;
  }
}

bool SupportsHierarchicalLevel(const ax::mojom::Role role) {
  switch (role) {
    case ax::mojom::Role::kComment:
    case ax::mojom::Role::kListItem:
    case ax::mojom::Role::kRow:
    case ax::mojom::Role::kTreeItem:
      return true;
    default:
      return false;
  }
}

bool SupportsOrientation(const ax::mojom::Role role) {
  switch (role) {
    case ax::mojom::Role::kComboBoxGrouping:
    case ax::mojom::Role::kComboBoxMenuButton:
    case ax::mojom::Role::kListBox:
    case ax::mojom::Role::kMenu:
    case ax::mojom::Role::kMenuBar:
    case ax::mojom::Role::kRadioGroup:
    case ax::mojom::Role::kScrollBar:
    case ax::mojom::Role::kSlider:
    case ax::mojom::Role::kSplitter:
    case ax::mojom::Role::kTabList:
    case ax::mojom::Role::kToolbar:
    case ax::mojom::Role::kTreeGrid:
    case ax::mojom::Role::kTree:
      return true;
    default:
      return false;
  }
}

bool SupportsRequired(const ax::mojom::Role role) {
  switch (role) {
    case ax::mojom::Role::kButton:        // Used by the file upload button.
    case ax::mojom::Role::kColumnHeader:  // Used only for gridheaders.
    case ax::mojom::Role::kComboBoxGrouping:
    case ax::mojom::Role::kCheckBox:
    case ax::mojom::Role::kDate:
    case ax::mojom::Role::kDateTime:
    case ax::mojom::Role::kGridCell:
    case ax::mojom::Role::kInputTime:
    case ax::mojom::Role::kListBox:
    case ax::mojom::Role::kRadioButton:
    case ax::mojom::Role::kRadioGroup:
    case ax::mojom::Role::kRowHeader:
    case ax::mojom::Role::kSearchBox:
    case ax::mojom::Role::kSlider:
    case ax::mojom::Role::kSpinButton:
    case ax::mojom::Role::kSwitch:
    case ax::mojom::Role::kTextField:
    case ax::mojom::Role::kTextFieldWithComboBox:
    case ax::mojom::Role::kToggleButton:
    case ax::mojom::Role::kTree:
    case ax::mojom::Role::kTreeGrid:
      return true;
    default:
      return false;
  }
}

bool SupportsToggle(const ax::mojom::Role role) {
  switch (role) {
    case ax::mojom::Role::kCheckBox:
    case ax::mojom::Role::kMenuItemCheckBox:
    case ax::mojom::Role::kSwitch:
    case ax::mojom::Role::kToggleButton:
      return true;
    default:
      return false;
  }
}

bool IsPlainContentElement(ax::mojom::Role role) {
  switch (role) {
    case ax::mojom::Role::kInlineTextBox:
    case ax::mojom::Role::kLineBreak:
    case ax::mojom::Role::kStaticText:
    case ax::mojom::Role::kCanvas:
    case ax::mojom::Role::kDocCover:
    case ax::mojom::Role::kGraphicsSymbol:
    case ax::mojom::Role::kImage:
    case ax::mojom::Role::kSvgRoot:
    case ax::mojom::Role::kGenericContainer:
    case ax::mojom::Role::kNone:
    case ax::mojom::Role::kGroup:
      return true;
    default:
      return false;
  }
}

bool SupportsArrowKeysForExpandCollapse(const ax::mojom::Role role) {
  // TODO(accessibility): Investigate if other roles should implement this
  // pattern.
  return role == ax::mojom::Role::kTreeItem;
}

}  // namespace ui
