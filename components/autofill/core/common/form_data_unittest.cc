// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/autofill/core/common/form_data.h"

#include <stddef.h>

#include "base/pickle.h"
#include "base/strings/utf_string_conversions.h"
#include "components/autofill/core/common/form_field_data.h"
#include "components/autofill/core/common/unique_ids.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace autofill {

namespace {

// This function serializes the form data into the pickle in version one format.
// It should always be possible to deserialize it using DeserializeFormData(),
// even when version changes. See kPickleVersion in form_data.cc.
void SerializeInVersion1Format(const FormData& form_data,
                               base::Pickle* pickle) {
  pickle->WriteInt(1);
  pickle->WriteString16(form_data.name());
  std::u16string method(u"POST");
  pickle->WriteString16(method);
  pickle->WriteString(form_data.url().spec());
  pickle->WriteString(form_data.action().spec());
  pickle->WriteBool(true);  // Used to be |user_submitted|, which was removed.
  pickle->WriteInt(static_cast<int>(form_data.fields.size()));
  for (size_t i = 0; i < form_data.fields.size(); ++i) {
    SerializeFormFieldData(form_data.fields[i], pickle);
  }
}

void SerializeInVersion2Format(const FormData& form_data,
                               base::Pickle* pickle) {
  pickle->WriteInt(2);
  pickle->WriteString16(form_data.name());
  pickle->WriteString(form_data.url().spec());
  pickle->WriteString(form_data.action().spec());
  pickle->WriteBool(true);  // Used to be |user_submitted|, which was removed.
  pickle->WriteInt(static_cast<int>(form_data.fields.size()));
  for (size_t i = 0; i < form_data.fields.size(); ++i) {
    SerializeFormFieldData(form_data.fields[i], pickle);
  }
}

void SerializeInVersion3Format(const FormData& form_data,
                               base::Pickle* pickle) {
  pickle->WriteInt(3);
  pickle->WriteString16(form_data.name());
  pickle->WriteString(form_data.url().spec());
  pickle->WriteString(form_data.action().spec());
  pickle->WriteBool(true);  // Used to be |user_submitted|, which was removed.
  pickle->WriteInt(static_cast<int>(form_data.fields.size()));
  for (size_t i = 0; i < form_data.fields.size(); ++i) {
    SerializeFormFieldData(form_data.fields[i], pickle);
  }
  pickle->WriteBool(false);  // Used to be `is_form_tag`, which was removed
}

void SerializeInVersion4Format(const FormData& form_data,
                               base::Pickle* pickle) {
  pickle->WriteInt(4);
  pickle->WriteString16(form_data.name());
  pickle->WriteString(form_data.url().spec());
  pickle->WriteString(form_data.action().spec());
  pickle->WriteInt(static_cast<int>(form_data.fields.size()));
  for (size_t i = 0; i < form_data.fields.size(); ++i) {
    SerializeFormFieldData(form_data.fields[i], pickle);
  }
  pickle->WriteBool(false);  // Used to be `is_form_tag`, which was removed
}

void SerializeInVersion5Format(const FormData& form_data,
                               base::Pickle* pickle) {
  pickle->WriteInt(5);
  pickle->WriteString16(form_data.name());
  pickle->WriteString(form_data.url().spec());
  pickle->WriteString(form_data.action().spec());
  pickle->WriteInt(static_cast<int>(form_data.fields.size()));
  for (size_t i = 0; i < form_data.fields.size(); ++i) {
    SerializeFormFieldData(form_data.fields[i], pickle);
  }
  pickle->WriteBool(false);  // Used to be `is_form_tag`, which was removed
  pickle->WriteBool(/*is_formless_checkout=*/true);
}

void SerializeInVersion6Format(const FormData& form_data,
                               base::Pickle* pickle) {
  pickle->WriteInt(6);
  pickle->WriteString16(form_data.name());
  pickle->WriteString(form_data.url().spec());
  pickle->WriteString(form_data.action().spec());
  pickle->WriteInt(static_cast<int>(form_data.fields.size()));
  for (size_t i = 0; i < form_data.fields.size(); ++i) {
    SerializeFormFieldData(form_data.fields[i], pickle);
  }
  pickle->WriteBool(false);  // Used to be `is_form_tag`, which was removed
  pickle->WriteBool(/*is_formless_checkout=*/true);
  pickle->WriteString(form_data.main_frame_origin().Serialize());
}

void SerializeInVersion7Format(const FormData& form_data,
                               base::Pickle* pickle) {
  pickle->WriteInt(7);
  pickle->WriteString16(form_data.name());
  pickle->WriteString(form_data.url().spec());
  pickle->WriteString(form_data.action().spec());
  pickle->WriteInt(static_cast<int>(form_data.fields.size()));
  for (size_t i = 0; i < form_data.fields.size(); ++i) {
    SerializeFormFieldData(form_data.fields[i], pickle);
  }
  pickle->WriteBool(false);  // Used to be `is_form_tag`, which was removed
  pickle->WriteString(form_data.main_frame_origin().Serialize());
}

void SerializeInVersion8Format(const FormData& form_data,
                               base::Pickle* pickle) {
  pickle->WriteInt(8);
  pickle->WriteString16(form_data.name());
  pickle->WriteString(form_data.url().spec());
  pickle->WriteString(form_data.action().spec());
  pickle->WriteInt(static_cast<int>(form_data.fields.size()));
  for (size_t i = 0; i < form_data.fields.size(); ++i) {
    SerializeFormFieldData(form_data.fields[i], pickle);
  }
  pickle->WriteString(form_data.main_frame_origin().Serialize());
}

// This function serializes the form data into the pickle in incorrect format
// (no version number).
void SerializeIncorrectFormat(const FormData& form_data, base::Pickle* pickle) {
  pickle->WriteString16(form_data.name());
  pickle->WriteString(form_data.url().spec());
  pickle->WriteString(form_data.action().spec());
  pickle->WriteBool(true);  // Used to be |user_submitted|, which was removed.
  pickle->WriteInt(static_cast<int>(form_data.fields.size()));
  for (size_t i = 0; i < form_data.fields.size(); ++i) {
    SerializeFormFieldData(form_data.fields[i], pickle);
  }
}

void FillInDummyFormData(FormData* data) {
  data->set_name(u"name");
  data->set_url(GURL("https://example.com"));
  data->set_action(GURL("https://example.com/action"));
  data->set_main_frame_origin(
      url::Origin::Create(GURL("https://origin-example.com")));

  FormFieldData field_data;
  field_data.set_label(u"label");
  field_data.set_name(u"name");
  field_data.set_value(u"value");
  field_data.set_form_control_type(FormControlType::kInputPassword);
  field_data.set_autocomplete_attribute("off");
  field_data.set_max_length(200);
  field_data.set_is_autofilled(true);
  field_data.set_check_status(FormFieldData::CheckStatus::kChecked);
  field_data.set_is_focusable(true);
  field_data.set_should_autocomplete(false);
  field_data.set_text_direction(base::i18n::RIGHT_TO_LEFT);
  field_data.set_options({{.value = u"First", .content = u"First"},
                          {.value = u"Second", .content = u"Second"}});
  data->fields.push_back(field_data);

  // Change a few fields.
  field_data.set_max_length(150);
  field_data.set_options({{.value = u"Third", .content = u"Third"}});
  data->fields.push_back(field_data);
}

}  // namespace

