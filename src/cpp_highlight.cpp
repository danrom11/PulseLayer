#include "cpp_highlight.hpp"

#include <cctype>
#include <string>
#include <unordered_set>

#include <pulseui/ui/theme.hpp>

namespace layer {

using namespace pulseui;

namespace {

struct SyntaxColors {
  Color keyword, type, string, number, comment, preproc, function;
};

// Picked from the current theme so both dark and light read well.
SyntaxColors colors() {
  if (th().window.luminance() < 0.5f)
    return {Color::hex(0xC792EA), Color::hex(0x4EC9B0), Color::hex(0xC3E88D), Color::hex(0xF78C6C),
            Color::hex(0x6A737D), Color::hex(0xFFCB6B), Color::hex(0x82AAFF)};
  return {Color::hex(0x8E24AA), Color::hex(0x00838F), Color::hex(0x2E7D32), Color::hex(0xD84315),
          Color::hex(0x8A939E), Color::hex(0xB26A00), Color::hex(0x1565C0)};
}

const std::unordered_set<std::string>& keywords() {
  static const std::unordered_set<std::string> k = {
      "alignas", "alignof", "and", "asm", "break", "case", "catch", "class", "co_await", "co_return", "co_yield",
      "concept", "const", "consteval", "constexpr", "constinit", "const_cast", "continue", "decltype", "default",
      "delete", "do", "dynamic_cast", "else", "enum", "explicit", "export", "extern", "false", "final", "for",
      "friend", "goto", "if", "inline", "mutable", "namespace", "new", "noexcept", "not", "nullptr", "operator",
      "or", "override", "private", "protected", "public", "register", "reinterpret_cast", "requires", "return",
      "sizeof", "static", "static_assert", "static_cast", "struct", "switch", "template", "this", "thread_local",
      "throw", "true", "try", "typedef", "typeid", "typename", "union", "using", "virtual", "volatile", "while"};
  return k;
}

const std::unordered_set<std::string>& types() {
  static const std::unordered_set<std::string> t = {
      "auto", "bool", "char", "char8_t", "char16_t", "char32_t", "double", "float", "int", "long", "short",
      "signed", "unsigned", "void", "wchar_t", "size_t", "std", "string", "vector", "pulse", "pulseui",
      "int8_t", "int16_t", "int32_t", "int64_t", "uint8_t", "uint16_t", "uint32_t", "uint64_t"};
  return t;
}

bool ident_start(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
bool ident_char(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

} // namespace

CodeEditor::Highlighter cpp_highlighter() {
  return [](std::string_view s, bool& in_comment) {
    const SyntaxColors c = colors();
    std::vector<TextSpan> out;
    std::size_t i = 0;
    const std::size_t n = s.size();
    if (in_comment) {
      const auto end = s.find("*/");
      const std::size_t stop = end == std::string_view::npos ? n : end + 2;
      out.push_back({0, stop, c.comment});
      if (end == std::string_view::npos) return out;
      in_comment = false;
      i = stop;
    }
    // preprocessor line
    std::size_t first = s.find_first_not_of(' ');
    if (first != std::string_view::npos && first >= i && s[first] == '#') {
      std::size_t j = first + 1;
      while (j < n && s[j] == ' ') ++j;
      while (j < n && ident_char(s[j])) ++j;
      out.push_back({first, j, c.preproc});
      i = j;
    }
    while (i < n) {
      const char ch = s[i];
      if (ch == '/' && i + 1 < n && s[i + 1] == '/') {
        out.push_back({i, n, c.comment});
        break;
      }
      if (ch == '/' && i + 1 < n && s[i + 1] == '*') {
        const auto end = s.find("*/", i + 2);
        const std::size_t stop = end == std::string_view::npos ? n : end + 2;
        out.push_back({i, stop, c.comment});
        if (end == std::string_view::npos) in_comment = true;
        i = stop;
        continue;
      }
      if (ch == '"' || ch == '\'' || (ch == '<' && !out.empty() && out.back().color == c.preproc && out.back().end <= i)) {
        const char close = ch == '<' ? '>' : ch;
        std::size_t j = i + 1;
        while (j < n && s[j] != close) j += (s[j] == '\\' && close != '>') ? 2 : 1;
        j = std::min(n, j + 1);
        out.push_back({i, j, c.string});
        i = j;
        continue;
      }
      if (std::isdigit(static_cast<unsigned char>(ch))) {
        std::size_t j = i;
        while (j < n && (std::isalnum(static_cast<unsigned char>(s[j])) || s[j] == '.' || s[j] == '\'')) ++j;
        out.push_back({i, j, c.number});
        i = j;
        continue;
      }
      if (ident_start(ch)) {
        std::size_t j = i;
        while (j < n && ident_char(s[j])) ++j;
        const std::string word(s.substr(i, j - i));
        std::size_t k = j;
        while (k < n && s[k] == ' ') ++k;
        if (keywords().count(word)) out.push_back({i, j, c.keyword});
        else if (types().count(word)) out.push_back({i, j, c.type});
        else if (k < n && s[k] == '(') out.push_back({i, j, c.function});
        else if (!word.empty() && std::isupper(static_cast<unsigned char>(word[0]))) out.push_back({i, j, c.type});
        i = j;
        continue;
      }
      ++i;
    }
    return out;
  };
}

} // namespace layer
