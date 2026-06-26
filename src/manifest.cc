#include "mapweave/manifest.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace mapweave {
namespace {

std::string trim(std::string text) {
  auto is_space = [](unsigned char ch) { return std::isspace(ch) != 0; };
  text.erase(text.begin(), std::find_if(text.begin(), text.end(),
                                        [&](char ch) { return !is_space(ch); }));
  text.erase(std::find_if(text.rbegin(), text.rend(),
                          [&](char ch) { return !is_space(ch); })
                 .base(),
             text.end());
  return text;
}

std::vector<std::string> splitWords(const std::string& line) {
  std::vector<std::string> words;
  std::string current;
  bool quoted = false;
  bool escaping = false;
  for (char ch : line) {
    if (escaping) {
      current.push_back(ch);
      escaping = false;
      continue;
    }
    if (ch == '\\') {
      escaping = true;
      continue;
    }
    if (ch == '"') {
      quoted = !quoted;
      continue;
    }
    if (std::isspace(static_cast<unsigned char>(ch)) && !quoted) {
      if (!current.empty()) {
        words.push_back(current);
        current.clear();
      }
      continue;
    }
    current.push_back(ch);
  }
  if (!current.empty()) {
    words.push_back(current);
  }
  return words;
}

Result<std::map<std::string, std::string>> attrsFromWords(
    const std::vector<std::string>& words, std::size_t start, std::size_t line_number) {
  std::map<std::string, std::string> attrs;
  for (std::size_t i = start; i < words.size(); ++i) {
    const std::size_t equals = words[i].find('=');
    if (equals == std::string::npos || equals == 0) {
      return Result<std::map<std::string, std::string>>::failure(
          makeError(ErrorCode::kInvalidSyntax, "attribute expects key=value", line_number));
    }
    attrs[words[i].substr(0, equals)] = words[i].substr(equals + 1);
  }
  return Result<std::map<std::string, std::string>>::success(std::move(attrs));
}

std::uint32_t parseU32Attr(const std::map<std::string, std::string>& attrs,
                           const std::string& key, std::uint32_t fallback) {
  auto found = attrs.find(key);
  if (found == attrs.end()) {
    return fallback;
  }
  try {
    return static_cast<std::uint32_t>(std::stoul(found->second, nullptr, 0));
  } catch (...) {
    return fallback;
  }
}

std::int32_t parseI32Attr(const std::map<std::string, std::string>& attrs,
                          const std::string& key, std::int32_t fallback) {
  auto found = attrs.find(key);
  if (found == attrs.end()) {
    return fallback;
  }
  try {
    return static_cast<std::int32_t>(std::stol(found->second, nullptr, 0));
  } catch (...) {
    return fallback;
  }
}

double parseDoubleAttr(const std::map<std::string, std::string>& attrs,
                       const std::string& key, double fallback) {
  auto found = attrs.find(key);
  if (found == attrs.end()) {
    return fallback;
  }
  try {
    return std::stod(found->second);
  } catch (...) {
    return fallback;
  }
}

}  // namespace