TEST(FormDataTest, SerializeAndDeserialize) {
  FormData data;
  FillInDummyFormData(&data);

  base::Pickle pickle;
  SerializeFormData(data, &pickle);

  base::PickleIterator iter(pickle);
  FormData actual;
  EXPECT_TRUE(DeserializeFormData(&iter, &actual));

  EXPECT_TRUE(actual.SameFormAs(data));
}

TEST(FormDataTest, Serialize_v1_Deserialize_vCurrent) {
  FormData data;
  FillInDummyFormData(&data);

  base::Pickle pickle;
  SerializeInVersion1Format(data, &pickle);

  base::PickleIterator iter(pickle);
  FormData actual;
  EXPECT_TRUE(DeserializeFormData(&iter, &actual));

  EXPECT_TRUE(actual.SameFormAs(data));
}

TEST(FormDataTest, Serialize_v2_Deserialize_vCurrent) {
  FormData data;
  FillInDummyFormData(&data);

  base::Pickle pickle;
  SerializeInVersion2Format(data, &pickle);

  base::PickleIterator iter(pickle);
  FormData actual;
  EXPECT_TRUE(DeserializeFormData(&iter, &actual));

  EXPECT_TRUE(actual.SameFormAs(data));
}

TEST(FormDataTest, Serialize_v3_Deserialize_vCurrent) {
  FormData data;
  FillInDummyFormData(&data);

  base::Pickle pickle;
  SerializeInVersion3Format(data, &pickle);

  base::PickleIterator iter(pickle);
  FormData actual;
  EXPECT_TRUE(DeserializeFormData(&iter, &actual));

  EXPECT_TRUE(actual.SameFormAs(data));
}

