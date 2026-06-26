#include "mapweave/style.h"

#include <cctype>
#include <cstdlib>
#include <sstream>

namespace mapweave {
namespace {

enum class TokenKind {
  kEnd,
  kIdentifier,
  kNumber,
  kString,
  kColor,
  kLBrace,
  kRBrace,
  kLParen,
  kRParen,
  kColon,
  kSemicolon,
  kComma,
  kDot,
  kPlus,
  kMinus,
  kStar,
  kSlash,
};

struct Token {
  TokenKind kind = TokenKind::kEnd;
  std::string text;
  std::size_t offset = 0;
};

class Lexer {
 public:
  explicit Lexer(std::string_view source) : source_(source) {}

  Result<Token> next() {
    skipTrivia();
    if (offset_ >= source_.size()) {
      return Result<Token>::success(Token{TokenKind::kEnd, "", offset_});
    }
    const std::size_t start = offset_;
    const char ch = source_[offset_++];
    switch (ch) {
      case '{':
        return Result<Token>::success(Token{TokenKind::kLBrace, "{", start});
      case '}':
        return Result<Token>::success(Token{TokenKind::kRBrace, "}", start});
      case '(':
        return Result<Token>::success(Token{TokenKind::kLParen, "(", start});
      case ')':
        return Result<Token>::success(Token{TokenKind::kRParen, ")", start});
      case ':':
        return Result<Token>::success(Token{TokenKind::kColon, ":", start});
      case ';':
        return Result<Token>::success(Token{TokenKind::kSemicolon, ";", start});
      case ',':
        return Result<Token>::success(Token{TokenKind::kComma, ",", start});
      case '.':
        return Result<Token>::success(Token{TokenKind::kDot, ".", start});
      case '+':
        return Result<Token>::success(Token{TokenKind::kPlus, "+", start});
      case '*':
        return Result<Token>::success(Token{TokenKind::kStar, "*", start});
      case '/':
        return Result<Token>::success(Token{TokenKind::kSlash, "/", start});
      case '-':
        if (offset_ < source_.size() &&
            std::isdigit(static_cast<unsigned char>(source_[offset_]))) {
          --offset_;
          return readNumber();
        }
        return Result<Token>::success(Token{TokenKind::kMinus, "-", start});
      case '"':
        return readString(start);
      case '#':
        return readColor(start);
      default:
        if (std::isalpha(static_cast<unsigned char>(ch)) || ch == '_' || ch == '$') {
          --offset_;
          return readIdentifier();
        }
        if (std::isdigit(static_cast<unsigned char>(ch))) {
          --offset_;
          return readNumber();
        }
        return Result<Token>::failure(
            makeError(ErrorCode::kInvalidSyntax, "unexpected style character", start));
    }
  }

 private:
  void skipTrivia() {
    while (offset_ < source_.size()) {
      const char ch = source_[offset_];
      if (std::isspace(static_cast<unsigned char>(ch))) {
        ++offset_;
        continue;
      }
      if (ch == '/' && offset_ + 1 < source_.size() && source_[offset_ + 1] == '/') {
        offset_ += 2;
        while (offset_ < source_.size() && source_[offset_] != '\n') {
          ++offset_;
        }
        continue;
      }
      break;
    }
  }

  Result<Token> readIdentifier() {
    const std::size_t start = offset_;
    while (offset_ < source_.size()) {
      const char ch = source_[offset_];
      if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '_' && ch != '-' &&
          ch != '$') {
        break;
      }
      ++offset_;
    }
    return Result<Token>::success(
        Token{TokenKind::kIdentifier, std::string(source_.substr(start, offset_ - start)), start});
  }

  Result<Token> readNumber() {
    const std::size_t start = offset_;
    if (source_[offset_] == '-') {
      ++offset_;
    }
    while (offset_ < source_.size() && std::isdigit(static_cast<unsigned char>(source_[offset_]))) {
      ++offset_;
    }
    if (offset_ < source_.size() && source_[offset_] == '.') {
      ++offset_;
      while (offset_ < source_.size() &&
             std::isdigit(static_cast<unsigned char>(source_[offset_]))) {
        ++offset_;
      }
    }
    return Result<Token>::success(
        Token{TokenKind::kNumber, std::string(source_.substr(start, offset_ - start)), start});
  }

