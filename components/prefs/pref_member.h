// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// A helper class that stays in sync with a preference (bool, int, real,
// string or filepath).  For example:
//
// class MyClass {
//  public:
//   MyClass(PrefService* prefs) {
//     my_string_.Init(prefs::kHomePage, prefs);
//   }
//  private:
//   StringPrefMember my_string_;
// };
//
// my_string_ should stay in sync with the prefs::kHomePage pref and will
// update if either the pref changes or if my_string_.SetValue is called.
//
// An optional observer can be passed into the Init method which can be used to
// notify MyClass of changes. Note that if you use SetValue(), the observer
// will not be notified.

#ifndef COMPONENTS_PREFS_PREF_MEMBER_H_
#define COMPONENTS_PREFS_PREF_MEMBER_H_

#include <string>
#include <vector>

#include "base/check.h"
#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "base/functional/callback_forward.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/ref_counted.h"
#include "base/task/sequenced_task_runner.h"
#include "base/values.h"
#include "components/prefs/pref_observer.h"
#include "components/prefs/prefs_export.h"

class PrefService;

namespace subtle {

class COMPONENTS_PREFS_EXPORT PrefMemberBase : public PrefObserver {
 public:
  // Type of callback you can register if you need to know the name of
  // the pref that is changing.
  using NamedChangeCallback = base::RepeatingCallback<void(const std::string&)>;

  PrefService* prefs() { return prefs_; }
  const PrefService* prefs() const { return prefs_; }

 protected:
  class COMPONENTS_PREFS_EXPORT Internal
      : public base::RefCountedThreadSafe<Internal> {
   public:
    Internal();

    Internal(const Internal&) = delete;
    Internal& operator=(const Internal&) = delete;

    // Update the value, either by calling |UpdateValueInternal| directly
    // or by dispatching to the right sequence.
    // Takes ownership of |value|.
    void UpdateValue(base::Value* value,
                     bool is_managed,
                     bool is_user_modifiable,
                     bool is_default_value,
                     base::OnceClosure callback) const;

    void MoveToSequence(scoped_refptr<base::SequencedTaskRunner> task_runner);

    // See PrefMember<> for description.
    bool IsManaged() const { return is_managed_; }
    bool IsUserModifiable() const { return is_user_modifiable_; }
    bool IsDefaultValue() const { return is_default_value_; }

   protected:
    friend class base::RefCountedThreadSafe<Internal>;
    virtual ~Internal();

    void CheckOnCorrectSequence() const { DCHECK(IsOnCorrectSequence()); }

   private:
    // This method actually updates the value. It should only be called from
    // the sequence the PrefMember is on.
    virtual bool UpdateValueInternal(const base::Value& value) const = 0;

    bool IsOnCorrectSequence() const;

    scoped_refptr<base::SequencedTaskRunner> owning_task_runner_;
    mutable bool is_managed_ = false;
    mutable bool is_user_modifiable_ = false;
    mutable bool is_default_value_ = false;
  };

  PrefMemberBase();
  virtual ~PrefMemberBase();

  // See PrefMember<> for description.
  void Init(const std::string& pref_name,
            PrefService* prefs,
            const NamedChangeCallback& observer);
  void Init(const std::string& pref_name, PrefService* prefs);

  virtual void CreateInternal() const = 0;

  // See PrefMember<> for description.
  void Destroy();

  void MoveToSequence(scoped_refptr<base::SequencedTaskRunner> task_runner);

  // PrefObserver
  void OnPreferenceChanged(PrefService* service,
                           const std::string& pref_name) override;

  void VerifyValuePrefName() const {
    DCHECK(!pref_name_.empty());
  }

  // This method is used to do the actual sync with the preference.
  // Note: it is logically const, because it doesn't modify the state
  // seen by the outside world. It is just doing a lazy load behind the scenes.
  void UpdateValueFromPref(base::OnceClosure callback) const;

  // Verifies the preference name, and lazily loads the preference value if
  // it hasn't been loaded yet.
  void VerifyPref() const;

  const std::string& pref_name() const { return pref_name_; }

