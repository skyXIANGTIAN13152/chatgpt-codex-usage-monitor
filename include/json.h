#pragma once

#include "common.h"
#include <map>
#include <variant>

namespace monitor {

class JsonValue {
 public:
  using Object = std::map<std::string, JsonValue, std::less<>>;
  using Array = std::vector<JsonValue>;
  using Storage = std::variant<std::nullptr_t, bool, double, std::string, Object, Array>;

  JsonValue() : value_(nullptr) {}
  explicit JsonValue(std::nullptr_t) : value_(nullptr) {}
  explicit JsonValue(bool value) : value_(value) {}
  explicit JsonValue(double value) : value_(value) {}
  explicit JsonValue(std::string value) : value_(std::move(value)) {}
  explicit JsonValue(Object value) : value_(std::move(value)) {}
  explicit JsonValue(Array value) : value_(std::move(value)) {}

  bool IsNull() const { return std::holds_alternative<std::nullptr_t>(value_); }
  bool IsBool() const { return std::holds_alternative<bool>(value_); }
  bool IsNumber() const { return std::holds_alternative<double>(value_); }
  bool IsString() const { return std::holds_alternative<std::string>(value_); }
  bool IsObject() const { return std::holds_alternative<Object>(value_); }
  bool IsArray() const { return std::holds_alternative<Array>(value_); }

  const Object* AsObject() const { return std::get_if<Object>(&value_); }
  const Array* AsArray() const { return std::get_if<Array>(&value_); }
  const std::string* AsString() const { return std::get_if<std::string>(&value_); }
  std::optional<double> AsNumber() const;
  std::optional<bool> AsBool() const;
  const JsonValue* Find(std::string_view key) const;

 private:
  Storage value_;
};

struct JsonParseResult {
  JsonValue value;
  std::string error;
  size_t errorOffset = 0;
  explicit operator bool() const { return error.empty(); }
};

JsonParseResult ParseJson(std::string_view input);
std::string EscapeJsonString(std::string_view input);

}  // namespace monitor

