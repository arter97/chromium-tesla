// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/css/resolver/style_cascade.h"

#include <bit>

#include "third_party/blink/renderer/core/animation/css/css_animations.h"
#include "third_party/blink/renderer/core/animation/css_interpolation_environment.h"
#include "third_party/blink/renderer/core/animation/css_interpolation_types_map.h"
#include "third_party/blink/renderer/core/animation/invalidatable_interpolation.h"
#include "third_party/blink/renderer/core/animation/property_handle.h"
#include "third_party/blink/renderer/core/animation/transition_interpolation.h"
#include "third_party/blink/renderer/core/css/css_appearance_auto_base_select_value_pair.h"
#include "third_party/blink/renderer/core/css/css_cyclic_variable_value.h"
#include "third_party/blink/renderer/core/css/css_flip_revert_value.h"
#include "third_party/blink/renderer/core/css/css_font_selector.h"
#include "third_party/blink/renderer/core/css/css_invalid_variable_value.h"
#include "third_party/blink/renderer/core/css/css_math_function_value.h"
#include "third_party/blink/renderer/core/css/css_numeric_literal_value.h"
#include "third_party/blink/renderer/core/css/css_pending_substitution_value.h"
#include "third_party/blink/renderer/core/css/css_syntax_string_parser.h"
#include "third_party/blink/renderer/core/css/css_unparsed_declaration_value.h"
#include "third_party/blink/renderer/core/css/css_unset_value.h"
#include "third_party/blink/renderer/core/css/css_variable_data.h"
#include "third_party/blink/renderer/core/css/document_style_environment_variables.h"
#include "third_party/blink/renderer/core/css/parser/css_parser_local_context.h"
#include "third_party/blink/renderer/core/css/parser/css_parser_token_stream.h"
#include "third_party/blink/renderer/core/css/parser/css_property_parser.h"
#include "third_party/blink/renderer/core/css/parser/css_tokenizer.h"
#include "third_party/blink/renderer/core/css/parser/css_variable_parser.h"
#include "third_party/blink/renderer/core/css/properties/css_parsing_utils.h"
#include "third_party/blink/renderer/core/css/properties/css_property.h"
#include "third_party/blink/renderer/core/css/properties/css_property_ref.h"
#include "third_party/blink/renderer/core/css/properties/longhands.h"
#include "third_party/blink/renderer/core/css/property_bitsets.h"
#include "third_party/blink/renderer/core/css/property_registry.h"
#include "third_party/blink/renderer/core/css/resolver/cascade_expansion-inl.h"
#include "third_party/blink/renderer/core/css/resolver/cascade_expansion.h"
#include "third_party/blink/renderer/core/css/resolver/cascade_interpolations.h"
#include "third_party/blink/renderer/core/css/resolver/cascade_resolver.h"
#include "third_party/blink/renderer/core/css/resolver/scoped_style_resolver.h"
#include "third_party/blink/renderer/core/css/resolver/style_builder.h"
#include "third_party/blink/renderer/core/css/resolver/style_builder_converter.h"
#include "third_party/blink/renderer/core/css/resolver/style_resolver_state.h"
#include "third_party/blink/renderer/core/css/style_engine.h"
#include "third_party/blink/renderer/core/css/try_value_flips.h"
#include "third_party/blink/renderer/core/dom/shadow_root.h"
#include "third_party/blink/renderer/core/frame/web_feature.h"
#include "third_party/blink/renderer/core/html/forms/html_select_element.h"
#include "third_party/blink/renderer/core/html/html_element.h"
#include "third_party/blink/renderer/core/style/computed_style.h"
#include "third_party/blink/renderer/core/style_property_shorthand.h"
#include "third_party/blink/renderer/platform/instrumentation/use_counter.h"
#include "third_party/blink/renderer/platform/wtf/math_extras.h"
#include "third_party/blink/renderer/platform/wtf/text/string_builder.h"