  Result<Token> readColor(std::size_t start) {
    while (offset_ < source_.size() &&
           std::isxdigit(static_cast<unsigned char>(source_[offset_]))) {
      ++offset_;
    }
    const std::string value(source_.substr(start, offset_ - start));
    if (value.size() != 4 && value.size() != 7 && value.size() != 9) {
      return Result<Token>::failure(
          makeError(ErrorCode::kInvalidSyntax, "invalid color literal", start));
    }
    return Result<Token>::success(Token{TokenKind::kColor, value, start});
  }

  Result<Token> readString(std::size_t start) {
    std::string out;
    while (offset_ < source_.size()) {
      const char ch = source_[offset_++];
      if (ch == '"') {
        return Result<Token>::success(Token{TokenKind::kString, out, start});
      }
      if (ch == '\\') {
        if (offset_ >= source_.size()) {
          return Result<Token>::failure(
              makeError(ErrorCode::kUnexpectedEof, "unterminated string escape", start));
        }
        const char escaped = source_[offset_++];
        switch (escaped) {
          case 'n':
            out.push_back('\n');
            break;
          case 't':
            out.push_back('\t');
            break;
          case 'r':
            out.push_back('\r');
            break;
          default:
            out.push_back(escaped);
            break;
        }
      } else {
        out.push_back(ch);
      }
    }
    return Result<Token>::failure(
        makeError(ErrorCode::kUnexpectedEof, "unterminated string", start));
  }

  std::string_view source_;
  std::size_t offset_ = 0;
};

class Parser {
 public:
  explicit Parser(std::string_view source) : lexer_(source) {}

  Result<StyleSheet> parse() {
    auto first = lexer_.next();
    if (!first) {
      return Result<StyleSheet>::failure(first.error());
    }
    current_ = first.value();
    StyleSheet sheet;
    while (current_.kind != TokenKind::kEnd) {
      auto rule = parseRule();
      if (!rule) {
        return Result<StyleSheet>::failure(rule.error());
      }
      sheet.rules.push_back(rule.takeValue());
    }
    return Result<StyleSheet>::success(std::move(sheet));
  }

 private:
  Result<StyleRule> parseRule() {
    if (current_.kind != TokenKind::kIdentifier && current_.kind != TokenKind::kDot) {
      return Result<StyleRule>::failure(
          makeError(ErrorCode::kInvalidSyntax, "expected style selector", current_.offset));
    }
    std::string selector;
    while (current_.kind == TokenKind::kIdentifier || current_.kind == TokenKind::kDot) {
      selector += current_.text;
      auto next = advance();
      if (!next) {
        return Result<StyleRule>::failure(next.error());
      }
    }
    auto open = expect(TokenKind::kLBrace, "style rule expects '{'");
    if (!open) {
      return Result<StyleRule>::failure(open.error());
    }

    StyleRule rule;
    rule.selector = std::move(selector);
    while (current_.kind != TokenKind::kRBrace) {
      if (current_.kind == TokenKind::kEnd) {
        return Result<StyleRule>::failure(
            makeError(ErrorCode::kUnexpectedEof, "unterminated style rule", 0));
      }
      if (current_.kind == TokenKind::kIdentifier) {
        const std::string key = current_.text;
        auto next = advance();
        if (!next) {
          return Result<StyleRule>::failure(next.error());
        }
        if (current_.kind == TokenKind::kLBrace) {
          StyleRule child;
          child.selector = key;
          auto child_rule = parseRuleBody(std::move(child));
          if (!child_rule) {
            return Result<StyleRule>::failure(child_rule.error());
          }
          rule.children.push_back(child_rule.takeValue());
        } else {
          auto colon = expect(TokenKind::kColon, "style property expects ':'");
          if (!colon) {
            return Result<StyleRule>::failure(colon.error());
          }
          auto value = parseExpression();
          if (!value) {
            return Result<StyleRule>::failure(value.error());
          }
          auto semi = expect(TokenKind::kSemicolon, "style property expects ';'");
          if (!semi) {
            return Result<StyleRule>::failure(semi.error());
          }
          StyleProperty property;
          property.key = key;
          property.value = value.takeValue();
          rule.properties.push_back(std::move(property));
        }
      } else {
        return Result<StyleRule>::failure(
            makeError(ErrorCode::kInvalidSyntax, "unexpected token in style rule",
                      current_.offset));
      }
    }
    auto close = advance();
    if (!close) {
      return Result<StyleRule>::failure(close.error());
    }
    return Result<StyleRule>::success(std::move(rule));
  }

