/*
 * (C) 1999-2003 Lars Knoll (knoll@kde.org)
 * (C) 2002-2003 Dirk Mueller (mueller@kde.org)
 * Copyright (C) 2002, 2006, 2008, 2012, 2013 Apple Inc. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_CSS_STYLE_RULE_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_CSS_STYLE_RULE_H_

#include <limits>

#include "base/bits.h"
#include "base/memory/scoped_refptr.h"
#include "base/types/pass_key.h"
#include "third_party/blink/renderer/core/core_export.h"
#include "third_party/blink/renderer/core/css/container_query.h"
#include "third_party/blink/renderer/core/css/css_property_value_set.h"
#include "third_party/blink/renderer/core/css/css_selector_list.h"
#include "third_party/blink/renderer/core/css/css_syntax_definition.h"
#include "third_party/blink/renderer/core/css/css_variable_data.h"
#include "third_party/blink/renderer/core/css/media_list.h"
#include "third_party/blink/renderer/core/css/parser/css_at_rule_id.h"
#include "third_party/blink/renderer/core/css/parser/css_nesting_type.h"
#include "third_party/blink/renderer/core/css/style_scope.h"
#include "third_party/blink/renderer/platform/heap/garbage_collected.h"
#include "third_party/blink/renderer/platform/wtf/casting.h"

namespace blink {

class CascadeLayer;
class CSSRule;
class CSSStyleSheet;
class ExecutionContext;
class StyleSheetContents;

class CORE_EXPORT StyleRuleBase : public GarbageCollected<StyleRuleBase> {
 public:
  enum RuleType {
    kCharset,
    kStyle,
    kImport,
    kMedia,
    kFontFace,
    kFontPaletteValues,
    kFontFeatureValues,
    kFontFeature,
    kPage,
    kPageMargin,
    kProperty,
    kKeyframes,
    kKeyframe,
    kLayerBlock,
    kLayerStatement,
    kNamespace,
    kContainer,
    kCounterStyle,
    kScope,
    kSupports,
    kStartingStyle,
    kViewTransition,
    kFunction,
    kPositionTry,
  };

  // Name of a cascade layer as given by an @layer rule, split at '.' into a
  // vector. Note that this may not be the full layer name if the rule is nested
  // in another @layer rule or in a layered @import.
  using LayerName = Vector<AtomicString, 1>;
  static String LayerNameAsString(const LayerName&);

  RuleType GetType() const { return static_cast<RuleType>(type_); }

  bool IsCharsetRule() const { return GetType() == kCharset; }
  bool IsContainerRule() const { return GetType() == kContainer; }
  bool IsCounterStyleRule() const { return GetType() == kCounterStyle; }
  bool IsFontFaceRule() const { return GetType() == kFontFace; }
  bool IsFontPaletteValuesRule() const {
    return GetType() == kFontPaletteValues;
  }
  bool IsFontFeatureValuesRule() const {
    return GetType() == kFontFeatureValues;
  }
  bool IsFontFeatureRule() const { return GetType() == kFontFeature; }
  bool IsKeyframesRule() const { return GetType() == kKeyframes; }
  bool IsKeyframeRule() const { return GetType() == kKeyframe; }
  bool IsLayerBlockRule() const { return GetType() == kLayerBlock; }
  bool IsLayerStatementRule() const { return GetType() == kLayerStatement; }
  bool IsNamespaceRule() const { return GetType() == kNamespace; }
  bool IsMediaRule() const { return GetType() == kMedia; }
  bool IsPageRule() const { return GetType() == kPage; }
  bool IsPageRuleMargin() const { return GetType() == kPageMargin; }
  bool IsPropertyRule() const { return GetType() == kProperty; }
  bool IsStyleRule() const { return GetType() == kStyle; }
  bool IsScopeRule() const { return GetType() == kScope; }
  bool IsSupportsRule() const { return GetType() == kSupports; }
  bool IsImportRule() const { return GetType() == kImport; }
  bool IsStartingStyleRule() const { return GetType() == kStartingStyle; }
  bool IsViewTransitionRule() const { return GetType() == kViewTransition; }
  bool IsConditionRule() const {
    return GetType() == kContainer || GetType() == kMedia ||
           GetType() == kSupports || GetType() == kStartingStyle;
  }
  bool IsFunctionRule() const { return GetType() == kFunction; }
  bool IsPositionTryRule() const { return GetType() == kPositionTry; }

  StyleRuleBase* Copy() const;

  // FIXME: There shouldn't be any need for the null parent version.
  CSSRule* CreateCSSOMWrapper(
      wtf_size_t position_hint = std::numeric_limits<wtf_size_t>::max(),
      CSSStyleSheet* parent_sheet = nullptr,
      bool trigger_use_counters = false) const;
  CSSRule* CreateCSSOMWrapper(wtf_size_t position_hint,
                              CSSRule* parent_rule,
                              bool trigger_use_counters = false) const;

  // Move this rule from being a child of old_parent (which is only given for
  // sake of DCHECK) to being a child of new_parent, updating parent pointers
  // in the selector. This happens only when we need to reallocate a StyleRule
  // because its selector changed.
  void Reparent(StyleRule* old_parent, StyleRule* new_parent);

  void Trace(Visitor*) const;
  void TraceAfterDispatch(blink::Visitor* visitor) const {}
  void FinalizeGarbageCollectedObject();

  // See CSSSelector::IsInvisible.
  bool IsInvisible() const;
  // See CSSSelector::Signal.
  bool IsSignaling() const;

  bool HasSignalingChildRule() const { return has_signaling_child_rule_; }

  // This class mimics the API of HeapVector<Member<StyleRuleBase>>,
  // except that any invisible rule added to the vector isn't visible
  // through any member function except `RawChildRules`.
  //
  // TODO(crbug.com/1517290): Remove this when we're done use-counting.
  class CORE_EXPORT ChildRuleVector : public GarbageCollected<ChildRuleVector> {
   public:
    ChildRuleVector() = default;

    // An iterator which skips invisible rules.
    class CORE_EXPORT Iterator {
      STACK_ALLOCATED();

     public:
      Iterator(const Member<StyleRuleBase>* position,
               const Member<StyleRuleBase>* end)
          : position_(position), end_(end) {
        CHECK_LE(position, end);
      }

      void operator++();
      Member<StyleRuleBase> operator*() const { return *position_; }
      bool operator==(const Iterator& o) const {
        return position_ == o.position_ && end_ == o.end_;
      }
      bool operator!=(const Iterator& o) const { return !(*this == o); }

     private:
      const Member<StyleRuleBase>* position_;
      const Member<StyleRuleBase>* end_;
    };

    ChildRuleVector* Copy() const;

    Iterator begin() const {
      // The AdjustedIndex call ensures that we skip leading invisible rules.
      return Iterator(rules_.begin() + AdjustedIndex(0), rules_.end());
    }
    Iterator end() const { return Iterator(rules_.end(), rules_.end()); }

    const Member<StyleRuleBase>& operator[](wtf_size_t i) const {
      return rules_.at(AdjustedIndex(i));
    }
    Member<StyleRuleBase>& operator[](wtf_size_t i) {
      return rules_.at(AdjustedIndex(i));
    }

    wtf_size_t size() const { return rules_.size() - num_invisible_rules_; }

    void AddChildRule(StyleRuleBase* rule);
    void WrapperInsertRule(unsigned index, StyleRuleBase*);
    void WrapperRemoveRule(unsigned index);

    const HeapVector<Member<StyleRuleBase>>& RawChildRules() const {
      return rules_;
    }

    void Trace(blink::Visitor* visitor) const { visitor->Trace(rules_); }

   private:
    // Finds the real index of the Nth non-invisible child rule.
    // The provided `index` must be in the range [0, size()].
    wtf_size_t AdjustedIndex(wtf_size_t index) const;

    HeapVector<Member<StyleRuleBase>> rules_;
    wtf_size_t num_invisible_rules_ = 0;
  };

 protected:
  explicit StyleRuleBase(RuleType type)
      : type_(type), has_signaling_child_rule_(false) {}
  StyleRuleBase(const StyleRuleBase& rule)
      : type_(rule.type_),
        has_signaling_child_rule_(rule.has_signaling_child_rule_) {}

  void SetHasSignalingChildRule(bool has_signaling_child_rule) {
    has_signaling_child_rule_ = has_signaling_child_rule;
  }

 private:
  CSSRule* CreateCSSOMWrapper(wtf_size_t position_hint,
                              CSSStyleSheet* parent_sheet,
                              CSSRule* parent_rule,
                              bool trigger_use_counters) const;

  const uint8_t type_;
  // See CSSSelector::Signal.
  bool has_signaling_child_rule_;
};

// A single rule from a stylesheet. Contains a selector list (one or more
// complex selectors) and a collection of style properties to be applied where
// those selectors match. These are output by CSSParserImpl.
//
// Note that since this we generate so many StyleRule objects, and all of them
// have at least one selector, the selector list is not allocated separately as
// on a CSSSelectorList. Instead, we put the CSSSelectors immediately after the
// StyleRule object. This both saves memory (since we don't need the pointer,
// or any of the extra allocation overhead), and makes it likely that the
// CSSSelectors are on the same cache line as the StyleRule. (On the flip side,
// it makes it unlikely that the CSSSelector's RareData is on the same cache
// line as the CSSSelector itself, but it is still overall a good tradeoff
// for us.) StyleRule provides an API that is a subset of CSSSelectorList,
// partially implemented using its static member functions.
class CORE_EXPORT StyleRule : public StyleRuleBase {
  static AdditionalBytes AdditionalBytesForSelectors(size_t flattened_size) {
    constexpr size_t padding_bytes =
        base::bits::AlignUp(sizeof(StyleRule), alignof(CSSSelector)) -
        sizeof(StyleRule);
    return AdditionalBytes{(sizeof(CSSSelector) * flattened_size) +
                           padding_bytes};
  }

 public:
  // Use these to allocate the right amount of memory for the StyleRule.
  static StyleRule* Create(base::span<CSSSelector> selectors,
                           CSSPropertyValueSet* properties) {
    return MakeGarbageCollected<StyleRule>(
        AdditionalBytesForSelectors(selectors.size()),
        base::PassKey<StyleRule>(), selectors, properties);
  }
  static StyleRule* Create(base::span<CSSSelector> selectors,
                           CSSLazyPropertyParser* lazy_property_parser) {
    return MakeGarbageCollected<StyleRule>(
        AdditionalBytesForSelectors(selectors.size()),
        base::PassKey<StyleRule>(), selectors, lazy_property_parser);
  }

  // See comment on the corresponding constructor.
  static StyleRule* Create(base::span<CSSSelector> selectors) {
    return MakeGarbageCollected<StyleRule>(
        AdditionalBytesForSelectors(selectors.size()),
        base::PassKey<StyleRule>(), selectors);
  }

  // Creates a StyleRule with the selectors changed (used by setSelectorText()).
  static StyleRule* Create(base::span<CSSSelector> selectors,
                           StyleRule&& other) {
    return MakeGarbageCollected<StyleRule>(
        AdditionalBytesForSelectors(selectors.size()),
        base::PassKey<StyleRule>(), selectors, std::move(other));
  }

  // Constructors. Note that these expect that the StyleRule has been
  // allocated on the Oilpan heap, with <flattened_size> * sizeof(CSSSelector)
  // additional bytes after the StyleRule (flattened_size is the number of
  // selectors). Do not call them directly; they are public only so that
  // MakeGarbageCollected() can call them. Instead, use Create() above or
  // Copy() below, as appropriate.
  StyleRule(base::PassKey<StyleRule>,
            base::span<CSSSelector> selector_vector,
            CSSPropertyValueSet*);
  StyleRule(base::PassKey<StyleRule>,
            base::span<CSSSelector> selector_vector,
            CSSLazyPropertyParser*);
  // If you use this constructor, the object will not be fully constructed until
  // you call SetProperties().
  StyleRule(base::PassKey<StyleRule>, base::span<CSSSelector> selector_vector);
  StyleRule(base::PassKey<StyleRule>,
            base::span<CSSSelector> selector_vector,
            StyleRule&&);
  StyleRule(const StyleRule&, size_t flattened_size);
  StyleRule(const StyleRule&) = delete;
  ~StyleRule();

  void SetProperties(CSSPropertyValueSet* properties) {
    DCHECK_EQ(properties_.Get(), nullptr);
    properties_ = properties;
  }

  // Partial subset of the CSSSelector API.
  const CSSSelector* FirstSelector() const { return SelectorArray(); }
  const CSSSelector& SelectorAt(wtf_size_t index) const {
    return SelectorArray()[index];
  }
  CSSSelector& MutableSelectorAt(wtf_size_t index) {
    return SelectorArray()[index];
  }
  wtf_size_t SelectorIndex(const CSSSelector& selector) const {
    return static_cast<wtf_size_t>(&selector - FirstSelector());
  }
  wtf_size_t IndexOfNextSelectorAfter(wtf_size_t index) const {
    const CSSSelector& current = SelectorAt(index);
    const CSSSelector* next = CSSSelectorList::Next(current);
    if (!next) {
      return kNotFound;
    }
    return SelectorIndex(*next);
  }
  String SelectorsText() const {
    return CSSSelectorList::SelectorsText(FirstSelector());
  }

  const CSSPropertyValueSet& Properties() const;
  MutableCSSPropertyValueSet& MutableProperties();

  StyleRule* Copy() const {
    const CSSSelector* selector_array = SelectorArray();
    size_t flattened_size = 1;
    while (!selector_array[flattened_size - 1].IsLastInSelectorList()) {
      ++flattened_size;
    }
    return MakeGarbageCollected<StyleRule>(
        AdditionalBytesForSelectors(flattened_size), *this, flattened_size);
  }

  static unsigned AverageSizeInBytes();

  // Helper function to avoid parsing lazy properties when not needed.
  bool PropertiesHaveFailedOrCanceledSubresources() const;

  void TraceAfterDispatch(blink::Visitor*) const;

  const ChildRuleVector* ChildRules() const { return child_rule_vector_.Get(); }

  void EnsureChildRules() {
    // Allocate the child rule vector only when we need it,
    // since most rules won't have children (almost by definition).
    if (child_rule_vector_ == nullptr) {
      child_rule_vector_ = MakeGarbageCollected<ChildRuleVector>();
    }
  }

  // Note that if `child` is invisible (see CSSSelector::IsInvisible),
  // then the added child rule won't be visible through `ChildRules`.
  void AddChildRule(StyleRuleBase*);

  void WrapperInsertRule(unsigned index, StyleRuleBase* rule) {
    EnsureChildRules();
    child_rule_vector_->WrapperInsertRule(index, rule);
  }
  void WrapperRemoveRule(unsigned index) {
    child_rule_vector_->WrapperRemoveRule(index);
  }

 private:
  friend class StyleRuleBase;
  friend class CSSLazyParsingTest;

  bool HasParsedProperties() const;

  CSSSelector* SelectorArray() {
    return reinterpret_cast<CSSSelector*>(base::bits::AlignUp(
        reinterpret_cast<uint8_t*>(this + 1), alignof(CSSSelector)));
  }
  const CSSSelector* SelectorArray() const {
    return const_cast<StyleRule*>(this)->SelectorArray();
  }

  mutable Member<CSSPropertyValueSet> properties_;
  mutable Member<CSSLazyPropertyParser> lazy_property_parser_;
  Member<ChildRuleVector> child_rule_vector_;
};

class CORE_EXPORT StyleRuleFontFace : public StyleRuleBase {
 public:
  explicit StyleRuleFontFace(CSSPropertyValueSet*);
  StyleRuleFontFace(const StyleRuleFontFace&);

  const CSSPropertyValueSet& Properties() const { return *properties_; }
  MutableCSSPropertyValueSet& MutableProperties();

  StyleRuleFontFace* Copy() const {
    return MakeGarbageCollected<StyleRuleFontFace>(*this);
  }

  void SetCascadeLayer(const CascadeLayer* layer) { layer_ = layer; }
  const CascadeLayer* GetCascadeLayer() const { return layer_.Get(); }

  void TraceAfterDispatch(blink::Visitor*) const;

 private:
  Member<CSSPropertyValueSet> properties_;  // Cannot be null.
  Member<const CascadeLayer> layer_;
};

class CORE_EXPORT StyleRuleProperty : public StyleRuleBase {
 public:
  StyleRuleProperty(const String& name, CSSPropertyValueSet*);
  StyleRuleProperty(const StyleRuleProperty&);

  const CSSPropertyValueSet& Properties() const { return *properties_; }
  MutableCSSPropertyValueSet& MutableProperties();
  const String& GetName() const { return name_; }
  const CSSValue* GetSyntax() const;
  const CSSValue* Inherits() const;
  const CSSValue* GetInitialValue() const;

  bool SetNameText(const ExecutionContext* execution_context,
                   const String& name_text);

  void SetCascadeLayer(const CascadeLayer* layer) { layer_ = layer; }
  const CascadeLayer* GetCascadeLayer() const { return layer_.Get(); }

  StyleRuleProperty* Copy() const {
    return MakeGarbageCollected<StyleRuleProperty>(*this);
  }

  void TraceAfterDispatch(blink::Visitor*) const;

 private:
  String name_;
  Member<CSSPropertyValueSet> properties_;
  Member<const CascadeLayer> layer_;
};

class CORE_EXPORT StyleRuleGroup : public StyleRuleBase {
 public:
  const ChildRuleVector& ChildRules() const { return *child_rule_vector_; }
  ChildRuleVector& ChildRules() { return *child_rule_vector_; }

  void WrapperInsertRule(CSSStyleSheet*, unsigned, StyleRuleBase*);
  void WrapperRemoveRule(CSSStyleSheet*, unsigned);

  void TraceAfterDispatch(blink::Visitor*) const;

 protected:
  StyleRuleGroup(RuleType, HeapVector<Member<StyleRuleBase>> rules);
  StyleRuleGroup(const StyleRuleGroup&);

 private:
  Member<ChildRuleVector> child_rule_vector_;
};

class CORE_EXPORT StyleRuleScope : public StyleRuleGroup {
 public:
  StyleRuleScope(const StyleScope&, HeapVector<Member<StyleRuleBase>> rules);
  StyleRuleScope(const StyleRuleScope&);

  StyleRuleScope* Copy() const {
    return MakeGarbageCollected<StyleRuleScope>(*this);
  }

  void TraceAfterDispatch(blink::Visitor*) const;

  const StyleScope& GetStyleScope() const { return *style_scope_; }

  void SetPreludeText(const ExecutionContext*,
                      String,
                      CSSNestingType,
                      StyleRule* parent_rule_for_nesting,
                      bool is_within_scope,
                      StyleSheetContents* style_sheet);

 private:
  Member<const StyleScope> style_scope_;
};

// https://www.w3.org/TR/css-cascade-5/#layer-block
class CORE_EXPORT StyleRuleLayerBlock : public StyleRuleGroup {
 public:
  StyleRuleLayerBlock(LayerName&& name,
                      HeapVector<Member<StyleRuleBase>> rules);
  StyleRuleLayerBlock(const StyleRuleLayerBlock&);

  const LayerName& GetName() const { return name_; }
  String GetNameAsString() const;

  StyleRuleLayerBlock* Copy() const {
    return MakeGarbageCollected<StyleRuleLayerBlock>(*this);
  }

  void TraceAfterDispatch(blink::Visitor*) const;

 private:
  LayerName name_;
};

// https://www.w3.org/TR/css-cascade-5/#layer-empty
class CORE_EXPORT StyleRuleLayerStatement : public StyleRuleBase {
 public:
  explicit StyleRuleLayerStatement(Vector<LayerName>&& names);
  StyleRuleLayerStatement(const StyleRuleLayerStatement& other);

  const Vector<LayerName>& GetNames() const { return names_; }
  Vector<String> GetNamesAsStrings() const;

  StyleRuleLayerStatement* Copy() const {
    return MakeGarbageCollected<StyleRuleLayerStatement>(*this);
  }

  void TraceAfterDispatch(blink::Visitor*) const;

 private:
  Vector<LayerName> names_;
};

class StyleRulePage : public StyleRuleGroup {
 public:
  StyleRulePage(CSSSelectorList* selector_list,
                CSSPropertyValueSet* properties,
                HeapVector<Member<StyleRuleBase>> child_rules);
  StyleRulePage(const StyleRulePage&);

  const CSSSelector* Selector() const { return selector_list_->First(); }
  const CSSPropertyValueSet& Properties() const { return *properties_; }
  MutableCSSPropertyValueSet& MutableProperties();

  void WrapperAdoptSelectorList(CSSSelectorList* selectors) {
    selector_list_ = selectors;
  }

  StyleRulePage* Copy() const {
    return MakeGarbageCollected<StyleRulePage>(*this);
  }

  void SetCascadeLayer(const CascadeLayer* layer) { layer_ = layer; }
  const CascadeLayer* GetCascadeLayer() const { return layer_.Get(); }

  void TraceAfterDispatch(blink::Visitor*) const;

 private:
  Member<CSSPropertyValueSet> properties_;  // Cannot be null.
  Member<const CascadeLayer> layer_;
  Member<CSSSelectorList> selector_list_;
};

class StyleRulePageMargin : public StyleRuleBase {
 public:
  StyleRulePageMargin(CSSAtRuleID id, CSSPropertyValueSet* properties);
  StyleRulePageMargin(const StyleRulePageMargin&);

  const CSSPropertyValueSet& Properties() const { return *properties_; }
  MutableCSSPropertyValueSet& MutableProperties();
  CSSAtRuleID ID() const { return id_; }

  StyleRulePageMargin* Copy() const {
    return MakeGarbageCollected<StyleRulePageMargin>(*this);
  }

  void TraceAfterDispatch(blink::Visitor*) const;

 private:
  CSSAtRuleID id_;                          // What margin, e.g. @top-right.
  Member<CSSPropertyValueSet> properties_;  // Cannot be null.
};

// If you add new children of this class, remember to update IsConditionRule()
// above.
class CORE_EXPORT StyleRuleCondition : public StyleRuleGroup {
 public:
  String ConditionText() const { return condition_text_; }

  void TraceAfterDispatch(blink::Visitor* visitor) const {
    StyleRuleGroup::TraceAfterDispatch(visitor);
  }

 protected:
  StyleRuleCondition(RuleType, HeapVector<Member<StyleRuleBase>> rules);
  StyleRuleCondition(RuleType,
                     const String& condition_text,
                     HeapVector<Member<StyleRuleBase>> rules);
  StyleRuleCondition(const StyleRuleCondition&);
  String condition_text_;
};

class CORE_EXPORT StyleRuleMedia : public StyleRuleCondition {
 public:
  StyleRuleMedia(const MediaQuerySet*, HeapVector<Member<StyleRuleBase>> rules);
  StyleRuleMedia(const StyleRuleMedia&) = default;

  const MediaQuerySet* MediaQueries() const { return media_queries_.Get(); }

  void SetMediaQueries(const MediaQuerySet* media_queries) {
    media_queries_ = media_queries;
  }

  StyleRuleMedia* Copy() const {
    return MakeGarbageCollected<StyleRuleMedia>(*this);
  }

  void TraceAfterDispatch(blink::Visitor*) const;

 private:
  Member<const MediaQuerySet> media_queries_;
};

class CORE_EXPORT StyleRuleSupports : public StyleRuleCondition {
 public:
  StyleRuleSupports(const String& condition_text,
                    bool condition_is_supported,
                    HeapVector<Member<StyleRuleBase>> rules);
  StyleRuleSupports(const StyleRuleSupports&);

  bool ConditionIsSupported() const { return condition_is_supported_; }
  StyleRuleSupports* Copy() const {
    return MakeGarbageCollected<StyleRuleSupports>(*this);
  }

  void SetConditionText(const ExecutionContext*, String);

  void TraceAfterDispatch(blink::Visitor* visitor) const {
    StyleRuleCondition::TraceAfterDispatch(visitor);
  }

 private:
  bool condition_is_supported_;
};

class CORE_EXPORT StyleRuleContainer : public StyleRuleCondition {
 public:
  StyleRuleContainer(ContainerQuery&, HeapVector<Member<StyleRuleBase>> rules);
  StyleRuleContainer(const StyleRuleContainer&);

  ContainerQuery& GetContainerQuery() const { return *container_query_; }

  StyleRuleContainer* Copy() const {
    return MakeGarbageCollected<StyleRuleContainer>(*this);
  }

  void SetConditionText(const ExecutionContext*, String);

  void TraceAfterDispatch(blink::Visitor*) const;

 private:
  Member<ContainerQuery> container_query_;
};

class StyleRuleStartingStyle : public StyleRuleGroup {
 public:
  explicit StyleRuleStartingStyle(HeapVector<Member<StyleRuleBase>> rules);
  StyleRuleStartingStyle(const StyleRuleStartingStyle&) = default;

  StyleRuleStartingStyle* Copy() const {
    return MakeGarbageCollected<StyleRuleStartingStyle>(*this);
  }

  void TraceAfterDispatch(blink::Visitor* visitor) const {
    StyleRuleGroup::TraceAfterDispatch(visitor);
  }
};

// This should only be used within the CSS Parser
class StyleRuleCharset : public StyleRuleBase {
 public:
  StyleRuleCharset() : StyleRuleBase(kCharset) {}
  void TraceAfterDispatch(blink::Visitor* visitor) const {
    StyleRuleBase::TraceAfterDispatch(visitor);
  }

 private:
};

// An @function rule, representing a CSS function.
class CORE_EXPORT StyleRuleFunction : public StyleRuleBase {
 public:
  struct Type {
    CSSSyntaxDefinition syntax;

    // Whether this is a numeric type, that would be accepted by calc()
    // (see https://drafts.csswg.org/css-values/#calc-func). This is used
    // to allow the user to not have to write calc() around every single
    // expression, so that one could do e.g. --foo(2 + 2) instead of
    // --foo(calc(2 + 2)). Since writing calc() around an expression of
    // such a type will never change its meaning, and nested calc is allowed,
    // this is always safe even when not needed.
    bool should_add_implicit_calc;
  };
  struct Parameter {
    String name;
    Type type;
  };

  StyleRuleFunction(AtomicString name,
                    Vector<Parameter> parameters,
                    CSSVariableData* function_body,
                    Type return_type);
  StyleRuleFunction(const StyleRuleFunction&) = delete;

  const AtomicString& GetName() const { return name_; }
  const Vector<Parameter>& GetParameters() const { return parameters_; }
  CSSVariableData& GetFunctionBody() const { return *function_body_; }
  const Type& GetReturnType() const { return return_type_; }

  void TraceAfterDispatch(blink::Visitor*) const;

 private:
  AtomicString name_;
  Vector<Parameter> parameters_;
  Member<CSSVariableData> function_body_;
  Type return_type_;
};

template <>
struct DowncastTraits<StyleRule> {
  static bool AllowFrom(const StyleRuleBase& rule) {
    return rule.IsStyleRule();
  }
};

template <>
struct DowncastTraits<StyleRuleFontFace> {
  static bool AllowFrom(const StyleRuleBase& rule) {
    return rule.IsFontFaceRule();
  }
};

template <>
struct DowncastTraits<StyleRulePage> {
  static bool AllowFrom(const StyleRuleBase& rule) { return rule.IsPageRule(); }
};

template <>
struct DowncastTraits<StyleRulePageMargin> {
  static bool AllowFrom(const StyleRuleBase& rule) {
    return rule.IsPageRuleMargin();
  }
};

template <>
struct DowncastTraits<StyleRuleProperty> {
  static bool AllowFrom(const StyleRuleBase& rule) {
    return rule.IsPropertyRule();
  }
};

template <>
struct DowncastTraits<StyleRuleScope> {
  static bool AllowFrom(const StyleRuleBase& rule) {
    return rule.IsScopeRule();
  }
};

template <>
struct DowncastTraits<StyleRuleGroup> {
  static bool AllowFrom(const StyleRuleBase& rule) {
    return rule.IsMediaRule() || rule.IsSupportsRule() ||
           rule.IsContainerRule() || rule.IsLayerBlockRule() ||
           rule.IsScopeRule() || rule.IsStartingStyleRule();
  }
};

template <>
struct DowncastTraits<StyleRuleLayerBlock> {
  static bool AllowFrom(const StyleRuleBase& rule) {
    return rule.IsLayerBlockRule();
  }
};

template <>
struct DowncastTraits<StyleRuleLayerStatement> {
  static bool AllowFrom(const StyleRuleBase& rule) {
    return rule.IsLayerStatementRule();
  }
};

template <>
struct DowncastTraits<StyleRuleMedia> {
  static bool AllowFrom(const StyleRuleBase& rule) {
    return rule.IsMediaRule();
  }
};

template <>
struct DowncastTraits<StyleRuleSupports> {
  static bool AllowFrom(const StyleRuleBase& rule) {
    return rule.IsSupportsRule();
  }
};

template <>
struct DowncastTraits<StyleRuleContainer> {
  static bool AllowFrom(const StyleRuleBase& rule) {
    return rule.IsContainerRule();
  }
};

template <>
struct DowncastTraits<StyleRuleCharset> {
  static bool AllowFrom(const StyleRuleBase& rule) {
    return rule.IsCharsetRule();
  }
};

template <>
struct DowncastTraits<StyleRuleStartingStyle> {
  static bool AllowFrom(const StyleRuleBase& rule) {
    return rule.IsStartingStyleRule();
  }
};

template <>
struct DowncastTraits<StyleRuleFunction> {
  static bool AllowFrom(const StyleRuleBase& rule) {
    return rule.IsFunctionRule();
  }
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_CSS_STYLE_RULE_H_
