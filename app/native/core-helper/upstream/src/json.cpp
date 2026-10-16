#include "json.h"
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <sstream>

namespace monitor {

std::optional<double> JsonValue::AsNumber() const {
  if (const auto* value = std::get_if<double>(&value_)) return *value;
  return std::nullopt;
}

std::optional<bool> JsonValue::AsBool() const {
  if (const auto* value = std::get_if<bool>(&value_)) return *value;
  return std::nullopt;
}

const JsonValue* JsonValue::Find(std::string_view key) const {
  const auto* object = AsObject();
  if (!object) return nullptr;
  const auto it = object->find(key);
  return it == object->end() ? nullptr : &it->second;
}

namespace {

void AppendUtf8(std::string& output, uint32_t codepoint) {
  if (codepoint <= 0x7f) {
    output.push_back(static_cast<char>(codepoint));
  } else if (codepoint <= 0x7ff) {
    output.push_back(static_cast<char>(0xc0 | (codepoint >> 6)));
    output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
  } else if (codepoint <= 0xffff) {
    output.push_back(static_cast<char>(0xe0 | (codepoint >> 12)));
    output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
    output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
  } else {
    output.push_back(static_cast<char>(0xf0 | (codepoint >> 18)));
    output.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f)));
    output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
    output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
  }
}

class Parser {
 public:
  explicit Parser(std::string_view input) : input_(input) {}

  JsonParseResult Run() {
    JsonValue value;
    SkipWhitespace();
    if (!ParseValue(&value, 0)) return {JsonValue(), error_, pos_};
    SkipWhitespace();
    if (pos_ != input_.size()) return Error("unexpected trailing content");
    return {std::move(value), {}, 0};
  }

 private:
  JsonParseResult Error(std::string message) {
    return {JsonValue(), std::move(message), pos_};
  }

  void Fail(std::string message) {
    if (error_.empty()) error_ = std::move(message);
  }

  void SkipWhitespace() {
    while (pos_ < input_.size()) {
      const char c = input_[pos_];
      if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
      ++pos_;
    }
  }

  bool ParseValue(JsonValue* output, int depth) {
    if (depth > 64) { Fail("maximum nesting exceeded"); return false; }
    SkipWhitespace();
    if (pos_ >= input_.size()) { Fail("unexpected end of input"); return false; }
    const char c = input_[pos_];
    if (c == '{') return ParseObject(output, depth + 1);
    if (c == '[') return ParseArray(output, depth + 1);
    if (c == '"') {
      std::string value;
      if (!ParseString(&value)) return false;
      *output = JsonValue(std::move(value));
      return true;
    }
    if (c == 't' && Consume("true")) { *output = JsonValue(true); return true; }
    if (c == 'f' && Consume("false")) { *output = JsonValue(false); return true; }
    if (c == 'n' && Consume("null")) { *output = JsonValue(nullptr); return true; }
    if (c == '-' || (c >= '0' && c <= '9')) return ParseNumber(output);
    Fail("invalid JSON value");
    return false;
  }

  bool Consume(std::string_view literal) {
    if (input_.substr(pos_, literal.size()) != literal) return false;
    pos_ += literal.size();
    return true;
  }

  bool ParseObject(JsonValue* output, int depth) {
    ++pos_;
    JsonValue::Object object;
    SkipWhitespace();
    if (pos_ < input_.size() && input_[pos_] == '}') {
      ++pos_;
      *output = JsonValue(std::move(object));
      return true;
    }
    while (pos_ < input_.size()) {
      std::string key;
      if (!ParseString(&key)) return false;
      SkipWhitespace();
      if (pos_ >= input_.size() || input_[pos_] != ':') {
        Fail("expected ':' after object key");
        return false;
      }
      ++pos_;
      JsonValue value;
      if (!ParseValue(&value, depth)) return false;
      object.insert_or_assign(std::move(key), std::move(value));
      SkipWhitespace();
      if (pos_ >= input_.size()) { Fail("unterminated object"); return false; }
      const char delimiter = input_[pos_++];
      if (delimiter == '}') {
        *output = JsonValue(std::move(object));
        return true;
      }
      if (delimiter != ',') { Fail("expected ',' or '}'"); return false; }
      SkipWhitespace();
    }
    Fail("unterminated object");
    return false;
  }

  bool ParseArray(JsonValue* output, int depth) {
    ++pos_;
    JsonValue::Array array;
    SkipWhitespace();
    if (pos_ < input_.size() && input_[pos_] == ']') {
      ++pos_;
      *output = JsonValue(std::move(array));
      return true;
    }
    while (pos_ < input_.size()) {
      JsonValue value;
      if (!ParseValue(&value, depth)) return false;
      array.push_back(std::move(value));
      SkipWhitespace();
      if (pos_ >= input_.size()) { Fail("unterminated array"); return false; }
      const char delimiter = input_[pos_++];
      if (delimiter == ']') {
        *output = JsonValue(std::move(array));
        return true;
      }
      if (delimiter != ',') { Fail("expected ',' or ']'"); return false; }
      SkipWhitespace();
    }
    Fail("unterminated array");
    return false;
  }