  Result<StyleRule> parseRuleBody(StyleRule rule) {
    auto open = expect(TokenKind::kLBrace, "nested style rule expects '{'");
    if (!open) {
      return Result<StyleRule>::failure(open.error());
    }
    while (current_.kind != TokenKind::kRBrace) {
      if (current_.kind == TokenKind::kEnd) {
        return Result<StyleRule>::failure(
            makeError(ErrorCode::kUnexpectedEof, "unterminated nested style rule", 0));
      }
      if (current_.kind != TokenKind::kIdentifier) {
        return Result<StyleRule>::failure(
            makeError(ErrorCode::kInvalidSyntax, "expected nested style property",
                      current_.offset));
      }
      const std::string key = current_.text;
      auto next = advance();
      if (!next) {
        return Result<StyleRule>::failure(next.error());
      }
      auto colon = expect(TokenKind::kColon, "nested property expects ':'");
      if (!colon) {
        return Result<StyleRule>::failure(colon.error());
      }
      auto value = parseExpression();
      if (!value) {
        return Result<StyleRule>::failure(value.error());
      }
      auto semi = expect(TokenKind::kSemicolon, "nested property expects ';'");
      if (!semi) {
        return Result<StyleRule>::failure(semi.error());
      }
      StyleProperty property;
      property.key = key;
      property.value = value.takeValue();
      rule.properties.push_back(std::move(property));
    }
    auto close = advance();
    if (!close) {
      return Result<StyleRule>::failure(close.error());
    }
    return Result<StyleRule>::success(std::move(rule));
  }

  Result<StyleValue> parseExpression() { return parseAdditive(); }

  Result<StyleValue> parseAdditive() {
    auto left = parseMultiplicative();
    if (!left) {
      return left;
    }
    while (current_.kind == TokenKind::kPlus || current_.kind == TokenKind::kMinus) {
      const char op = current_.kind == TokenKind::kPlus ? '+' : '-';
      auto next = advance();
      if (!next) {
        return Result<StyleValue>::failure(next.error());
      }
      auto right = parseMultiplicative();
      if (!right) {
        return right;
      }
      StyleValue value;
      value.kind = StyleValueKind::kBinary;
      value.op = op;
      value.left.reset(new StyleValue(left.takeValue()));
      value.right.reset(new StyleValue(right.takeValue()));
      value.text = styleValueToString(*value.left) + op + styleValueToString(*value.right);
      left = Result<StyleValue>::success(std::move(value));
    }
    return left;
  }

  Result<StyleValue> parseMultiplicative() {
    auto left = parsePrimary();
    if (!left) {
      return left;
    }
    while (current_.kind == TokenKind::kStar || current_.kind == TokenKind::kSlash) {
      const char op = current_.kind == TokenKind::kStar ? '*' : '/';
      auto next = advance();
      if (!next) {
        return Result<StyleValue>::failure(next.error());
      }
      auto right = parsePrimary();
      if (!right) {
        return right;
      }
      StyleValue value;
      value.kind = StyleValueKind::kBinary;
      value.op = op;
      value.left.reset(new StyleValue(left.takeValue()));
      value.right.reset(new StyleValue(right.takeValue()));
      value.text = styleValueToString(*value.left) + op + styleValueToString(*value.right);
      left = Result<StyleValue>::success(std::move(value));
    }
    return left;
  }