TEST(FormDataTest, Serialize_v3_Deserialize_vCurrent_IsFormTagFalse) {
  FormData data;
  FillInDummyFormData(&data);
  data.set_renderer_id(FormRendererId());

  base::Pickle pickle;
  SerializeInVersion3Format(data, &pickle);

  base::PickleIterator iter(pickle);
  FormData actual;
  EXPECT_TRUE(DeserializeFormData(&iter, &actual));

  EXPECT_TRUE(actual.SameFormAs(data));
}

TEST(FormDataTest, Serialize_v4_Deserialize_vCurrent) {
  FormData data;
  FillInDummyFormData(&data);

  base::Pickle pickle;
  SerializeInVersion4Format(data, &pickle);

  base::PickleIterator iter(pickle);
  FormData actual;
  EXPECT_TRUE(DeserializeFormData(&iter, &actual));

  EXPECT_TRUE(actual.SameFormAs(data));
}

TEST(FormDataTest, Serialize_v5_Deserialize_vCurrent) {
  FormData data;
  FillInDummyFormData(&data);

  base::Pickle pickle;
  SerializeInVersion5Format(data, &pickle);

  base::PickleIterator iter(pickle);
  FormData actual;
  EXPECT_TRUE(DeserializeFormData(&iter, &actual));

  EXPECT_TRUE(actual.SameFormAs(data));
}

TEST(FormDataTest, Serialize_v6_Deserialize_vCurrent) {
  FormData data;
  FillInDummyFormData(&data);

  base::Pickle pickle;
  SerializeInVersion6Format(data, &pickle);

  base::PickleIterator iter(pickle);
  FormData actual;
  EXPECT_TRUE(DeserializeFormData(&iter, &actual));

  EXPECT_TRUE(actual.SameFormAs(data));
}

TEST(FormDataTest, Serialize_v7_Deserialize_vCurrent) {
  FormData data;
  FillInDummyFormData(&data);

  base::Pickle pickle;
  SerializeInVersion7Format(data, &pickle);

  base::PickleIterator iter(pickle);
  FormData actual;
  EXPECT_TRUE(DeserializeFormData(&iter, &actual));

  EXPECT_TRUE(actual.SameFormAs(data));
}

TEST(FormDataTest, Serialize_v8_Deserialize_vCurrent) {
  FormData data;
  FillInDummyFormData(&data);

  base::Pickle pickle;
  SerializeInVersion8Format(data, &pickle);

  base::PickleIterator iter(pickle);
  FormData actual;
  EXPECT_TRUE(DeserializeFormData(&iter, &actual));

  EXPECT_TRUE(actual.SameFormAs(data));
}

TEST(FormDataTest, SerializeIncorrectFormatAndDeserialize) {
  FormData data;
  FillInDummyFormData(&data);

  base::Pickle pickle;
  SerializeIncorrectFormat(data, &pickle);

  base::PickleIterator iter(pickle);
  FormData actual;
  EXPECT_FALSE(DeserializeFormData(&iter, &actual));

  FormData empty;
  EXPECT_TRUE(actual.SameFormAs(empty));
}

}  // namespace autofill