Result<Manifest> parseManifest(const std::string& text) {
  std::istringstream input(text);
  std::string line;
  std::size_t line_number = 0;
  Manifest manifest;
  bool saw_header = false;
  bool collecting_style = false;
  std::ostringstream style_text;

  while (std::getline(input, line)) {
    ++line_number;
    if (collecting_style) {
      if (trim(line) == "style-end") {
        collecting_style = false;
        continue;
      }
      style_text << line << '\n';
      continue;
    }

    line = trim(line);
    if (line.empty() || line[0] == '#') {
      continue;
    }
    if (!saw_header) {
      if (line != "MWM1") {
        return Result<Manifest>::failure(
            makeError(ErrorCode::kInvalidMagic, "manifest missing MWM1", line_number));
      }
      saw_header = true;
      continue;
    }

    if (line == "style-begin") {
      collecting_style = true;
      continue;
    }

    auto words = splitWords(line);
    if (words.empty()) {
      continue;
    }
    if (words[0] == "schema") {
      if (words.size() != 2) {
        return Result<Manifest>::failure(
            makeError(ErrorCode::kInvalidSyntax, "schema expects integer", line_number));
      }
      try {
        manifest.schema = static_cast<std::uint32_t>(std::stoul(words[1], nullptr, 0));
      } catch (...) {
        return Result<Manifest>::failure(
            makeError(ErrorCode::kInvalidSyntax, "invalid schema integer", line_number));
      }
    } else if (words[0] == "meta") {
      auto attrs = attrsFromWords(words, 1, line_number);
      if (!attrs) {
        return Result<Manifest>::failure(attrs.error());
      }
      for (const auto& item : attrs.value()) {
        manifest.metadata[item.first] = item.second;
      }
    } else if (words[0] == "source") {
      auto attrs = attrsFromWords(words, 1, line_number);
      if (!attrs) {
        return Result<Manifest>::failure(attrs.error());
      }
      Source source;
      source.name = attrs.value()["name"];
      source.codec = attrs.value()["codec"];
      source.path_hint = attrs.value()["path"];
      source.scale = parseDoubleAttr(attrs.value(), "scale", 1.0);
      source.attributes = attrs.takeValue();
      if (source.name.empty()) {
        return Result<Manifest>::failure(
            makeError(ErrorCode::kInvalidSyntax, "source missing name", line_number));
      }
      manifest.sources.push_back(std::move(source));
    } else if (words[0] == "layer") {
      auto attrs = attrsFromWords(words, 1, line_number);
      if (!attrs) {
        return Result<Manifest>::failure(attrs.error());
      }
      Layer layer;
      layer.id = attrs.value()["id"];
      layer.type = attrs.value()["type"];
      layer.source = attrs.value()["source"];
      layer.srid = parseU32Attr(attrs.value(), "srid", 3857);
      layer.priority = parseI32Attr(attrs.value(), "priority", 0);
      layer.attributes = attrs.takeValue();
      if (layer.id.empty()) {
        return Result<Manifest>::failure(
            makeError(ErrorCode::kInvalidSyntax, "layer missing id", line_number));
      }
      manifest.layers.push_back(std::move(layer));
    } else {
      return Result<Manifest>::failure(
          makeError(ErrorCode::kInvalidSyntax, "unknown manifest directive", line_number));
    }
  }

  if (!saw_header) {
    return Result<Manifest>::failure(
        makeError(ErrorCode::kInvalidMagic, "empty manifest", 0));
  }
  if (collecting_style) {
    return Result<Manifest>::failure(
        makeError(ErrorCode::kUnexpectedEof, "unterminated style block", line_number));
  }
  const std::string styles = style_text.str();
  if (!styles.empty()) {
    auto parsed_style = parseStyleSheet(styles);
    if (!parsed_style) {
      return Result<Manifest>::failure(parsed_style.error());
    }
    manifest.inline_style = parsed_style.takeValue();
  }
  return Result<Manifest>::success(std::move(manifest));
}

Result<void> validateManifest(const Manifest& manifest) {
  if (manifest.schema == 0 || manifest.schema > 3) {
    return Result<void>::failure(
        makeError(ErrorCode::kUnsupportedVersion, "unsupported manifest schema", 0));
  }
  if (manifest.layers.empty()) {
    return Result<void>::failure(
        makeError(ErrorCode::kInvalidState, "manifest has no layers", 0));
  }
  for (const auto& layer : manifest.layers) {
    if (layer.id.empty() || layer.type.empty()) {
      return Result<void>::failure(
          makeError(ErrorCode::kInvalidState, "layer missing id or type", 0));
    }
  }
  return Result<void>::success();
}

std::string formatManifestSummary(const Manifest& manifest) {
  std::ostringstream out;
  out << "schema=" << manifest.schema << '\n';
  out << "layers=" << manifest.layers.size() << '\n';
  out << "sources=" << manifest.sources.size() << '\n';
  out << "style_nodes=" << countStyleNodes(manifest.inline_style) << '\n';
  for (const auto& layer : manifest.layers) {
    out << "layer=" << layer.id << ":" << layer.type << ":" << layer.source << ":"
        << layer.srid << ":" << layer.priority << '\n';
  }
  return out.str();
}

}  // namespace mapweave