  Result<StyleValue> parsePrimary() {
    StyleValue value;
    if (current_.kind == TokenKind::kNumber) {
      value.kind = StyleValueKind::kNumber;
      value.text = current_.text;
      value.number = std::strtod(current_.text.c_str(), nullptr);
    } else if (current_.kind == TokenKind::kString) {
      value.kind = StyleValueKind::kString;
      value.text = current_.text;
    } else if (current_.kind == TokenKind::kColor) {
      value.kind = StyleValueKind::kColor;
      value.text = current_.text;
    } else if (current_.kind == TokenKind::kIdentifier) {
      value.kind = StyleValueKind::kIdentifier;
      value.text = current_.text;
    } else if (current_.kind == TokenKind::kLParen) {
      auto open = advance();
      if (!open) {
        return Result<StyleValue>::failure(open.error());
      }
      auto nested = parseExpression();
      if (!nested) {
        return nested;
      }
      auto close = expect(TokenKind::kRParen, "expression expects ')'");
      if (!close) {
        return Result<StyleValue>::failure(close.error());
      }
      return nested;
    } else {
      return Result<StyleValue>::failure(
          makeError(ErrorCode::kInvalidSyntax, "expected style value", current_.offset));
    }
    auto next = advance();
    if (!next) {
      return Result<StyleValue>::failure(next.error());
    }
    return Result<StyleValue>::success(std::move(value));
  }

  Result<void> expect(TokenKind kind, const char* message) {
    if (current_.kind != kind) {
      return Result<void>::failure(makeError(ErrorCode::kInvalidSyntax, message, current_.offset));
    }
    return advance();
  }

  Result<void> advance() {
    auto next = lexer_.next();
    if (!next) {
      return Result<void>::failure(next.error());
    }
    current_ = next.value();
    return Result<void>::success();
  }

  Lexer lexer_;
  Token current_;
};

std::size_t countRuleNodes(const StyleRule& rule) {
  std::size_t total = 1 + rule.properties.size();
  for (const auto& property : rule.properties) {
    total += countStyleValueNodes(property.value);
  }
  for (const auto& child : rule.children) {
    total += countRuleNodes(child);
  }
  return total;
}

}  // namespace

StyleValue::StyleValue(const StyleValue& other)
    : kind(other.kind), text(other.text), number(other.number), op(other.op) {
  if (other.left) {
    left.reset(new StyleValue(*other.left));
  }
  if (other.right) {
    right.reset(new StyleValue(*other.right));
  }
}

StyleValue& StyleValue::operator=(const StyleValue& other) {
  if (this == &other) {
    return *this;
  }
  kind = other.kind;
  text = other.text;
  number = other.number;
  op = other.op;
  left.reset(other.left ? new StyleValue(*other.left) : nullptr);
  right.reset(other.right ? new StyleValue(*other.right) : nullptr);
  return *this;
}

Result<StyleSheet> parseStyleSheet(std::string_view source) {
  Parser parser(source);
  return parser.parse();
}

std::string styleValueToString(const StyleValue& value) {
  switch (value.kind) {
    case StyleValueKind::kNumber:
    case StyleValueKind::kColor:
    case StyleValueKind::kIdentifier:
      return value.text;
    case StyleValueKind::kString:
      return "\"" + value.text + "\"";
    case StyleValueKind::kBinary:
      if (value.left && value.right) {
        return "(" + styleValueToString(*value.left) + value.op +
               styleValueToString(*value.right) + ")";
      }
      return value.text;
  }
  return value.text;
}

std::size_t countStyleValueNodes(const StyleValue& value) {
  std::size_t total = 1;
  if (value.left) {
    total += countStyleValueNodes(*value.left);
  }
  if (value.right) {
    total += countStyleValueNodes(*value.right);
  }
  return total;
}

std::size_t countStyleNodes(const StyleSheet& sheet) {
  std::size_t total = 0;
  for (const auto& rule : sheet.rules) {
    total += countRuleNodes(rule);
  }
  return total;
}

}  // namespace mapweave
