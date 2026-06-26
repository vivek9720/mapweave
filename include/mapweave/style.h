#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "mapweave/result.h"

namespace mapweave {

enum class StyleValueKind {
  kNumber,
  kString,
  kColor,
  kIdentifier,
  kBinary,
};

struct StyleValue {
  StyleValueKind kind = StyleValueKind::kIdentifier;
  std::string text;
  double number = 0.0;
  char op = 0;
  std::unique_ptr<StyleValue> left;
  std::unique_ptr<StyleValue> right;

  StyleValue() = default;
  StyleValue(const StyleValue& other);
  StyleValue& operator=(const StyleValue& other);
  StyleValue(StyleValue&&) noexcept = default;
  StyleValue& operator=(StyleValue&&) noexcept = default;
};

struct StyleProperty {
  std::string key;
  StyleValue value;
};

struct StyleRule {
  std::string selector;
  std::vector<StyleProperty> properties;
  std::vector<StyleRule> children;
};

struct StyleSheet {
  std::vector<StyleRule> rules;
};

Result<StyleSheet> parseStyleSheet(std::string_view source);
std::string styleValueToString(const StyleValue& value);
std::size_t countStyleNodes(const StyleSheet& sheet);
std::size_t countStyleValueNodes(const StyleValue& value);

}  // namespace mapweave