  virtual Internal* internal() const = 0;

  // Used to allow registering plain base::RepeatingClosure callbacks.
  static void InvokeUnnamedCallback(const base::RepeatingClosure& callback,
                                    const std::string& pref_name);

 private:
  // Ordered the members to compact the class instance.
  std::string pref_name_;
  NamedChangeCallback observer_;
  raw_ptr<PrefService> prefs_;

 protected:
  bool setting_value_;
};

// This function implements StringListPrefMember::UpdateValue().
// It is exposed here for testing purposes.
bool COMPONENTS_PREFS_EXPORT PrefMemberVectorStringUpdate(
    const base::Value& value,
    std::vector<std::string>* string_vector);

}  // namespace subtle

template <typename ValueType>
class PrefMember : public subtle::PrefMemberBase {
 public:
  // Defer initialization to an Init method so it's easy to make this class be
  // a member variable.
  PrefMember() {}

  PrefMember(const PrefMember&) = delete;
  PrefMember& operator=(const PrefMember&) = delete;

  virtual ~PrefMember() {}

  // Do the actual initialization of the class.  Use the two-parameter
  // version if you don't want any notifications of changes.  This
  // method should only be called on the UI thread.
  void Init(const std::string& pref_name,
            PrefService* prefs,
            const NamedChangeCallback& observer) {
    subtle::PrefMemberBase::Init(pref_name, prefs, observer);
  }
  void Init(const std::string& pref_name,
            PrefService* prefs,
            const base::RepeatingClosure& observer) {
    subtle::PrefMemberBase::Init(
        pref_name, prefs,
        base::BindRepeating(&PrefMemberBase::InvokeUnnamedCallback, observer));
  }
  void Init(const std::string& pref_name, PrefService* prefs) {
    subtle::PrefMemberBase::Init(pref_name, prefs);
  }

  // Unsubscribes the PrefMember from the PrefService. After calling this
  // function, the PrefMember may not be used any more on the UI thread.
  // Assuming |MoveToSequence| was previously called, |GetValue|, |IsManaged|,
  // and |IsUserModifiable| can still be called from the other sequence but
  // the results will no longer update from the PrefService.
  // This method should only be called on the UI thread.
  void Destroy() {
    subtle::PrefMemberBase::Destroy();
  }

  // Moves the PrefMember to another sequence, allowing read accesses from
  // there. Changes from the PrefService will be propagated asynchronously
  // via PostTask.
  // This method should only be used from the sequence the PrefMember is
  // currently on, which is the UI thread by default.
  void MoveToSequence(scoped_refptr<base::SequencedTaskRunner> task_runner) {
    subtle::PrefMemberBase::MoveToSequence(task_runner);
  }

  // Check whether the pref is managed, i.e. controlled externally through
  // enterprise configuration management (e.g. windows group policy). Returns
  // false for unknown prefs.
  // This method should only be used from the sequence the PrefMember is
  // currently on, which is the UI thread unless changed by |MoveToSequence|.
  bool IsManaged() const {
    VerifyPref();
    return internal_->IsManaged();
  }

  // Checks whether the pref can be modified by the user. This returns false
  // when the pref is managed by a policy or an extension, and when a command
  // line flag overrides the pref.
  // This method should only be used from the sequence the PrefMember is
  // currently on, which is the UI thread unless changed by |MoveToSequence|.
  bool IsUserModifiable() const {
    VerifyPref();
    return internal_->IsUserModifiable();
  }

  // Checks whether the pref is currently using its default value, and has not
  // been set by any higher-priority source (even with the same value). This
  // method should only be used from the sequence the PrefMember is currently
  // on, which is the UI thread unless changed by |MoveToSequence|.
  bool IsDefaultValue() const {
    VerifyPref();
    return internal_->IsDefaultValue();
  }

  // Retrieve the value of the member variable.
  // This method should only be used from the sequence the PrefMember is
  // currently on, which is the UI thread unless changed by |MoveToSequence|.
  ValueType GetValue() const {
    VerifyPref();
    return internal_->value();
  }