  static int Hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  }

  bool ParseHex4(uint32_t* value) {
    if (pos_ + 4 > input_.size()) return false;
    uint32_t result = 0;
    for (int i = 0; i < 4; ++i) {
      const int digit = Hex(input_[pos_++]);
      if (digit < 0) return false;
      result = (result << 4) | static_cast<uint32_t>(digit);
    }
    *value = result;
    return true;
  }

  bool ParseString(std::string* output) {
    SkipWhitespace();
    if (pos_ >= input_.size() || input_[pos_] != '"') {
      Fail("expected string");
      return false;
    }
    ++pos_;
    std::string result;
    while (pos_ < input_.size()) {
      const unsigned char c = static_cast<unsigned char>(input_[pos_++]);
      if (c == '"') { *output = std::move(result); return true; }
      if (c < 0x20) { Fail("control character in string"); return false; }
      if (c != '\\') { result.push_back(static_cast<char>(c)); continue; }
      if (pos_ >= input_.size()) { Fail("invalid string escape"); return false; }
      const char escaped = input_[pos_++];
      switch (escaped) {
        case '"': result.push_back('"'); break;
        case '\\': result.push_back('\\'); break;
        case '/': result.push_back('/'); break;
        case 'b': result.push_back('\b'); break;
        case 'f': result.push_back('\f'); break;
        case 'n': result.push_back('\n'); break;
        case 'r': result.push_back('\r'); break;
        case 't': result.push_back('\t'); break;
        case 'u': {
          uint32_t cp = 0;
          if (!ParseHex4(&cp)) { Fail("invalid unicode escape"); return false; }
          if (cp >= 0xd800 && cp <= 0xdbff) {
            if (pos_ + 2 > input_.size() || input_[pos_] != '\\' || input_[pos_ + 1] != 'u') {
              Fail("unpaired high surrogate"); return false;
            }
            pos_ += 2;
            uint32_t low = 0;
            if (!ParseHex4(&low) || low < 0xdc00 || low > 0xdfff) {
              Fail("invalid low surrogate"); return false;
            }
            cp = 0x10000 + ((cp - 0xd800) << 10) + (low - 0xdc00);
          } else if (cp >= 0xdc00 && cp <= 0xdfff) {
            Fail("unpaired low surrogate"); return false;
          }
          AppendUtf8(result, cp);
          break;
        }
        default: Fail("unknown string escape"); return false;
      }
    }
    Fail("unterminated string");
    return false;
  }

  bool ParseNumber(JsonValue* output) {
    const size_t start = pos_;
    if (input_[pos_] == '-') ++pos_;
    if (pos_ >= input_.size()) { Fail("invalid number"); return false; }
    if (input_[pos_] == '0') {
      ++pos_;
    } else if (input_[pos_] >= '1' && input_[pos_] <= '9') {
      while (pos_ < input_.size() && input_[pos_] >= '0' && input_[pos_] <= '9') ++pos_;
    } else {
      Fail("invalid number"); return false;
    }
    if (pos_ < input_.size() && input_[pos_] == '.') {
      ++pos_;
      const size_t digits = pos_;
      while (pos_ < input_.size() && input_[pos_] >= '0' && input_[pos_] <= '9') ++pos_;
      if (digits == pos_) { Fail("invalid fraction"); return false; }
    }
    if (pos_ < input_.size() && (input_[pos_] == 'e' || input_[pos_] == 'E')) {
      ++pos_;
      if (pos_ < input_.size() && (input_[pos_] == '+' || input_[pos_] == '-')) ++pos_;
      const size_t digits = pos_;
      while (pos_ < input_.size() && input_[pos_] >= '0' && input_[pos_] <= '9') ++pos_;
      if (digits == pos_) { Fail("invalid exponent"); return false; }
    }
    std::string text(input_.substr(start, pos_ - start));
    char* end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    if (!end || *end != '\0' || !std::isfinite(value)) {
      Fail("number is out of range"); return false;
    }
    *output = JsonValue(value);
    return true;
  }

  std::string_view input_;
  size_t pos_ = 0;
  std::string error_;
};

}  // namespace

JsonParseResult ParseJson(std::string_view input) { return Parser(input).Run(); }

std::string EscapeJsonString(std::string_view input) {
  std::string result;
  result.reserve(input.size() + 8);
  for (const unsigned char c : input) {
    switch (c) {
      case '"': result += "\\\""; break;
      case '\\': result += "\\\\"; break;
      case '\b': result += "\\b"; break;
      case '\f': result += "\\f"; break;
      case '\n': result += "\\n"; break;
      case '\r': result += "\\r"; break;
      case '\t': result += "\\t"; break;
      default:
        if (c < 0x20) {
          char buffer[7]{};
          sprintf_s(buffer, "\\u%04x", static_cast<unsigned int>(c));
          result += buffer;
        } else {
          result.push_back(static_cast<char>(c));
        }
    }
  }
  return result;
}

}  // namespace monitor