namespace blink {

namespace {

AtomicString ConsumeVariableName(CSSParserTokenStream& stream) {
  stream.ConsumeWhitespace();
  CSSParserToken ident_token = stream.ConsumeIncludingWhitespaceRaw();
  DCHECK_EQ(ident_token.GetType(), kIdentToken);
  return ident_token.Value().ToAtomicString();
}

bool ConsumeComma(CSSParserTokenStream& stream) {
  if (stream.Peek().GetType() == kCommaToken) {
    stream.ConsumeRaw();
    return true;
  }
  return false;
}

const CSSValue* Parse(const CSSProperty& property,
                      CSSParserTokenStream& stream,
                      const CSSParserContext* context) {
  return CSSPropertyParser::ParseSingleValue(property.PropertyID(), stream,
                                             context);
}

const CSSValue* ValueAt(const MatchResult& result, uint32_t position) {
  wtf_size_t matched_properties_index = DecodeMatchedPropertiesIndex(position);
  wtf_size_t declaration_index = DecodeDeclarationIndex(position);
  const MatchedPropertiesVector& vector = result.GetMatchedProperties();
  const CSSPropertyValueSet* set = vector[matched_properties_index].properties;
  return &set->PropertyAt(declaration_index).Value();
}

const TreeScope& TreeScopeAt(const MatchResult& result, uint32_t position) {
  wtf_size_t matched_properties_index = DecodeMatchedPropertiesIndex(position);
  const MatchedProperties& properties =
      result.GetMatchedProperties()[matched_properties_index];
  DCHECK_EQ(properties.types_.origin, CascadeOrigin::kAuthor);
  return result.ScopeFromTreeOrder(properties.types_.tree_order);
}

const CSSValue* EnsureScopedValue(const Document& document,
                                  const MatchResult& match_result,
                                  CascadePriority priority,
                                  const CSSValue* value) {
  CascadeOrigin origin = priority.GetOrigin();
  const TreeScope* tree_scope{nullptr};
  if (origin == CascadeOrigin::kAuthor) {
    tree_scope = &TreeScopeAt(match_result, priority.GetPosition());
  } else if (origin == CascadeOrigin::kAuthorPresentationalHint) {
    tree_scope = &document;
  }
  return &value->EnsureScopedValue(tree_scope);
}

PropertyHandle ToPropertyHandle(const CSSProperty& property,
                                CascadePriority priority) {
  uint32_t position = priority.GetPosition();
  CSSPropertyID id = DecodeInterpolationPropertyID(position);
  if (id == CSSPropertyID::kVariable) {
    DCHECK(IsA<CustomProperty>(property));
    return PropertyHandle(property.GetPropertyNameAtomicString());
  }
  return PropertyHandle(CSSProperty::Get(id),
                        DecodeIsPresentationAttribute(position));
}

// https://drafts.csswg.org/css-cascade-4/#default
CascadeOrigin TargetOriginForRevert(CascadeOrigin origin) {
  switch (origin) {
    case CascadeOrigin::kNone:
    case CascadeOrigin::kTransition:
      NOTREACHED_IN_MIGRATION();
      return CascadeOrigin::kNone;
    case CascadeOrigin::kUserAgent:
      return CascadeOrigin::kNone;
    case CascadeOrigin::kUser:
      return CascadeOrigin::kUserAgent;
    case CascadeOrigin::kAuthorPresentationalHint:
    case CascadeOrigin::kAuthor:
    case CascadeOrigin::kAnimation:
      return CascadeOrigin::kUser;
  }
}

CSSPropertyID UnvisitedID(CSSPropertyID id) {
  if (id == CSSPropertyID::kVariable) {
    return id;
  }
  const CSSProperty& property = CSSProperty::Get(id);
  if (!property.IsVisited()) {
    return id;
  }
  return property.GetUnvisitedProperty()->PropertyID();
}

bool IsInterpolation(CascadePriority priority) {
  switch (priority.GetOrigin()) {
    case CascadeOrigin::kAnimation:
    case CascadeOrigin::kTransition:
      return true;
    case CascadeOrigin::kNone:
    case CascadeOrigin::kUserAgent:
    case CascadeOrigin::kUser:
    case CascadeOrigin::kAuthorPresentationalHint:
    case CascadeOrigin::kAuthor:
      return false;
  }
}

#if DCHECK_IS_ON()

bool HasUnresolvedReferences(CSSParserTokenRange range) {
  while (!range.AtEnd()) {
    switch (range.Consume().FunctionId()) {
      case CSSValueID::kVar:
      case CSSValueID::kEnv:
        return true;
      case CSSValueID::kArg:
        return RuntimeEnabledFeatures::CSSFunctionsEnabled();
      default:
        continue;
    }
  }
  return false;
}

#endif  // DCHECK_IS_ON()

}  // namespace

MatchResult& StyleCascade::MutableMatchResult() {
  DCHECK(!generation_) << "Apply has already been called";
  needs_match_result_analyze_ = true;
  return match_result_;
}

void StyleCascade::AddInterpolations(const ActiveInterpolationsMap* map,
                                     CascadeOrigin origin) {
  DCHECK(map);
  needs_interpolations_analyze_ = true;
  interpolations_.Add(map, origin);
}

void StyleCascade::Apply(CascadeFilter filter) {
  AnalyzeIfNeeded();
  ProcessPendingSignals();

  // Invisible rules are not supposed to have any effect on the observable
  // result, so we cascade again if any invisible rules were seen.
  //
  // We do this *after* processing signals, because some of the signals may
  // come from invisible rules.
  if (has_invisible_rules_) {
    allow_invisible_rules_ = false;
    Reanalyze();
  }

  state_.UpdateLengthConversionData();

  CascadeResolver resolver(filter, ++generation_);

  ApplyCascadeAffecting(resolver);

  ApplyHighPriority(resolver);
  state_.UpdateFont();

  if (map_.NativeBitset().Has(CSSPropertyID::kLineHeight)) {
    LookupAndApply(GetCSSPropertyLineHeight(), resolver);
  }
  state_.UpdateLineHeight();

  ApplyWideOverlapping(resolver);

  ApplyMatchResult(resolver);
  ApplyInterpolations(resolver);

  // These three flags are only used if HasAppearance() is set
  // (they are used for knowing whether appearance: auto is to be overridden),
  // but we compute them nevertheless, to avoid suddenly having to compute them
  // after-the-fact if inline style is updated incrementally.
  if (resolver.AuthorFlags() & CSSProperty::kBackground) {
    state_.StyleBuilder().SetHasAuthorBackground();
  }
  if (resolver.AuthorFlags() & CSSProperty::kBorder) {
    state_.StyleBuilder().SetHasAuthorBorder();
  }
  if (resolver.AuthorFlags() & CSSProperty::kBorderRadius) {
    state_.StyleBuilder().SetHasAuthorBorderRadius();
  }

  if ((state_.InsideLink() != EInsideLink::kInsideVisitedLink &&
       (resolver.AuthorFlags() & CSSProperty::kHighlightColors)) ||
      (state_.InsideLink() == EInsideLink::kInsideVisitedLink &&
       (resolver.AuthorFlags() & CSSProperty::kVisitedHighlightColors))) {
    state_.StyleBuilder().SetHasAuthorHighlightColors();
  }

  if (resolver.Flags() & CSSProperty::kAnimation) {
    state_.StyleBuilder().SetCanAffectAnimations();
  }
  if (resolver.RejectedFlags() & CSSProperty::kLegacyOverlapping) {
    state_.SetRejectedLegacyOverlapping();
  }

  // TOOD(crbug.com/1334570):
  //
  // Count applied H1 font-size from html.css UA stylesheet where H1 is inside
  // a sectioning element matching selectors like:
  //
  // :-webkit-any(article,aside,nav,section) h1 { ... }
  //
  if (!state_.GetElement().HasTagName(html_names::kH1Tag)) {
    return;
  }
  if (CascadePriority* priority =
          map_.Find(GetCSSPropertyFontSize().GetCSSPropertyName())) {
    if (priority->GetOrigin() != CascadeOrigin::kUserAgent) {
      return;
    }
    const CSSValue* value = ValueAt(match_result_, priority->GetPosition());
    if (const auto* numeric = DynamicTo<CSSNumericLiteralValue>(value)) {
      DCHECK(numeric->GetType() == CSSNumericLiteralValue::UnitType::kEms);
      if (numeric->DoubleValue() != 2.0) {
        CountUse(WebFeature::kH1UserAgentFontSizeInSectionApplied);
      }
    }
  }
}

std::unique_ptr<CSSBitset> StyleCascade::GetImportantSet() {
  AnalyzeIfNeeded();
  if (!map_.HasImportant()) {
    return nullptr;
  }
  auto set = std::make_unique<CSSBitset>();
  for (CSSPropertyID id : map_.NativeBitset()) {
    // We use the unvisited ID because visited/unvisited colors are currently
    // interpolated together.
    // TODO(crbug.com/1062217): Interpolate visited colors separately
    set->Or(UnvisitedID(id), map_.At(CSSPropertyName(id)).IsImportant());
  }
  return set;
}

void StyleCascade::Reset() {
  map_.Reset();
  match_result_.Reset();
  interpolations_.Reset();
  generation_ = 0;
  depends_on_cascade_affecting_property_ = false;
  allow_invisible_rules_ = true;
  has_invisible_rules_ = false;
}

const CSSValue* StyleCascade::Resolve(const CSSPropertyName& name,
                                      const CSSValue& value,
                                      CascadeOrigin origin,
                                      CascadeResolver& resolver) {
  CSSPropertyRef ref(name, state_.GetDocument());

  const CSSValue* resolved = Resolve(ResolveSurrogate(ref.GetProperty()), value,
                                     CascadePriority(origin), origin, resolver);

  DCHECK(resolved);

  // TODO(crbug.com/1185745): Cycles in animations get special handling by our
  // implementation. This is not per spec, but the correct behavior is not
  // defined at the moment.
  if (resolved->IsCyclicVariableValue()) {
    return nullptr;
  }

  // TODO(crbug.com/1185745): We should probably not return 'unset' for
  // properties where CustomProperty::SupportsGuaranteedInvalid return true.
  if (resolved->IsInvalidVariableValue()) {
    return cssvalue::CSSUnsetValue::Create();
  }

  return resolved;
}

HeapHashMap<CSSPropertyName, Member<const CSSValue>>
StyleCascade::GetCascadedValues() const {
  DCHECK(!needs_match_result_analyze_);
  DCHECK(!needs_interpolations_analyze_);
  DCHECK_GE(generation_, 0);

  HeapHashMap<CSSPropertyName, Member<const CSSValue>> result;

  for (CSSPropertyID id : map_.NativeBitset()) {
    CSSPropertyName name(id);
    CascadePriority priority = map_.At(name);
    if (IsInterpolation(priority)) {
      continue;
    }
    if (!priority.HasOrigin()) {
      // Declarations added for explicit defaults (AddExplicitDefaults)
      // should not be observable.
      continue;
    }
    const CSSValue* cascaded = ValueAt(match_result_, priority.GetPosition());
    DCHECK(cascaded);
    result.Set(name, cascaded);
  }

  for (const auto& name : map_.GetCustomMap().Keys()) {
    CascadePriority priority = map_.At(CSSPropertyName(name));
    DCHECK(priority.HasOrigin());
    if (IsInterpolation(priority)) {
      continue;
    }
    const CSSValue* cascaded = ValueAt(match_result_, priority.GetPosition());
    DCHECK(cascaded);
    result.Set(CSSPropertyName(name), cascaded);
  }

  return result;
}

const CSSValue* StyleCascade::Resolve(StyleResolverState& state,
                                      const CSSPropertyName& name,
                                      const CSSValue& value) {
  STACK_UNINITIALIZED StyleCascade cascade(state);

  // Since the cascade map is empty, the CascadeResolver isn't important,
  // as there can be no cycles in an empty map. We just instantiate it to
  // satisfy the API.
  CascadeResolver resolver(CascadeFilter(), /* generation */ 0);

  // The origin is relevant for 'revert', but since the cascade map
  // is empty, there will be nothing to revert to regardless of the origin
  // We use kNone, because kAuthor (etc) imply that the `value` originates
  // from a location on the `MatchResult`, which is not the case.
  CascadeOrigin origin = CascadeOrigin::kNone;

  return cascade.Resolve(name, value, origin, resolver);
}

void StyleCascade::AnalyzeIfNeeded() {
  if (needs_match_result_analyze_) {
    AnalyzeMatchResult();
    needs_match_result_analyze_ = false;
  }
  if (needs_interpolations_analyze_) {
    AnalyzeInterpolations();
    needs_interpolations_analyze_ = false;
  }
}

void StyleCascade::AnalyzeMatchResult() {
  AddExplicitDefaults();

  int index = 0;
  for (const MatchedProperties& properties :
       match_result_.GetMatchedProperties()) {
    // We expect signals to be very rare, so to avoid the cost of checking
    // against Signal::kNone for every *declaration*, we check once for
    // every declaration *block* (i.e. rule), and then use a separate call to
    // ExpandCascade (via ExpandSignals) to handle the signals.
    if (Signal signal = static_cast<Signal>(properties.types_.signal);
        signal != Signal::kNone) {
      ExpandSignals(properties, index, signal);
    }
    if (properties.types_.is_invisible) {
      if (!allow_invisible_rules_) {
        ++index;
        continue;
      }
      has_invisible_rules_ = true;
    }

    ExpandCascade(
        properties, GetDocument(), index++,
        [this](CascadePriority cascade_priority,
               const AtomicString& custom_property_name) {
          map_.Add(custom_property_name, cascade_priority);
        },
        [this](CascadePriority cascade_priority, CSSPropertyID property_id) {
          if (kSurrogateProperties.Has(property_id)) {
            const CSSProperty& property =
                ResolveSurrogate(CSSProperty::Get(property_id));
            map_.Add(property.PropertyID(), cascade_priority);
          } else {
            map_.Add(property_id, cascade_priority);
          }
        });
  }
}

void StyleCascade::AnalyzeInterpolations() {
  const auto& entries = interpolations_.GetEntries();
  for (wtf_size_t i = 0; i < entries.size(); ++i) {
    for (const auto& active_interpolation : *entries[i].map) {
      auto name = active_interpolation.key.GetCSSPropertyName();
      uint32_t position = EncodeInterpolationPosition(
          name.Id(), i, active_interpolation.key.IsPresentationAttribute());
      CascadePriority priority(entries[i].origin,
                               /* important */ false,
                               /* tree_order */ 0,
                               /* is_inline_style */ false,
                               /* is_try_style */ false,
                               /* is_try_tactics_style */ false,
                               /* layer_order */ 0, position);

      CSSPropertyRef ref(name, GetDocument());
      DCHECK(ref.IsValid());

      if (name.IsCustomProperty()) {
        map_.Add(name.ToAtomicString(), priority);
      } else {
        const CSSProperty& property = ResolveSurrogate(ref.GetProperty());
        map_.Add(property.PropertyID(), priority);

        // Since an interpolation for an unvisited property also causes an
        // interpolation of the visited property, add the visited property to
        // the map as well.
        // TODO(crbug.com/1062217): Interpolate visited colors separately
        if (const CSSProperty* visited = property.GetVisitedProperty()) {
          map_.Add(visited->PropertyID(), priority);
        }
      }
    }
  }
}

// The implicit defaulting behavior of inherited properties is to take
// the value of the parent style [1]. However, we never reach
// Longhand::ApplyInherit for implicit defaults, which is needed to adjust
// Lengths with premultiplied zoom. Therefore, all inherited properties
// are instead explicitly defaulted [2] when the effective zoom has changed
// versus the parent zoom.
//
// [1] https://drafts.csswg.org/css-cascade/#defaulting
// [2] https://drafts.csswg.org/css-cascade/#defaulting-keywords
void StyleCascade::AddExplicitDefaults() {
  if (RuntimeEnabledFeatures::StandardizedBrowserZoomEnabled() &&
      effective_zoom_changed_) {
    // TODO(crbug.com/40946858): Generate a list from json5.
    //
    // At a glance, these inherited properties can contain lengths:
    //
    //   -webkit-border-horizontal-spacing
    //   -webkit-border-vertical-spacing
    //   -webkit-text-stroke-width
    //   letter-spacing
    //   line-height
    //   list-style-image
    //   stroke-dashoffset
    //   stroke-width
    //   text-indent
    //   text-shadow
    //   text-underline-offset
    //   word-spacing
    //
    // (And maybe more).
    map_.Add(CSSPropertyID::kLetterSpacing,
             CascadePriority(CascadeOrigin::kNone));
    map_.Add(CSSPropertyID::kLineHeight, CascadePriority(CascadeOrigin::kNone));
  }
}

void StyleCascade::Reanalyze() {
  map_.Reset();
  generation_ = 0;
  depends_on_cascade_affecting_property_ = false;
  has_invisible_rules_ = false;

  needs_match_result_analyze_ = true;
  needs_interpolations_analyze_ = true;
  AnalyzeIfNeeded();
}

void StyleCascade::ApplyCascadeAffecting(CascadeResolver& resolver) {
  // During the initial call to Analyze, we speculatively assume that the
  // direction/writing-mode inherited from the parent will be the final
  // direction/writing-mode. If either property ends up with another value,
  // our assumption was incorrect, and we have to Reanalyze with the correct
  // values on ComputedStyle.
  auto direction = state_.StyleBuilder().Direction();
  auto writing_mode = state_.StyleBuilder().GetWritingMode();
  // Similarly, we assume that the effective zoom of this element
  // is the same as the parent's effective zoom. If it isn't,
  // we re-cascade with explicit defaults inserted at CascadeOrigin::kNone.
  //
  // See also StyleCascade::AddExplicitDefaults.
  float effective_zoom = state_.StyleBuilder().EffectiveZoom();

  if (map_.NativeBitset().Has(CSSPropertyID::kDirection)) {
    LookupAndApply(GetCSSPropertyDirection(), resolver);
  }
  if (map_.NativeBitset().Has(CSSPropertyID::kWritingMode)) {
    LookupAndApply(GetCSSPropertyWritingMode(), resolver);
  }
  if (map_.NativeBitset().Has(CSSPropertyID::kZoom)) {
    LookupAndApply(GetCSSPropertyZoom(), resolver);
  }

  bool reanalyze = false;

  if (depends_on_cascade_affecting_property_) {
    if (direction != state_.StyleBuilder().Direction() ||
        writing_mode != state_.StyleBuilder().GetWritingMode()) {
      reanalyze = true;
    }
  }
  if (effective_zoom != state_.StyleBuilder().EffectiveZoom()) {
    effective_zoom_changed_ = true;
    reanalyze = true;
  }

  if (reanalyze) {
    Reanalyze();
  }
}

void StyleCascade::ApplyHighPriority(CascadeResolver& resolver) {
  uint64_t bits = map_.HighPriorityBits();

  while (bits) {
    int i = std::countr_zero(bits);
    bits &= bits - 1;  // Clear the lowest bit.
    LookupAndApply(CSSProperty::Get(ConvertToCSSPropertyID(i)), resolver);
  }
}

void StyleCascade::ApplyWideOverlapping(CascadeResolver& resolver) {
  // Overlapping properties are handled as follows:
  //
  // 1. Apply the "wide" longhand which represents the entire computed value
  //    first. This is not always the non-legacy property,
  //    e.g.-webkit-border-image is one such longhand.
  // 2. For the other overlapping longhands (each of which represent a *part*
  //    of that computed value), *skip* applying that longhand if the wide
  //    longhand has a higher priority.
  //
  // This allows us to always apply the "wide" longhand in a fixed order versus
  // the other overlapping longhands, but still produce the same result as if
  // everything was applied in the order the properties were specified.

  // Skip `property` if its priority is lower than the incoming priority.
  // Skipping basically means pretending it's already applied by setting the
  // generation.
  auto maybe_skip = [this, &resolver](const CSSProperty& property,
                                      CascadePriority priority) {
    if (CascadePriority* p = map_.Find(property.GetCSSPropertyName())) {
      if (*p < priority) {
        *p = CascadePriority(*p, resolver.generation_);
      }
    }
  };

  const CSSProperty& webkit_border_image = GetCSSPropertyWebkitBorderImage();
  if (!resolver.filter_.Rejects(webkit_border_image)) {
    if (const CascadePriority* priority =
            map_.Find(webkit_border_image.GetCSSPropertyName())) {
      LookupAndApply(webkit_border_image, resolver);

      const auto& shorthand = borderImageShorthand();
      const CSSProperty** longhands = shorthand.properties();
      for (unsigned i = 0; i < shorthand.length(); ++i) {
        maybe_skip(*longhands[i], *priority);
      }
    }
  }

  const CSSProperty& perspective_origin = GetCSSPropertyPerspectiveOrigin();
  if (!resolver.filter_.Rejects(perspective_origin)) {
    if (const CascadePriority* priority =
            map_.Find(perspective_origin.GetCSSPropertyName())) {
      LookupAndApply(perspective_origin, resolver);
      maybe_skip(GetCSSPropertyWebkitPerspectiveOriginX(), *priority);
      maybe_skip(GetCSSPropertyWebkitPerspectiveOriginY(), *priority);
    }
  }

  const CSSProperty& transform_origin = GetCSSPropertyTransformOrigin();
  if (!resolver.filter_.Rejects(transform_origin)) {
    if (const CascadePriority* priority =
            map_.Find(transform_origin.GetCSSPropertyName())) {
      LookupAndApply(transform_origin, resolver);
      maybe_skip(GetCSSPropertyWebkitTransformOriginX(), *priority);
      maybe_skip(GetCSSPropertyWebkitTransformOriginY(), *priority);
      maybe_skip(GetCSSPropertyWebkitTransformOriginZ(), *priority);
    }
  }

  // vertical-align will become a shorthand in the future - in order to
  // mitigate the forward compat risk, skip the baseline-source longhand.
  const CSSProperty& vertical_align = GetCSSPropertyVerticalAlign();
  if (!resolver.filter_.Rejects(vertical_align)) {
    if (const CascadePriority* priority =
            map_.Find(vertical_align.GetCSSPropertyName())) {
      LookupAndApply(vertical_align, resolver);
      maybe_skip(GetCSSPropertyBaselineSource(), *priority);
    }
  }
}

// Go through all properties that were found during the analyze phase
// (e.g. in AnalyzeMatchResult()) and actually apply them. We need to do this
// in a second phase so that we know which ones actually won the cascade
// before we start applying, as some properties can affect others.
void StyleCascade::ApplyMatchResult(CascadeResolver& resolver) {
  // All the high-priority properties were dealt with in ApplyHighPriority(),
  // so we don't need to look at them again. (That would be a no-op due to
  // the generation check below, but it's cheaper just to mask them out
  // entirely.)
  for (auto it = map_.NativeBitset().BeginAfterHighPriority();
       it != map_.NativeBitset().end(); ++it) {
    CSSPropertyID id = *it;
    CascadePriority* p = map_.FindKnownToExist(id);
    const CascadePriority priority = *p;
    if (priority.GetGeneration() >= resolver.generation_) {
      // Already applied this generation.
      // Also checked in LookupAndApplyDeclaration,
      // but done here to get a fast exit.
      continue;
    }
    if (IsInterpolation(priority)) {
      continue;
    }

    const CSSProperty& property = CSSProperty::Get(id);
    if (resolver.Rejects(property)) {
      continue;
    }
    LookupAndApplyDeclaration(property, p, resolver);
  }

  for (auto& [name, priority_list] : map_.GetCustomMap()) {
    CascadePriority* p = &map_.Top(priority_list);
    CascadePriority priority = *p;
    if (priority.GetGeneration() >= resolver.generation_) {
      continue;
    }
    if (IsInterpolation(priority)) {
      continue;
    }

    CustomProperty property(name, GetDocument());
    if (resolver.Rejects(property)) {
      continue;
    }
    LookupAndApplyDeclaration(property, p, resolver);
  }
}

void StyleCascade::ApplyInterpolations(CascadeResolver& resolver) {
  const auto& entries = interpolations_.GetEntries();
  for (wtf_size_t i = 0; i < entries.size(); ++i) {
    const auto& entry = entries[i];
    ApplyInterpolationMap(*entry.map, entry.origin, i, resolver);
  }
}

void StyleCascade::ApplyInterpolationMap(const ActiveInterpolationsMap& map,
                                         CascadeOrigin origin,
                                         size_t index,
                                         CascadeResolver& resolver) {
  for (const auto& entry : map) {
    auto name = entry.key.GetCSSPropertyName();
    uint32_t position = EncodeInterpolationPosition(
        name.Id(), index, entry.key.IsPresentationAttribute());
    CascadePriority priority(origin,
                             /* important */ false,
                             /* tree_order */ 0,
                             /* is_inline_style */ false,
                             /* is_try_style */ false,
                             /* is_try_tactics_style */ false,
                             /* layer_order */ 0, position);
    priority = CascadePriority(priority, resolver.generation_);

    CSSPropertyRef ref(name, GetDocument());
    if (resolver.Rejects(ref.GetProperty())) {
      continue;
    }

    const CSSProperty& property = ResolveSurrogate(ref.GetProperty());

    CascadePriority* p = map_.Find(property.GetCSSPropertyName());
    if (!p || *p >= priority) {
      continue;
    }
    *p = priority;

    ApplyInterpolation(property, priority, *entry.value, resolver);
  }
}

void StyleCascade::ApplyInterpolation(
    const CSSProperty& property,
    CascadePriority priority,
    const ActiveInterpolations& interpolations,
    CascadeResolver& resolver) {
  DCHECK(!property.IsSurrogate());

  CSSInterpolationTypesMap map(state_.GetDocument().GetPropertyRegistry(),
                               state_.GetDocument());
  CSSInterpolationEnvironment environment(map, state_, this, &resolver);

  const Interpolation& interpolation = *interpolations.front();
  if (IsA<InvalidatableInterpolation>(interpolation)) {
    InvalidatableInterpolation::ApplyStack(interpolations, environment);
  } else {
    To<TransitionInterpolation>(interpolation).Apply(environment);
  }

  // Applying a color property interpolation will also unconditionally apply
  // the -internal-visited- counterpart (see CSSColorInterpolationType::
  // ApplyStandardPropertyValue). To make sure !important rules in :visited
  // selectors win over animations, we re-apply the -internal-visited property
  // if its priority is higher.
  //
  // TODO(crbug.com/1062217): Interpolate visited colors separately
  if (const CSSProperty* visited = property.GetVisitedProperty()) {
    CascadePriority* visited_priority =
        map_.Find(visited->GetCSSPropertyName());
    if (visited_priority && priority < *visited_priority) {
      DCHECK(visited_priority->IsImportant());
      // Resetting generation to zero makes it possible to apply the
      // visited property again.
      *visited_priority = CascadePriority(*visited_priority, 0);
      LookupAndApply(*visited, resolver);
    }
  }
}

void StyleCascade::LookupAndApply(const CSSPropertyName& name,
                                  CascadeResolver& resolver) {
  CSSPropertyRef ref(name, state_.GetDocument());
  DCHECK(ref.IsValid());
  LookupAndApply(ref.GetProperty(), resolver);
}

void StyleCascade::LookupAndApply(const CSSProperty& property,
                                  CascadeResolver& resolver) {
  DCHECK(!property.IsSurrogate());

  CSSPropertyName name = property.GetCSSPropertyName();
  DCHECK(!resolver.IsLocked(property));

  CascadePriority* priority = map_.Find(name);
  if (!priority) {
    return;
  }

  if (resolver.Rejects(property)) {
    return;
  }

  LookupAndApplyValue(property, priority, resolver);
}

void StyleCascade::LookupAndApplyValue(const CSSProperty& property,
                                       CascadePriority* priority,
                                       CascadeResolver& resolver) {
  DCHECK(!property.IsSurrogate());

  if (priority->GetOrigin() < CascadeOrigin::kAnimation) {
    LookupAndApplyDeclaration(property, priority, resolver);
  } else if (priority->GetOrigin() >= CascadeOrigin::kAnimation) {
    LookupAndApplyInterpolation(property, priority, resolver);
  }
}

void StyleCascade::LookupAndApplyDeclaration(const CSSProperty& property,
                                             CascadePriority* priority,
                                             CascadeResolver& resolver) {
  if (priority->GetGeneration() >= resolver.generation_) {
    // Already applied this generation.
    return;
  }
  *priority = CascadePriority(*priority, resolver.generation_);
  DCHECK(!property.IsSurrogate());
  DCHECK(priority->GetOrigin() < CascadeOrigin::kAnimation);
  CascadeOrigin origin = priority->GetOrigin();
  // Values at CascadeOrigin::kNone are used for explicit defaulting,
  // see StyleCascade::AddExplicitDefaults.
  const CSSValue* value = (origin == CascadeOrigin::kNone)
                              ? cssvalue::CSSUnsetValue::Create()
                              : ValueAt(match_result_, priority->GetPosition());
  DCHECK(value);
  value = Resolve(property, *value, *priority, origin, resolver);
  DCHECK(IsA<CustomProperty>(property) || !value->IsUnparsedDeclaration());
  DCHECK(!value->IsPendingSubstitutionValue());
  value = EnsureScopedValue(GetDocument(), match_result_, *priority, value);
  StyleBuilder::ApplyPhysicalProperty(property, state_, *value);
}

void StyleCascade::LookupAndApplyInterpolation(const CSSProperty& property,
                                               CascadePriority* priority,
                                               CascadeResolver& resolver) {
  if (priority->GetGeneration() >= resolver.generation_) {
    // Already applied this generation.
    return;
  }
  *priority = CascadePriority(*priority, resolver.generation_);

  DCHECK(!property.IsSurrogate());

  // Interpolations for -internal-visited properties are applied via the
  // interpolation for the main (unvisited) property, so we don't need to
  // apply it twice.
  // TODO(crbug.com/1062217): Interpolate visited colors separately
  if (property.IsVisited()) {
    return;
  }
  DCHECK(priority->GetOrigin() >= CascadeOrigin::kAnimation);
  wtf_size_t index = DecodeInterpolationIndex(priority->GetPosition());
  DCHECK_LE(index, interpolations_.GetEntries().size());
  const ActiveInterpolationsMap& map = *interpolations_.GetEntries()[index].map;
  PropertyHandle handle = ToPropertyHandle(property, *priority);
  const auto& entry = map.find(handle);
  DCHECK_NE(entry, map.end());
  ApplyInterpolation(property, *priority, *entry->value, resolver);
}

bool StyleCascade::IsRootElement() const {
  return &state_.GetElement() == state_.GetDocument().documentElement();
}

StyleCascade::TokenSequence::TokenSequence(const CSSVariableData* data)
    : is_animation_tainted_(data->IsAnimationTainted()),
      has_font_units_(data->HasFontUnits()),
      has_root_font_units_(data->HasRootFontUnits()),
      has_line_height_units_(data->HasLineHeightUnits()) {}

bool StyleCascade::TokenSequence::AppendFallback(const TokenSequence& sequence,
                                                 wtf_size_t byte_limit) {
  // https://drafts.csswg.org/css-variables/#long-variables
  if (original_text_.length() + sequence.original_text_.length() > byte_limit) {
    return false;
  }

  base::span<const CSSParserToken> other_tokens(sequence.tokens_.begin(),
                                                sequence.tokens_.end());
  StringView other_text = sequence.original_text_;
  other_text =
      CSSVariableParser::StripTrailingWhitespaceAndComments(other_text);
  while (!other_tokens.empty() &&
         other_tokens.front().GetType() == kWhitespaceToken) {
    other_tokens = other_tokens.subspan(1);
  }
  while (!other_tokens.empty() &&
         other_tokens.back().GetType() == kWhitespaceToken) {
    other_tokens = other_tokens.first(other_tokens.size() - 1);
  }

  if (!tokens_.empty() && !other_tokens.empty() &&
      NeedsInsertedComment(tokens_.back(), other_tokens.front())) {
    original_text_.Append("/**/");
  }
  tokens_.Append(other_tokens.data(),
                 static_cast<wtf_size_t>(other_tokens.size()));
  original_text_.Append(other_text);

  is_animation_tainted_ |= sequence.is_animation_tainted_;
  has_font_units_ |= sequence.has_font_units_;
  has_root_font_units_ |= sequence.has_root_font_units_;
  has_line_height_units_ |= sequence.has_line_height_units_;
  return true;
}

void StyleCascade::TokenSequence::StripCommentTokens() {
  tokens_.erase(std::remove_if(tokens_.begin(), tokens_.end(),
                               [](const CSSParserToken& token) {
                                 return token.GetType() == kCommentToken;
                               }),
                tokens_.end());
}

bool StyleCascade::TokenSequence::Append(CSSVariableData* data,
                                         CSSTokenizer* parent_tokenizer,
                                         wtf_size_t byte_limit) {
  // https://drafts.csswg.org/css-variables/#long-variables
  if (original_text_.length() + data->OriginalText().length() > byte_limit) {
    return false;
  }
  CSSTokenizer tokenizer(data->OriginalText());
  const CSSParserToken first_token = tokenizer.TokenizeSingleWithComments();
  if (first_token.GetType() != kEOFToken) {
    if (!tokens_.empty() && NeedsInsertedComment(tokens_.back(), first_token)) {
      original_text_.Append("/**/");
    }
    tokens_.push_back(first_token);
    while (true) {
      const CSSParserToken token = tokenizer.TokenizeSingleWithComments();
      if (token.GetType() == kEOFToken) {
        break;
      } else {
        tokens_.push_back(token);
      }
    }
  }
  original_text_.Append(data->OriginalText());
  is_animation_tainted_ |= data->IsAnimationTainted();
  has_font_units_ |= data->HasFontUnits();
  has_root_font_units_ |= data->HasRootFontUnits();
  has_line_height_units_ |= data->HasLineHeightUnits();
  if (parent_tokenizer) {
    tokenizer.PersistStrings(*parent_tokenizer);
  }
  return true;
}

void StyleCascade::TokenSequence::Append(const CSSParserToken& token,
                                         StringView original_text) {
  CSSVariableData::ExtractFeatures(token, has_font_units_, has_root_font_units_,
                                   has_line_height_units_);
  if (!tokens_.empty() && NeedsInsertedComment(tokens_.back(), token)) {
    original_text_.Append("/**/");
  }
  tokens_.push_back(token);
  original_text_.Append(original_text);
}

CSSVariableData* StyleCascade::TokenSequence::BuildVariableData() {
  return CSSVariableData::Create(original_text_, is_animation_tainted_,
                                 /*needs_variable_resolution=*/false,
                                 has_font_units_, has_root_font_units_,
                                 has_line_height_units_);
}

const CSSValue* StyleCascade::Resolve(const CSSProperty& property,
                                      const CSSValue& value,
                                      CascadePriority priority,
                                      CascadeOrigin& origin,
                                      CascadeResolver& resolver) {
  DCHECK(!property.IsSurrogate());

  const CSSValue* result = ResolveSubstitutions(property, value, resolver);
  DCHECK(result);

  if (result->IsRevertValue()) {
    return ResolveRevert(property, *result, origin, resolver);
  }
  if (result->IsRevertLayerValue() || TreatAsRevertLayer(priority)) {
    return ResolveRevertLayer(property, priority, origin, resolver);
  }
  if (const auto* v = DynamicTo<CSSFlipRevertValue>(result)) {
    return ResolveFlipRevert(property, *v, priority, origin, resolver);
  }
  if (const auto* v = DynamicTo<CSSAppearanceAutoBaseSelectValuePair>(result)) {
    return ResolveAppearanceAutoBaseSelect(property, *v, priority, origin,
                                           resolver);
  }
  if (const auto* v = DynamicTo<CSSMathFunctionValue>(result)) {
    return ResolveMathFunction(property, *v, priority);
  }

  resolver.CollectFlags(property, origin);

  return result;
}

const CSSValue* StyleCascade::ResolveSubstitutions(const CSSProperty& property,
                                                   const CSSValue& value,
                                                   CascadeResolver& resolver) {
  if (const auto* v = DynamicTo<CSSUnparsedDeclarationValue>(value)) {
    if (property.GetCSSPropertyName().IsCustomProperty()) {
      return ResolveCustomProperty(property, *v, resolver);
    } else {
      return ResolveVariableReference(property, *v, resolver);
    }
  }
  if (const auto* v = DynamicTo<cssvalue::CSSPendingSubstitutionValue>(value)) {
    return ResolvePendingSubstitution(property, *v, resolver);
  }
  return &value;
}

const CSSValue* StyleCascade::ResolveCustomProperty(
    const CSSProperty& property,
    const CSSUnparsedDeclarationValue& decl,
    CascadeResolver& resolver) {
  DCHECK(!property.IsSurrogate());

  DCHECK(!resolver.IsLocked(property));
  CascadeResolver::AutoLock lock(property, resolver);

  CSSVariableData* data = decl.VariableDataValue();

  if (data->NeedsVariableResolution()) {
    data = ResolveVariableData(data, *GetParserContext(decl), resolver);
  }

  if (HasFontSizeDependency(To<CustomProperty>(property), data)) {
    resolver.DetectCycle(GetCSSPropertyFontSize());
  }

  if (HasLineHeightDependency(To<CustomProperty>(property), data)) {
    resolver.DetectCycle(GetCSSPropertyLineHeight());
  }

  if (resolver.InCycle()) {
    return CSSCyclicVariableValue::Create();
  }

  if (!data) {
    return CSSInvalidVariableValue::Create();
  }

  if (data == decl.VariableDataValue()) {
    return &decl;
  }

  // If a declaration, once all var() functions are substituted in, contains
  // only a CSS-wide keyword (and possibly whitespace), its value is determined
  // as if that keyword were its specified value all along.
  //
  // https://drafts.csswg.org/css-variables/#substitute-a-var
  {
    CSSTokenizer tokenizer(data->OriginalText());
    Vector<CSSParserToken, 32> tokens = tokenizer.TokenizeToEOF();
    CSSParserTokenRange range(tokens);
    range.ConsumeWhitespace();
    CSSValue* value = css_parsing_utils::ConsumeCSSWideKeyword(range);
    if (value && range.AtEnd()) {
      return value;
    }
  }

  return MakeGarbageCollected<CSSUnparsedDeclarationValue>(
      data, decl.ParserContext());
}

const CSSValue* StyleCascade::ResolveVariableReference(
    const CSSProperty& property,
    const CSSUnparsedDeclarationValue& value,
    CascadeResolver& resolver) {
  DCHECK(!property.IsSurrogate());
  DCHECK(!resolver.IsLocked(property));
  CascadeResolver::AutoLock lock(property, resolver);

  const CSSVariableData* data = value.VariableDataValue();
  const CSSParserContext* context = GetParserContext(value);

  MarkHasVariableReference(property);

  DCHECK(data);
  DCHECK(context);

  TokenSequence sequence;

  CSSTokenizer tokenizer(data->OriginalText());
  CSSParserTokenStream stream(tokenizer);
  if (ResolveTokensInto(stream, resolver, &tokenizer, *context,
                        FunctionContext{}, sequence)) {
    // TODO(sesse): It would be nice if we had some way of combining
    // ResolveTokensInto() and the re-tokenization. This is basically
    // what we pay by using the streaming parser everywhere; we tokenize
    // everything involving variable references twice.
    CSSTokenizer tokenizer2(sequence.OriginalText());
    CSSParserTokenStream stream2(tokenizer2);
    if (const auto* parsed = Parse(property, stream2, context)) {
      return parsed;
    }
  }

  return cssvalue::CSSUnsetValue::Create();
}

const CSSValue* StyleCascade::ResolvePendingSubstitution(
    const CSSProperty& property,
    const cssvalue::CSSPendingSubstitutionValue& value,
    CascadeResolver& resolver) {
  DCHECK(!property.IsSurrogate());
  DCHECK(!resolver.IsLocked(property));
  CascadeResolver::AutoLock lock(property, resolver);

  CascadePriority priority = map_.At(property.GetCSSPropertyName());
  DCHECK_NE(property.PropertyID(), CSSPropertyID::kVariable);
  DCHECK_NE(priority.GetOrigin(), CascadeOrigin::kNone);

  MarkHasVariableReference(property);

  // If the previous call to ResolvePendingSubstitution parsed 'value', then
  // we don't need to do it again.
  bool is_cached = resolver.shorthand_cache_.value == &value;

  if (!is_cached) {
    CSSUnparsedDeclarationValue* shorthand_value = value.ShorthandValue();
    const auto* shorthand_data = shorthand_value->VariableDataValue();
    CSSPropertyID shorthand_property_id = value.ShorthandPropertyId();

    TokenSequence sequence;

    CSSTokenizer tokenizer(shorthand_data->OriginalText());
    CSSParserTokenStream stream(tokenizer);
    if (!ResolveTokensInto(stream, resolver, &tokenizer,
                           *GetParserContext(*shorthand_value),
                           FunctionContext{}, sequence)) {
      return cssvalue::CSSUnsetValue::Create();
    }

    HeapVector<CSSPropertyValue, 64> parsed_properties;

    // NOTE: We don't actually need the original text to be comment-stripped,
    // since we're not storing it in a custom property anywhere.
    CSSTokenizer tokenizer2(sequence.OriginalText());
    CSSParserTokenStream stream2(tokenizer2);
    if (!CSSPropertyParser::ParseValue(
            shorthand_property_id, /*allow_important_annotation=*/false,
            stream2, shorthand_value->ParserContext(), parsed_properties,
            StyleRule::RuleType::kStyle)) {
      return cssvalue::CSSUnsetValue::Create();
    }

    resolver.shorthand_cache_.value = &value;
    resolver.shorthand_cache_.parsed_properties = std::move(parsed_properties);
  }

  const auto& parsed_properties = resolver.shorthand_cache_.parsed_properties;

  // For -internal-visited-properties with CSSPendingSubstitutionValues,
  // the inner 'shorthand_property_id' will expand to a set of longhands
  // containing the unvisited equivalent. Hence, when parsing the
  // CSSPendingSubstitutionValue, we look for the unvisited property in
  // parsed_properties.
  const CSSProperty* unvisited_property =
      property.IsVisited() ? property.GetUnvisitedProperty() : &property;

  unsigned parsed_properties_count = parsed_properties.size();
  for (unsigned i = 0; i < parsed_properties_count; ++i) {
    const CSSProperty& longhand = CSSProperty::Get(parsed_properties[i].Id());
    const CSSValue* parsed = parsed_properties[i].Value();

    // When using var() in a css-logical shorthand (e.g. margin-inline),
    // the longhands here will also be logical.
    if (unvisited_property == &ResolveSurrogate(longhand)) {
      return parsed;
    }
  }

  NOTREACHED_IN_MIGRATION();
  return cssvalue::CSSUnsetValue::Create();
}

const CSSValue* StyleCascade::ResolveRevert(const CSSProperty& property,
                                            const CSSValue& value,
                                            CascadeOrigin& origin,
                                            CascadeResolver& resolver) {
  MaybeUseCountRevert(value);

  CascadeOrigin target_origin = TargetOriginForRevert(origin);

  switch (target_origin) {
    case CascadeOrigin::kTransition:
    case CascadeOrigin::kNone:
      return cssvalue::CSSUnsetValue::Create();
    case CascadeOrigin::kUserAgent:
    case CascadeOrigin::kUser:
    case CascadeOrigin::kAuthorPresentationalHint:
    case CascadeOrigin::kAuthor:
    case CascadeOrigin::kAnimation: {
      const CascadePriority* p =
          map_.Find(property.GetCSSPropertyName(), target_origin);
      if (!p) {
        origin = CascadeOrigin::kNone;
        return cssvalue::CSSUnsetValue::Create();
      }
      origin = p->GetOrigin();
      return Resolve(property, *ValueAt(match_result_, p->GetPosition()), *p,
                     origin, resolver);
    }
  }
}

const CSSValue* StyleCascade::ResolveRevertLayer(const CSSProperty& property,
                                                 CascadePriority priority,
                                                 CascadeOrigin& origin,
                                                 CascadeResolver& resolver) {
  const CascadePriority* p = map_.FindRevertLayer(
      property.GetCSSPropertyName(), priority.ForLayerComparison());
  if (!p) {
    origin = CascadeOrigin::kNone;
    return cssvalue::CSSUnsetValue::Create();
  }
  origin = p->GetOrigin();
  return Resolve(property, *ValueAt(match_result_, p->GetPosition()), *p,
                 origin, resolver);
}

const CSSValue* StyleCascade::ResolveFlipRevert(const CSSProperty& property,
                                                const CSSFlipRevertValue& value,
                                                CascadePriority priority,
                                                CascadeOrigin& origin,
                                                CascadeResolver& resolver) {
  const CSSProperty& to_property =
      ResolveSurrogate(CSSProperty::Get(value.PropertyID()));
  const CSSValue* unflipped =
      ResolveRevertLayer(to_property, priority, origin, resolver);
  // Note: the value is transformed *from* the property we're reverting *to*.
  const CSSValue* flipped = TryValueFlips::FlipValue(
      /* from_property */ to_property.PropertyID(), unflipped,
      value.Transform(), state_.StyleBuilder().GetWritingDirection());
  return Resolve(property, *flipped, priority, origin, resolver);
}

const CSSValue* StyleCascade::ResolveAppearanceAutoBaseSelect(
    const CSSProperty& property,
    const CSSAppearanceAutoBaseSelectValuePair& value,
    CascadePriority priority,
    CascadeOrigin& origin,
    CascadeResolver& resolver) {
  // The UA stylesheet only uses -internal-appearance-auto-base-select(),
  // on select elements, which is currently the only element which supports
  // appearance:base-select.
  CHECK(IsA<HTMLSelectElement>(state_.GetElement()));
  const CSSValue& selected = state_.StyleBuilder().HasBaseSelectAppearance()
                                 ? value.Second()
                                 : value.First();
  return Resolve(property, selected, priority, origin, resolver);
}

// Math functions can become invalid at computed-value time. Currently, this
// is only possible for invalid anchor*() functions.
//
// https://drafts.csswg.org/css-anchor-position-1/#anchor-valid
// https://drafts.csswg.org/css-anchor-position-1/#anchor-size-valid
const CSSValue* StyleCascade::ResolveMathFunction(
    const CSSProperty& property,
    const CSSMathFunctionValue& math_value,
    CascadePriority priority) {
  if (!math_value.HasAnchorFunctions()) {
    return &math_value;
  }

  auto anchor_mode = [](const CSSProperty& property) {
    switch (property.PropertyID()) {
      case CSSPropertyID::kTop:
        return AnchorEvaluator::Mode::kTop;
      case CSSPropertyID::kRight:
        return AnchorEvaluator::Mode::kRight;
      case CSSPropertyID::kBottom:
        return AnchorEvaluator::Mode::kBottom;
      case CSSPropertyID::kLeft:
        return AnchorEvaluator::Mode::kLeft;
      case CSSPropertyID::kWidth:
      case CSSPropertyID::kHeight:
      case CSSPropertyID::kMinWidth:
      case CSSPropertyID::kMinHeight:
      case CSSPropertyID::kMaxWidth:
      case CSSPropertyID::kMaxHeight:
        return AnchorEvaluator::Mode::kSize;
      default:
        return AnchorEvaluator::Mode::kNone;
    }
  };

  const CSSLengthResolver& length_resolver = state_.CssToLengthConversionData();

  // Calling HasInvalidAnchorFunctions evaluates the anchor*() functions
  // inside the CSSMathFunctionValue. Evaluating anchor*() requires that we
  // have the correct AnchorEvaluator::Mode, so we need to set that just like
  // we do for during e.g. Left::ApplyValue, Right::ApplyValue, etc.
  AnchorScope anchor_scope(anchor_mode(property),
                           length_resolver.GetAnchorEvaluator());
  // HasInvalidAnchorFunctions actually evaluates any anchor*() queries
  // within the CSSMathFunctionValue, and this requires the TreeScope to
  // be populated.
  const auto* scoped_math_value = To<CSSMathFunctionValue>(
      EnsureScopedValue(GetDocument(), match_result_, priority, &math_value));
  if (scoped_math_value->HasInvalidAnchorFunctions(length_resolver)) {
    return cssvalue::CSSUnsetValue::Create();
  }
  return scoped_math_value;
}

CSSVariableData* StyleCascade::ResolveVariableData(
    CSSVariableData* data,
    const CSSParserContext& context,
    CascadeResolver& resolver) {
  DCHECK(data && data->NeedsVariableResolution());

  TokenSequence sequence(data);

  CSSTokenizer tokenizer(data->OriginalText());
  CSSParserTokenStream stream(tokenizer);
  if (!ResolveTokensInto(stream, resolver, /*parent_tokenizer=*/nullptr,
                         context, FunctionContext{}, sequence)) {
    return nullptr;
  }

  return sequence.BuildVariableData();
}

bool StyleCascade::ResolveTokensInto(CSSParserTokenStream& stream,
                                     CascadeResolver& resolver,
                                     CSSTokenizer* parent_tokenizer,
                                     const CSSParserContext& context,
                                     const FunctionContext& function_context,
                                     TokenSequence& out) {
  bool success = true;
  int nesting_level = 0;
  while (true) {
    const CSSParserToken& token = stream.Peek();
    if (token.IsEOF()) {
      break;
    } else if (token.FunctionId() == CSSValueID::kVar) {
      CSSParserTokenStream::BlockGuard guard(stream);
      success &=
          ResolveVarInto(stream, resolver, parent_tokenizer, context, out);
    } else if (token.FunctionId() == CSSValueID::kEnv) {
      CSSParserTokenStream::BlockGuard guard(stream);
      success &=
          ResolveEnvInto(stream, resolver, parent_tokenizer, context, out);
    } else if (token.FunctionId() == CSSValueID::kArg &&
               RuntimeEnabledFeatures::CSSFunctionsEnabled()) {
      CSSParserTokenStream::BlockGuard guard(stream);
      success &= ResolveArgInto(stream, resolver, parent_tokenizer, context,
                                function_context, out);
    } else if (token.GetType() == kFunctionToken &&
               CSSVariableParser::IsValidVariableName(token.Value()) &&
               RuntimeEnabledFeatures::CSSFunctionsEnabled()) {
      // User-defined CSS function.
      CSSParserTokenStream::BlockGuard guard(stream);
      success &=
          ResolveFunctionInto(token.Value(), stream, resolver, parent_tokenizer,
                              context, function_context, out);
    } else {
      if (token.GetBlockType() == CSSParserToken::kBlockStart) {
        ++nesting_level;
      } else if (token.GetBlockType() == CSSParserToken::kBlockEnd) {
        if (nesting_level == 0) {
          // Attempting to go outside our block.
          break;
        }
        --nesting_level;
      }
      wtf_size_t start = stream.Offset();
      stream.ConsumeRaw();
      wtf_size_t end = stream.Offset();

      // NOTE: This will include any comment tokens that ConsumeRaw()
      // skipped over; i.e., any comment will be attributed to the
      // token after it and any trailing comments will be skipped.
      // This is fine, because trailing comments (sans whitespace)
      // should be skipped anyway.
      out.Append(token, stream.StringRangeAt(start, end - start));
    }
  }
  return success;
}

bool StyleCascade::ResolveVarInto(CSSParserTokenStream& stream,
                                  CascadeResolver& resolver,
                                  CSSTokenizer* parent_tokenizer,
                                  const CSSParserContext& context,
                                  TokenSequence& out) {
  CustomProperty property(ConsumeVariableName(stream), state_.GetDocument());
  DCHECK(stream.AtEnd() || (stream.Peek().GetType() == kCommaToken));

  // Any custom property referenced (by anything, even just once) in the
  // document can currently not be animated on the compositor. Hence we mark
  // properties that have been referenced.
  DCHECK(resolver.CurrentProperty());
  MarkIsReferenced(*resolver.CurrentProperty(), property);

  if (!resolver.DetectCycle(property)) {
    // We are about to substitute var(property). In order to do that, we must
    // know the computed value of 'property', hence we Apply it.
    //
    // We can however not do this if we're in a cycle. If a cycle is detected
    // here, it means we are already resolving 'property', and have discovered
    // a reference to 'property' during that resolution.
    LookupAndApply(property, resolver);
  }

  // Note that even if we are in a cycle, we must proceed in order to discover
  // secondary cycles via the var() fallback.

  CSSVariableData* data = GetVariableData(property);

  // If substitution is not allowed, treat the value as
  // invalid-at-computed-value-time.
  //
  // https://drafts.csswg.org/css-variables/#animation-tainted
  if (!resolver.AllowSubstitution(data)) {
    data = nullptr;
  }

  // If we have a fallback, we must process it to look for cycles,
  // even if we aren't going to use the fallback.
  //
  // https://drafts.csswg.org/css-variables/#cycles
  if (ConsumeComma(stream)) {
    stream.ConsumeWhitespace();

    TokenSequence fallback;
    bool success = ResolveTokensInto(stream, resolver, parent_tokenizer,
                                     context, FunctionContext{}, fallback);
    // The fallback must match the syntax of the referenced custom property.
    // https://drafts.css-houdini.org/css-properties-values-api-1/#fallbacks-in-var-references
    //
    // NOTE: We don't need the original text here, because ValidateFallback()
    // only validates the tokens; it doesn't store anything.
    if (!ValidateFallback(property, {fallback.TokenRange(), StringView()})) {
      return false;
    }
    if (!data) {
      return success &&
             out.AppendFallback(fallback, CSSVariableData::kMaxVariableBytes);
    }
  }

  if (!data || resolver.InCycle()) {
    return false;
  }

  return out.Append(data, parent_tokenizer, CSSVariableData::kMaxVariableBytes);
}

bool StyleCascade::ResolveFunctionInto(StringView function_name,
                                       CSSParserTokenStream& stream,
                                       CascadeResolver& resolver,
                                       CSSTokenizer* parent_tokenizer,
                                       const CSSParserContext& context,
                                       const FunctionContext& function_context,
                                       TokenSequence& out) {
  state_.StyleBuilder().SetAffectedByCSSFunction();

  // TODO(sesse): Deal with tree-scoped references.
  StyleRuleFunction* function = nullptr;
  if (GetDocument().GetScopedStyleResolver()) {
    function =
        GetDocument().GetScopedStyleResolver()->FunctionForName(function_name);
  }
  if (!function) {
    return false;
  }

  // Parse and resolve function arguments.
  HeapHashMap<String, Member<const CSSValue>> function_arguments;

  bool first_parameter = true;
  for (const StyleRuleFunction::Parameter& parameter :
       function->GetParameters()) {
    stream.ConsumeWhitespace();
    if (!first_parameter) {
      if (stream.Peek().GetType() != kCommaToken) {
        return false;
      }
      stream.ConsumeIncludingWhitespace();
    }
    first_parameter = false;

    wtf_size_t value_start_offset = stream.LookAheadOffset();
    const CSSParserTokenRange argument_range [[maybe_unused]] =
        stream.ConsumeUntilPeekedTypeIs<kCommaToken, kRightParenthesisToken>();
    wtf_size_t value_end_offset = stream.LookAheadOffset();
    StringView argument_string = stream.StringRangeAt(
        value_start_offset, value_end_offset - value_start_offset);

    // We need to resolve the argument in the context of this function,
    // so that we can do type coercion on the resolved value before the call.
    // In particular, we want any arg() within the argument to be resolved
    // in our context; e.g., --foo(arg(--a)) should be our a, not foo's a
    // (if that even exists).
    //
    // Note that if this expression comes from directly a function call,
    // as in the example above (and if the return and argument types are the
    // same), we will effectively do type parsing of exactly the same data
    // twice. This is wasteful, and it's possible that we should do something
    // about it if it proves to be a common case.
    const CSSValue* argument_value = ResolveFunctionExpression(
        argument_string, parameter.type, resolver, context, function_context);
    if (argument_value == nullptr) {
      return false;
    }

    function_arguments.insert(parameter.name, argument_value);
  }

  const CSSValue* ret_value = ResolveFunctionExpression(
      function->GetFunctionBody().OriginalText(), function->GetReturnType(),
      resolver, context, FunctionContext{function_arguments});
  if (ret_value == nullptr) {
    return false;
  }
  // Urggg
  CSSTokenizer tokenizer(ret_value->CssText());
  CSSParserTokenStream ret_value_stream(tokenizer);
  return ResolveTokensInto(ret_value_stream, resolver, &tokenizer, context,
                           FunctionContext{}, out);
}

// Resolves an expression within a function; in practice, either a function
// argument or its return value. In practice, this is about taking a string
// and coercing it into the given type -- and then the caller will convert it
// right back to a string again. This is pretty suboptimal, but it's the way
// registered properties also work, and crucially, without such a resolve step
// (which needs a type), we would not be able to collapse calc() expressions
// and similar, which could cause massive blowup as the values are passed
// through a large tree of function calls.
const CSSValue* StyleCascade::ResolveFunctionExpression(
    StringView expr,
    const StyleRuleFunction::Type& type,
    CascadeResolver& resolver,
    const CSSParserContext& context,
    const FunctionContext& function_context) {
  TokenSequence resolved_expr;

  // See documentation on should_add_implicit_calc.
  if (type.should_add_implicit_calc) {
    static const char kCalcToken[] = "calc";
    static const char kCalcStart[] = "calc(";
    resolved_expr.Append(
        CSSParserToken(kFunctionToken, kCalcToken, CSSParserToken::kBlockStart),
        kCalcStart);
  }

  CSSTokenizer tokenizer(expr);
  CSSParserTokenStream argument_stream(tokenizer);
  if (!ResolveTokensInto(argument_stream, resolver, &tokenizer, context,
                         function_context, resolved_expr)) {
    return nullptr;
  }

  if (type.should_add_implicit_calc) {
    static const char kCalcEnd[] = ")";
    resolved_expr.Append(
        CSSParserToken(kRightParenthesisToken, CSSParserToken::kBlockEnd),
        kCalcEnd);
  }

  const CSSValue* value =
      type.syntax.Parse(CSSTokenizedValue{resolved_expr.TokenRange(),
                                          resolved_expr.OriginalText()},
                        context, /*is_animation_tainted=*/false);
  if (!value) {
    return nullptr;
  }

  // Resolve the value as if it were a registered property, to get rid of
  // extraneous calc(), resolve lengths and so on.
  return &StyleBuilderConverter::ConvertRegisteredPropertyValue(state_, *value,
                                                                &context);
}

bool StyleCascade::ResolveEnvInto(CSSParserTokenStream& stream,
                                  CascadeResolver& resolver,
                                  CSSTokenizer* parent_tokenizer,
                                  const CSSParserContext& context,
                                  TokenSequence& out) {
  AtomicString variable_name = ConsumeVariableName(stream);
  DCHECK(stream.AtEnd() || (stream.Peek().GetType() == kCommaToken) ||
         (stream.Peek().GetType() == kNumberToken));

  WTF::Vector<unsigned> indices;
  if (!stream.AtEnd() && stream.Peek().GetType() != kCommaToken) {
    do {
      const CSSParserToken& token = stream.ConsumeIncludingWhitespaceRaw();
      DCHECK(token.GetNumericValueType() == kIntegerValueType);
      DCHECK(token.NumericValue() >= 0.);
      indices.push_back(static_cast<unsigned>(token.NumericValue()));
    } while (stream.Peek().GetType() == kNumberToken);
  }

  DCHECK(stream.AtEnd() || (stream.Peek().GetType() == kCommaToken));

  CSSVariableData* data = GetEnvironmentVariable(variable_name, indices);

  if (!data) {
    if (ConsumeComma(stream)) {
      return ResolveTokensInto(stream, resolver, parent_tokenizer, context,
                               FunctionContext{}, out);
    }
    return false;
  }

  return out.Append(data, parent_tokenizer);
}

bool StyleCascade::ResolveArgInto(CSSParserTokenStream& stream,
                                  CascadeResolver& resolver,
                                  CSSTokenizer* parent_tokenizer,
                                  const CSSParserContext& context,
                                  const FunctionContext& function_context,
                                  TokenSequence& out) {
  AtomicString argument_name = ConsumeVariableName(stream);
  DCHECK(stream.AtEnd());

  const auto it = function_context.arguments.find(argument_name);
  if (it == function_context.arguments.end()) {
    // Argument not found.
    return false;
  }

  CSSTokenizer tokenizer(it->value->CssText());
  CSSParserTokenStream arg_value_stream(tokenizer);
  return ResolveTokensInto(arg_value_stream, resolver, &tokenizer, context,
                           FunctionContext{}, out);
}

CSSVariableData* StyleCascade::GetVariableData(
    const CustomProperty& property) const {
  const AtomicString& name = property.GetPropertyNameAtomicString();
  const bool is_inherited = property.IsInherited();
  return state_.StyleBuilder().GetVariableData(name, is_inherited);
}

CSSVariableData* StyleCascade::GetEnvironmentVariable(
    const AtomicString& name,
    WTF::Vector<unsigned> indices) const {
  // If we are in a User Agent Shadow DOM then we should not record metrics.
  ContainerNode& scope_root = state_.GetElement().GetTreeScope().RootNode();
  auto* shadow_root = DynamicTo<ShadowRoot>(&scope_root);
  bool is_ua_scope = shadow_root && shadow_root->IsUserAgent();

  return state_.GetDocument()
      .GetStyleEngine()
      .EnsureEnvironmentVariables()
      .ResolveVariable(name, std::move(indices), !is_ua_scope);
}

const CSSParserContext* StyleCascade::GetParserContext(
    const CSSUnparsedDeclarationValue& value) {
  // TODO(crbug.com/985028): CSSUnparsedDeclarationValue should always have a
  // CSSParserContext. (CSSUnparsedValue violates this).
  if (value.ParserContext()) {
    return value.ParserContext();
  }
  return StrictCSSParserContext(
      state_.GetDocument().GetExecutionContext()->GetSecureContextMode());
}

bool StyleCascade::HasFontSizeDependency(const CustomProperty& property,
                                         CSSVariableData* data) const {
  if (!property.IsRegistered() || !data) {
    return false;
  }
  if (data->HasFontUnits() || data->HasLineHeightUnits()) {
    return true;
  }
  if (data->HasRootFontUnits() && IsRootElement()) {
    return true;
  }
  return false;
}

bool StyleCascade::HasLineHeightDependency(const CustomProperty& property,
                                           CSSVariableData* data) const {
  if (!property.IsRegistered() || !data) {
    return false;
  }
  if (data->HasLineHeightUnits()) {
    return true;
  }
  return false;
}

bool StyleCascade::ValidateFallback(const CustomProperty& property,
                                    CSSTokenizedValue value) const {
#if DCHECK_IS_ON()
  DCHECK(!HasUnresolvedReferences(value.range));
#endif  // DCHECK_IS_ON()
  if (!property.IsRegistered()) {
    return true;
  }
  auto context_mode =
      state_.GetDocument().GetExecutionContext()->GetSecureContextMode();
  auto* context = StrictCSSParserContext(context_mode);
  auto local_context = CSSParserLocalContext();
  return property.Parse(value, *context, local_context);
}

void StyleCascade::MarkIsReferenced(const CSSProperty& referencer,
                                    const CustomProperty& referenced) {
  if (!referenced.IsRegistered()) {
    return;
  }
  const AtomicString& name = referenced.GetPropertyNameAtomicString();
  state_.GetDocument().EnsurePropertyRegistry().MarkReferenced(name);
}

void StyleCascade::MarkHasVariableReference(const CSSProperty& property) {
  if (!property.IsInherited()) {
    state_.StyleBuilder().SetHasVariableReferenceFromNonInheritedProperty();
  }
  state_.StyleBuilder().SetHasVariableReference();
}

bool StyleCascade::TreatAsRevertLayer(CascadePriority priority) const {
  return priority.IsTryStyle() && !ComputedStyle::HasOutOfFlowPosition(
                                      state_.StyleBuilder().GetPosition());
}

const Document& StyleCascade::GetDocument() const {
  return state_.GetDocument();
}

const CSSProperty& StyleCascade::ResolveSurrogate(const CSSProperty& property) {
  if (!property.IsSurrogate()) {
    return property;
  }
  // This marks the cascade as dependent on cascade-affecting properties
  // even for simple surrogates like -webkit-writing-mode, but there isn't
  // currently a flag to distinguish such surrogates from e.g. css-logical
  // properties.
  depends_on_cascade_affecting_property_ = true;
  const CSSProperty* original =
      property.SurrogateFor(state_.StyleBuilder().Direction(),
                            state_.StyleBuilder().GetWritingMode());
  DCHECK(original);
  return *original;
}

void StyleCascade::CountUse(WebFeature feature) {
  GetDocument().CountUse(feature);
}

void StyleCascade::MaybeUseCountRevert(const CSSValue& value) {
  if (value.IsRevertValue()) {
    CountUse(WebFeature::kCSSKeywordRevert);
  }
}

void StyleCascade::ExpandSignals(const MatchedProperties& properties,
                                 int index,
                                 Signal signal) {
  ExpandCascade(
      properties, GetDocument(), index,
      [this, signal](CascadePriority cascade_priority,
                     const AtomicString& custom_property_name) {
        MaybeAddPendingSignal(CSSPropertyName(custom_property_name),
                              cascade_priority, signal);
      },
      [this, signal](CascadePriority cascade_priority,
                     CSSPropertyID property_id) {
        if (kSurrogateProperties.Has(property_id)) {
          const CSSProperty& property =
              ResolveSurrogate(CSSProperty::Get(property_id));
          MaybeAddPendingSignal(property.GetCSSPropertyName(), cascade_priority,
                                signal);
        } else {
          MaybeAddPendingSignal(CSSPropertyName(property_id), cascade_priority,
                                signal);
        }
      });
}

void StyleCascade::MaybeAddPendingSignal(const CSSPropertyName& name,
                                         CascadePriority priority,
                                         Signal signal) {
  CHECK_NE(signal, Signal::kNone);

  const CascadePriority* existing_priority = map_.Find(name);

  bool add_would_change_value =
      !existing_priority ||
      (ValueAt(match_result_, existing_priority->GetPosition()) !=
       ValueAt(match_result_, priority.GetPosition()));

  // We only add a pending signal if the declaration actually makes
  // a difference to the cascade. The pending signals then either get
  // converted to actual use-counting during `ProcessPendingSignals`
  // if they end up winning the cascade, or they just get ignored.
  if (add_would_change_value) {
    wtf_size_t index = static_cast<wtf_size_t>(signal) - 1;
    CHECK_LT(index, static_cast<wtf_size_t>(Signal::kMax));
    pending_signals_[index].Set(name, priority);
  }
}

void StyleCascade::ProcessPendingSignals() {
  static_assert(static_cast<int>(Signal::kBareDeclarationShift) == 1);
  static_assert(static_cast<int>(Signal::kNestedGroupRuleSpecificity) == 2);
  static_assert(static_cast<int>(Signal::kMax) == 2);
  ProcessPendingSignals(WebFeature::kCSSBareDeclarationShift,
                        pending_signals_[0]);
  pending_signals_[0].clear();
  ProcessPendingSignals(WebFeature::kCSSNestedGroupRuleSpecificity,
                        pending_signals_[1]);
  pending_signals_[1].clear();
}

void StyleCascade::ProcessPendingSignals(
    WebFeature feature,
    const HashMap<CSSPropertyName, CascadePriority>& signals) {
  for (const auto& [name, signal_priority] : signals) {
    // Only trigger the use-counter if the signaling declaration
    // won the cascade.
    if (signal_priority == map_.At(name)) {
      CountUse(feature);
    }
  }
}

}  // namespace blink