  // Provided as a convenience.
  ValueType operator*() const {
    return GetValue();
  }

  // Set the value of the member variable.
  // This method should only be called on the UI thread.
  void SetValue(const ValueType& value) {
    VerifyValuePrefName();
    setting_value_ = true;
    UpdatePref(value);
    setting_value_ = false;
  }

  // Returns the pref name.
  const std::string& GetPrefName() const {
    return pref_name();
  }

 private:
  class Internal : public subtle::PrefMemberBase::Internal {
   public:
    Internal() : value_(ValueType()) {}

    Internal(const Internal&) = delete;
    Internal& operator=(const Internal&) = delete;

    ValueType value() {
      CheckOnCorrectSequence();
      return value_;
    }

   protected:
    ~Internal() override {}

    COMPONENTS_PREFS_EXPORT bool UpdateValueInternal(
        const base::Value& value) const override;

    // We cache the value of the pref so we don't have to keep walking the pref
    // tree.
    mutable ValueType value_;
  };

  Internal* internal() const override { return internal_.get(); }
  void CreateInternal() const override { internal_ = new Internal(); }

  // This method is used to do the actual sync with pref of the specified type.
  void COMPONENTS_PREFS_EXPORT UpdatePref(const ValueType& value);

  mutable scoped_refptr<Internal> internal_;
};

// Declaration of template specialization need to be repeated here
// specifically for each specialization (rather than just once above)
// or at least one of our compilers won't be happy in all cases.
// Specifically, it was failing on ChromeOS with a complaint about
// PrefMember<FilePath>::UpdateValueInternal not being defined when
// built in a chroot with the following parameters:
//
// FEATURES="noclean nostrip" USE="-chrome_debug -chrome_remoting
// -chrome_internal -chrome_pdf component_build"
// cros_chrome_make --board=${BOARD}
// --install --runhooks

template <>
COMPONENTS_PREFS_EXPORT void PrefMember<bool>::UpdatePref(const bool& value);

template <>
COMPONENTS_PREFS_EXPORT bool PrefMember<bool>::Internal::UpdateValueInternal(
    const base::Value& value) const;

template <>
COMPONENTS_PREFS_EXPORT void PrefMember<int>::UpdatePref(const int& value);

template <>
COMPONENTS_PREFS_EXPORT bool PrefMember<int>::Internal::UpdateValueInternal(
    const base::Value& value) const;

template <>
COMPONENTS_PREFS_EXPORT void
PrefMember<double>::UpdatePref(const double& value);

template <>
COMPONENTS_PREFS_EXPORT bool PrefMember<double>::Internal::UpdateValueInternal(
    const base::Value& value) const;

template <>
COMPONENTS_PREFS_EXPORT void PrefMember<std::string>::UpdatePref(
    const std::string& value);

template <>
COMPONENTS_PREFS_EXPORT bool
PrefMember<std::string>::Internal::UpdateValueInternal(
    const base::Value& value) const;

template <>
COMPONENTS_PREFS_EXPORT void PrefMember<base::FilePath>::UpdatePref(
    const base::FilePath& value);

template <>
COMPONENTS_PREFS_EXPORT bool
PrefMember<base::FilePath>::Internal::UpdateValueInternal(
    const base::Value& value) const;

template <>
COMPONENTS_PREFS_EXPORT void PrefMember<std::vector<std::string>>::UpdatePref(
    const std::vector<std::string>& value);

template <>
COMPONENTS_PREFS_EXPORT bool
PrefMember<std::vector<std::string>>::Internal::UpdateValueInternal(
    const base::Value& value) const;

typedef PrefMember<bool> BooleanPrefMember;
typedef PrefMember<int> IntegerPrefMember;
typedef PrefMember<double> DoublePrefMember;
typedef PrefMember<std::string> StringPrefMember;
typedef PrefMember<base::FilePath> FilePathPrefMember;
// This preference member is expensive for large string arrays.
typedef PrefMember<std::vector<std::string>> StringListPrefMember;

#endif  // COMPONENTS_PREFS_PREF_MEMBER_H_
