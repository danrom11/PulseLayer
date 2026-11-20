#include "cpp_complete.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <regex>
#include <set>
#include <unordered_map>
#include <unordered_set>

#include <pulseui/core/json.hpp>

namespace layer {

using pulseui::CompletionItem;
using pulseui::CompletionResult;
using K = CompletionItem::Kind;

namespace {

// ── API knowledge (PulseUI / Pulse / std) ────────────────────────────────────

struct Api {
  std::map<std::string, std::vector<CompletionItem>> members; // type -> members
  std::map<std::string, std::vector<CompletionItem>> scopes;  // "Alignment" -> enumerators, "pulse" -> names
  std::vector<CompletionItem> globals;                        // names usable without qualification
  std::set<std::string> views;                                // types that have the common view modifiers
};

std::vector<CompletionItem> items(K kind, std::initializer_list<std::pair<const char*, const char*>> list) {
  std::vector<CompletionItem> out;
  for (auto& [n, d] : list) out.push_back({n, d, kind, {}});
  return out;
}

const Api& api() {
  static const Api a = [] {
    Api a;
    const auto view_common = items(K::Function, {
        {"id", "(string) → id for find<T>()"}, {"padding", "(Insets)"}, {"width", "(float)"}, {"height", "(float)"},
        {"size", "(w, h)"}, {"min_width", "(float)"}, {"min_height", "(float)"}, {"max_width", "(float)"},
        {"max_height", "(float)"}, {"flex", "(grow = 1)"}, {"align", "(Alignment)"}, {"position", "(x, y)"},
        {"radius", "(float)"}, {"clip", "(bool)"}, {"opacity", "(float)"}, {"tooltip", "(Bind<string>)"},
        {"cursor", "(Cursor)"}, {"focusable", "(bool)"}, {"background", "(Bind<Color>)"}, {"border", "(Bind<Color>, w)"},
        {"visible", "(Bind<bool>)"}, {"enabled", "(Bind<bool>)"}, {"on_click", "(fn)"}, {"on_hover", "(fn(bool))"},
        {"on_event_fn", "(fn(InputEvent))"}, {"ref", "(T*&)"}, {"on_mount_fn", "(fn(T&))"}, {"find", "<T>(id) → T*"},
        {"add", "(Node) → View&"}, {"children", "() → views"}, {"parent", "() → View*"}, {"window", "() → Window*"},
        {"frame", "() → Rect"}, {"bounds", "() → Rect"}, {"set_visible", "(bool)"}, {"set_enabled", "(bool)"},
        {"is_visible", "() → bool"}, {"is_enabled", "() → bool"}, {"focus", "()"}, {"invalidate", "()"},
        {"set_tooltip", "(string)"}, {"style", "() → Style&"}, {"events", "() → signal<InputEvent>&"}});
    auto add_view = [&](const std::string& t, std::vector<CompletionItem> own) {
      a.views.insert(t);
      a.members[t] = std::move(own);
    };
    add_view("View", {});
    add_view("Button", items(K::Member, {{"clicked", "signal<>"}, {"clicks", "() → observable<unit>"}, {"label", "() → string"},
                                        {"set_label", "(string)"}, {"bind_label", "(Bind<string>)"}, {"primary", "()"},
                                        {"flat", "()"}, {"danger", "()"}, {"kind", "(ButtonKind)"}, {"icon", "(string)"}}));
    add_view("Text", items(K::Member, {{"text", "() → string"}, {"set_text", "(string)"}, {"bind_text", "(Bind<string>)"},
                                      {"font_size", "(float)"}, {"weight", "(FontWeight)"}, {"bold", "()"}, {"title", "()"},
                                      {"dim", "()"}, {"wrap", "(bool)"}, {"text_align", "(Align)"}, {"color", "(Bind<Color>)"},
                                      {"mono", "()"}}));
    add_view("TextField", items(K::Member, {{"text", "() → string"}, {"set_text", "(string)"}, {"model", "(property<string>&)"},
                                           {"placeholder", "(Bind<string>)"}, {"password", "(bool)"}, {"on_change", "(fn(string))"},
                                           {"on_submit", "(fn(string))"}, {"on_commit", "(fn(string))"}, {"changed", "signal<string>"},
                                           {"submitted", "signal<string>"}, {"committed", "signal<string>"},
                                           {"text_changes", "() → observable<string>"}}));
    const auto boolean = items(K::Member, {{"checked", "() → bool"}, {"set_checked", "(bool)"}, {"model", "(property<bool>&)"},
                                           {"on_change", "(fn(bool))"}, {"changed", "signal<bool>"}, {"label", "() → string"},
                                           {"set_label", "(string)"}});
    add_view("Checkbox", boolean);
    add_view("Toggle", boolean);
    add_view("Slider", items(K::Member, {{"value", "() → double"}, {"set_value", "(double)"}, {"model", "(property<double>&)"},
                                        {"on_change", "(fn(double))"}, {"changed", "signal<double>"}, {"step", "(double)"},
                                        {"min", "() → double"}, {"max", "() → double"}, {"set_range", "(lo, hi)"}}));
    add_view("ProgressBar", items(K::Member, {{"value", "() → double"}, {"set_value", "(double)"}, {"bind_value", "(Bind<double>)"}}));
    add_view("Select", items(K::Member, {{"index", "() → int"}, {"set_index", "(int)"}, {"options", "() → vector<string>"},
                                        {"set_options", "(vector<string>)"}, {"model", "(property<int>&)"},
                                        {"bind_options", "(Bind<vector<string>>)"}, {"on_change", "(fn(int))"}, {"changed", "signal<int>"}}));
    add_view("Segmented", items(K::Member, {{"index", "() → int"}, {"set_index", "(int)"}, {"on_change", "(fn(int))"}, {"changed", "signal<int>"}}));
    add_view("Tabs", items(K::Member, {{"current", "() → int"}, {"set_current", "(int)"}, {"selected", "(property<int>&)"},
                                      {"add_tab", "(Tab)"}, {"changed", "signal<int>"}, {"titles", "() → vector<string>"}}));
    add_view("CodeEditor", items(K::Member, {{"text", "() → string"}, {"set_text", "(string)"}, {"append", "(string)"},
                                            {"clear", "()"}, {"model", "(property<string>&)"}, {"read_only", "(bool)"},
                                            {"diagnostics", "(Bind<vector<Diagnostic>>)"}, {"go_to", "(line, col)"}}));
    const auto stack = items(K::Member, {{"spacing", "(float)"}, {"align_items", "(Alignment)"}, {"justify", "(Justify)"}});
    for (auto t : {"VStack", "HStack", "Panel"}) add_view(t, stack);
    for (auto t : {"Layer", "Scroll", "Spacer", "Divider", "Split", "Show", "ForEach", "Canvas", "ImageView"}) add_view(t, {});
    for (auto& [t, own] : a.members)
      if (a.views.count(t)) own.insert(own.end(), view_common.begin(), view_common.end());

    a.members["Window"] = items(K::Function, {
        {"content", "(Node) → Window&"}, {"show", "()"}, {"hide", "()"}, {"close", "()"}, {"set_title", "(string)"},
        {"resize", "(w, h)"}, {"size", "() → Size"}, {"set_min_size", "(w, h)"}, {"find", "<T>(id) → T*"},
        {"shortcut", "(keys, fn) → Window&"}, {"input", "() → observable<InputEvent>"}, {"closed", "signal<>"},
        {"resized", "signal<Size>"}, {"on_close", "function<bool()>"}, {"open_popup", "(Node, Point, fn)"},
        {"close_popup", "(View*)"}, {"focus_view", "() → View*"}, {"set_focus", "(View*)"}, {"screenshot", "(path)"},
        {"grab", "() → Image"}, {"root", "() → View*"}, {"scale", "() → float"}, {"is_open", "() → bool"}});
    a.members["App"] = items(K::Function, {{"run", "() → int"}, {"quit", "(code)"}, {"post", "(fn)"}, {"ui", "() → executor&"},
                                           {"after", "(ms, fn) → subscription"}, {"every", "(ms, fn) → subscription"},
                                           {"set_clipboard", "(string)"}, {"clipboard", "() → string"}, {"args", "vector<string>"},
                                           {"quit_on_last_window_closed", "bool"}});
    a.members["property"] = items(K::Function, {
        {"get", "() → T"}, {"set", "(T) → bool"}, {"changed", "signal<T>"}, {"on_change", "(fn) → connection"},
        {"map", "(fn) → observable<U>"}, {"bind", "(source)"}, {"unbind", "()"}, {"update", "(fn(T&))"},
        {"as_observable", "() → observable<T>"}, {"bind_bidirectional", "(property&) → subscription"}, {"is_bound", "() → bool"}});
    a.members["computed"] = items(K::Function, {{"get", "() → T"}, {"changed", "signal<T>"}, {"map", "(fn) → observable<U>"},
                                                {"as_observable", "() → observable<T>"}, {"recompute", "()"}});
    a.members["signal"] = items(K::Function, {{"connect", "(fn) → connection"}, {"emit", "(args...)"},
                                              {"as_observable", "() → observable"}, {"disconnect_all", "()"},
                                              {"set_blocked", "(bool)"}, {"forward_to", "(signal&)"}, {"slot_count", "() → size_t"}});
    a.members["observable"] = items(K::Function, {{"subscribe", "(on_next, on_error, on_done) → subscription"}, {"valid", "() → bool"}});
    a.members["subject"] = items(K::Function, {{"on_next", "(T)"}, {"on_error", "(exception_ptr)"}, {"on_completed", "()"},
                                               {"as_observable", "() → observable<T>"}, {"subscribe", "(fn) → subscription"}});
    a.members["behavior_subject"] = a.members["subject"];
    a.members["behavior_subject"].push_back({"value", "() → T", K::Function, {}});
    a.members["subscription"] = items(K::Function, {{"reset", "()"}, {"release", "()"}, {"active", "() → bool"}, {"unsubscribe", "()"}});
    a.members["connection"] = items(K::Function, {{"disconnect", "()"}, {"connected", "() → bool"}, {"scoped", "() → subscription"}});
    a.members["string"] = items(K::Function, {{"size", "() → size_t"}, {"empty", "() → bool"}, {"c_str", "() → const char*"},
                                              {"substr", "(pos, n) → string"}, {"find", "(s) → size_t"}, {"append", "(s)"},
                                              {"push_back", "(c)"}, {"clear", "()"}, {"length", "() → size_t"},
                                              {"begin", "()"}, {"end", "()"}, {"back", "()"}, {"front", "()"}, {"erase", "(pos, n)"},
                                              {"insert", "(pos, s)"}, {"replace", "(pos, n, s)"}, {"starts_with", "(s) → bool"}});
    a.members["vector"] = items(K::Function, {{"push_back", "(T)"}, {"emplace_back", "(args...)"}, {"size", "() → size_t"},
                                              {"empty", "() → bool"}, {"clear", "()"}, {"begin", "()"}, {"end", "()"},
                                              {"front", "() → T&"}, {"back", "() → T&"}, {"erase", "(it)"}, {"insert", "(it, v)"},
                                              {"resize", "(n)"}, {"reserve", "(n)"}, {"at", "(i) → T&"}, {"data", "() → T*"}, {"pop_back", "()"}});
    a.members["Color"] = items(K::Function, {{"with_alpha", "(a) → Color"}, {"lighter", "(k) → Color"}, {"darker", "(k) → Color"},
                                             {"to_hex", "() → string"}, {"r", "float"}, {"g", "float"}, {"b", "float"}, {"a", "float"}});
    a.members["InputEvent"] = items(K::Member, {{"type", "Type"}, {"pos", "Point"}, {"window_pos", "Point"}, {"button", "MouseButton"},
                                                {"clicks", "int"}, {"key", "Key"}, {"mods", "unsigned"}, {"text", "string"},
                                                {"wheel_dy", "float"}, {"is_mouse", "() → bool"}, {"primary", "() → bool"}});

    a.scopes["Alignment"] = items(K::Value, {{"Auto", ""}, {"Start", ""}, {"Center", ""}, {"End", ""}, {"Stretch", ""}});
    a.scopes["Justify"] = items(K::Value, {{"Start", ""}, {"Center", ""}, {"End", ""}, {"SpaceBetween", ""}});
    a.scopes["Orientation"] = items(K::Value, {{"Horizontal", ""}, {"Vertical", ""}});
    a.scopes["ButtonKind"] = items(K::Value, {{"Normal", ""}, {"Primary", ""}, {"Flat", ""}, {"Danger", ""}});
    a.scopes["FontWeight"] = items(K::Value, {{"Light", ""}, {"Normal", ""}, {"Medium", ""}, {"SemiBold", ""}, {"Bold", ""}});
    a.scopes["Cursor"] = items(K::Value, {{"Arrow", ""}, {"IBeam", ""}, {"Hand", ""}, {"Crosshair", ""}, {"SizeWE", ""}, {"SizeNS", ""}, {"SizeAll", ""}});
    a.scopes["Align"] = items(K::Value, {{"Left", ""}, {"HCenter", ""}, {"Right", ""}, {"Top", ""}, {"VCenter", ""}, {"Bottom", ""},
                                         {"Center", ""}, {"LeftCenter", ""}, {"RightCenter", ""}});
    a.scopes["Mod"] = items(K::Value, {{"Shift", ""}, {"Ctrl", ""}, {"Alt", ""}, {"Super", ""}, {"Primary", "Ctrl / Cmd"}});
    a.scopes["Theme"] = items(K::Function, {{"dark", "() → Theme"}, {"light", "() → Theme"}});
    a.scopes["Color"] = items(K::Function, {{"hex", "(0xRRGGBB) → Color"}, {"rgb8", "(r, g, b) → Color"}, {"parse", "(\"#RRGGBB\")"},
                                            {"white", "()"}, {"black", "()"}, {"transparent", "()"}});
    a.scopes["App"] = items(K::Function, {{"post", "(fn)"}, {"ui", "() → executor&"}, {"after", "(ms, fn)"}, {"every", "(ms, fn)"},
                                          {"quit", "(code)"}, {"instance", "() → App*"}, {"set_clipboard", "(string)"}, {"clipboard", "() → string"}});
    {
      std::vector<CompletionItem> keys;
      for (const char* k : {"Enter", "Escape", "Tab", "Space", "Backspace", "Delete", "Left", "Right", "Up", "Down", "Home", "End",
                            "PageUp", "PageDown", "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12"})
        keys.push_back({k, "", K::Value, {}});
      for (char c = 'A'; c <= 'Z'; ++c) keys.push_back({std::string(1, c), "", K::Value, {}});
      a.scopes["Key"] = keys;
    }
    a.scopes["pulse"] = items(K::Type, {
        {"property", "<T> observable value"}, {"computed", "<T> derived value"}, {"signal", "<Args...> event"},
        {"subject", "<T>"}, {"behavior_subject", "<T>"}, {"observable", "<T>"}, {"subscription", ""},
        {"composite_subscription", ""}, {"connection", ""}, {"scoped_connection", ""}, {"topic", "<T>"},
        {"thread_pool", ""}, {"strand", ""}, {"unit", ""}});
    for (auto& op : {"map", "filter", "tap", "scan", "debounce", "throttle", "throttle_latest", "delay", "timeout",
                     "switch_map", "flat_map", "concat_map", "merge", "combine_latest", "with_latest_from", "zip",
                     "distinct_until_changed", "take", "skip", "take_until", "first", "last", "retry", "catch_error",
                     "share", "observe_on", "subscribe_on", "start_with", "buffer", "window", "timer", "interval",
                     "just", "from", "range", "empty", "never", "defer"})
      a.scopes["pulse"].push_back({op, "operator", K::Function, {}});
    a.scopes["std"] = items(K::Type, {{"string", ""}, {"vector", "<T>"}, {"map", "<K, V>"}, {"unordered_map", "<K, V>"},
                                      {"optional", "<T>"}, {"function", "<R(Args...)>"}, {"unique_ptr", "<T>"}, {"shared_ptr", "<T>"},
                                      {"pair", "<A, B>"}, {"array", "<T, N>"}, {"string_view", ""}});
    for (auto& f : {"to_string", "move", "make_unique", "make_shared", "min", "max", "clamp", "sort", "find", "find_if",
                    "erase_if", "stoi", "stod", "abs", "swap", "cout", "endl"})
      a.scopes["std"].push_back({f, "", K::Function, {}});

    a.globals = items(K::Type, {
        {"App", "event loop"}, {"Window", "top-level window"}, {"VStack", "{...} vertical stack"}, {"HStack", "{...} horizontal stack"},
        {"Panel", "{...} card"}, {"Layer", "{...} overlay"}, {"Scroll", "(Node)"}, {"Spacer", "()"}, {"Divider", "()"},
        {"Text", "(Bind<string>)"}, {"Button", "(label, fn)"}, {"TextField", "(property<string>&)"}, {"Checkbox", "(label, property<bool>&)"},
        {"Toggle", "(label, property<bool>&)"}, {"Slider", "(property<double>&, min, max)"}, {"ProgressBar", "(Bind<double>)"},
        {"Select", "({options}, property<int>&)"}, {"Segmented", "({options}, property<int>&)"}, {"Tabs", "{Tab(...)}"},
        {"Tab", "(title, Node)"}, {"Split", "(a, b, orientation)"}, {"Show", "(cond, then, else)"}, {"ForEach", "(list, fn)"},
        {"Canvas", "(fn(Painter&, Rect))"}, {"ImageView", "(path)"}, {"CodeEditor", "(property<string>&)"}, {"Node", "any view"},
        {"View", "base view"}, {"Bind", "<T>"}, {"Model", "<T>"}, {"Color", ""}, {"Insets", "(all | h, v | l, t, r, b)"},
        {"Point", ""}, {"Size", ""}, {"Rect", ""}, {"Font", ""}, {"Theme", ""}, {"Alignment", "enum"}, {"Justify", "enum"},
        {"Orientation", "enum"}, {"ButtonKind", "enum"}, {"FontWeight", "enum"}, {"Key", "enum"}, {"Mod", "modifiers"},
        {"InputEvent", ""}, {"Image", ""}, {"Ui", "designed forms"}, {"pulse", "namespace"}, {"pulseui", "namespace"}, {"std", "namespace"}});
    for (auto& f : std::vector<std::pair<const char*, const char*>>{{"theme", "() → property<Theme>&"}, {"th", "() → const Theme&"},
                                                                    {"run", "(title, w, h, Node) → int"}, {"load_form", "(path) → Form"}})
      a.globals.push_back({f.first, f.second, K::Function, {}});
    a.scopes["pulseui"] = a.globals;
    return a;
  }();
  return a;
}

const std::unordered_set<std::string>& keywords() {
  static const std::unordered_set<std::string> k = {
      "alignas", "alignof", "auto", "bool", "break", "case", "catch", "char", "class", "const", "constexpr", "const_cast",
      "continue", "decltype", "default", "delete", "do", "double", "dynamic_cast", "else", "enum", "explicit", "extern",
      "false", "float", "for", "friend", "if", "inline", "int", "long", "mutable", "namespace", "new", "noexcept",
      "nullptr", "operator", "override", "private", "protected", "public", "return", "short", "signed", "sizeof",
      "static", "static_assert", "static_cast", "struct", "switch", "template", "this", "throw", "true", "try",
      "typedef", "typename", "union", "unsigned", "using", "virtual", "void", "volatile", "while", "final", "co_await"};
  return k;
}

// words that may precede a declared name as (part of) its type
bool type_word(const std::string& w) {
  static const std::unordered_set<std::string> not_types = {
      "return", "new", "delete", "case", "else", "throw", "goto", "co_return", "co_await", "sizeof", "typedef",
      "using", "namespace", "public", "private", "protected", "operator", "do", "if", "while", "for", "switch",
      "class", "struct", "enum", "template", "typename", "friend", "this", "true", "false", "nullptr", "decltype"};
  return !not_types.count(w);
}

// ── tokenizer ────────────────────────────────────────────────────────────────

struct Tok {
  enum Kind { Ident, Num, Str, Punct } kind;
  std::string s;
};

bool ident_start(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
bool ident_char(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

std::vector<Tok> tokenize(const std::string& t, std::size_t limit) {
  std::vector<Tok> out;
  std::size_t i = 0;
  const std::size_t n = std::min(limit, t.size());
  bool line_start = true;
  while (i < n) {
    const char c = t[i];
    if (c == '\n') { line_start = true; ++i; continue; }
    if (c == ' ' || c == '\t' || c == '\r') { ++i; continue; }
    if (line_start && c == '#') { // preprocessor line
      while (i < n && t[i] != '\n') ++i;
      continue;
    }
    line_start = false;
    if (c == '/' && i + 1 < n && t[i + 1] == '/') { while (i < n && t[i] != '\n') ++i; continue; }
    if (c == '/' && i + 1 < n && t[i + 1] == '*') {
      const auto e = t.find("*/", i + 2);
      i = e == std::string::npos ? n : e + 2;
      continue;
    }
    if (c == '"' || c == '\'') {
      std::size_t j = i + 1;
      while (j < n && t[j] != c && t[j] != '\n') j += t[j] == '\\' ? 2 : 1;
      out.push_back({Tok::Str, t.substr(i, std::min(j + 1, n) - i)});
      i = std::min(j + 1, n);
      continue;
    }
    if (ident_start(c)) {
      std::size_t j = i;
      while (j < n && ident_char(t[j])) ++j;
      out.push_back({Tok::Ident, t.substr(i, j - i)});
      i = j;
      continue;
    }
    if (std::isdigit(static_cast<unsigned char>(c))) {
      std::size_t j = i;
      while (j < n && (std::isalnum(static_cast<unsigned char>(t[j])) || t[j] == '.' || t[j] == '\'')) ++j;
      out.push_back({Tok::Num, t.substr(i, j - i)});
      i = j;
      continue;
    }
    if (i + 1 < n && ((c == ':' && t[i + 1] == ':') || (c == '-' && t[i + 1] == '>') || (c == '&' && t[i + 1] == '&'))) {
      out.push_back({Tok::Punct, t.substr(i, 2)});
      i += 2;
      continue;
    }
    out.push_back({Tok::Punct, std::string(1, c)});
    ++i;
  }
  return out;
}

// ── scope scanner ────────────────────────────────────────────────────────────

struct Decl {
  std::string name, type; // type: base name used for member lookup ("Window", "property", ...)
  std::string detail;     // shown in the popup
  K kind = K::Variable;
};

struct Scan {
  std::vector<std::vector<Decl>> visible;        // innermost scope first
  std::vector<Decl> pending;                      // declarations in an open ( ... ) at the caret
  std::string current_class;                      // inside a member function / class body
  std::map<std::string, std::vector<Decl>> classes; // class -> members
  std::vector<Decl> globals;                      // namespace scope declarations
};

struct Block {
  enum Kind { Global, Namespace, Class, Function, Plain } kind;
  std::string cls; // class whose members are visible here
  std::vector<Decl> decls;
};

bool is(const std::vector<Tok>& t, std::size_t i, const char* s) { return i < t.size() && t[i].s == s; }

// index of the '(' matching the ')' at `close`, or npos
std::size_t match_open(const std::vector<Tok>& t, std::size_t close, const char* open, const char* shut) {
  int depth = 0;
  for (std::size_t k = close + 1; k-- > 0;) {
    if (t[k].kind != Tok::Punct) continue;
    if (t[k].s == shut) ++depth;
    else if (t[k].s == open && --depth == 0) return k;
  }
  return std::string::npos;
}

// Type of `auto x = <init>`: find<T>(...), make_unique<T>, T(...), T{...}, new T
std::string infer_auto(const std::vector<Tok>& t, std::size_t eq) {
  for (std::size_t k = eq + 1; k < t.size() && k < eq + 40; ++k) {
    if (is(t, k, ";") || is(t, k, ",") || is(t, k, ")")) break;
    if (t[k].kind == Tok::Ident &&
        (t[k].s == "find" || t[k].s == "make_unique" || t[k].s == "make_shared" || t[k].s == "static_cast" ||
         t[k].s == "dynamic_cast") &&
        is(t, k + 1, "<") && k + 2 < t.size() && t[k + 2].kind == Tok::Ident) {
      std::size_t j = k + 2;
      while (is(t, j + 1, "::") && j + 2 < t.size()) j += 2; // pulseui::Button -> Button
      return t[j].s;
    }
    if (t[k].s == "new" && k + 1 < t.size() && t[k + 1].kind == Tok::Ident) return t[k + 1].s;
  }
  if (eq + 1 < t.size() && t[eq + 1].kind == Tok::Ident) {
    std::size_t j = eq + 1;
    while (is(t, j + 1, "::") && j + 2 < t.size()) j += 2;
    if (is(t, j + 1, "(") || is(t, j + 1, "{") || is(t, j + 1, "<")) return t[j].s;
  }
  return "auto";
}

Scan scan(const std::vector<Tok>& t) {
  std::vector<Block> stack{{Block::Global, {}, {}}};
  std::vector<Decl> pending;
  Scan out;
  int paren = 0;

  auto enclosing_class = [&]() -> std::string {
    for (auto it = stack.rbegin(); it != stack.rend(); ++it)
      if (!it->cls.empty()) return it->cls;
    return {};
  };
  auto close_block = [&](Block& b) {
    if (b.kind == Block::Class) {
      auto& m = out.classes[b.cls];
      m.insert(m.end(), b.decls.begin(), b.decls.end());
    } else if (b.kind == Block::Global || b.kind == Block::Namespace) {
      out.globals.insert(out.globals.end(), b.decls.begin(), b.decls.end());
    }
  };

  for (std::size_t i = 0; i < t.size(); ++i) {
    const Tok& tk = t[i];
    if (tk.kind == Tok::Punct) {
      if (tk.s == "(") {
        if (++paren == 1) pending.clear();
      } else if (tk.s == ")") {
        paren = std::max(0, paren - 1);
      } else if (tk.s == ";" && paren == 0) {
        pending.clear();
      } else if (tk.s == "{") {
        Block b{Block::Plain, enclosing_class(), {}};
        // look back for what opens this block
        std::size_t k = i;
        while (k > 0 && t[k - 1].kind == Tok::Ident &&
               (t[k - 1].s == "const" || t[k - 1].s == "override" || t[k - 1].s == "noexcept" ||
                t[k - 1].s == "mutable" || t[k - 1].s == "final"))
          --k;
        std::string cls_name, ns_name;
        for (std::size_t j = i; j-- > 0;) {
          if (t[j].s == ";" || t[j].s == "{" || t[j].s == "}" || t[j].s == ")") break;
          if ((t[j].s == "class" || t[j].s == "struct") && j + 1 < i && t[j + 1].kind == Tok::Ident) cls_name = t[j + 1].s;
          if (t[j].s == "namespace") ns_name = j + 1 < i ? t[j + 1].s : "_";
        }
        if (!cls_name.empty()) {
          b.kind = Block::Class;
          b.cls = cls_name;
        } else if (!ns_name.empty()) {
          b.kind = Block::Namespace;
        } else if (k > 0 && t[k - 1].s == ")") {
          b.kind = Block::Function;
          b.decls = pending;
          const std::size_t open = match_open(t, k - 1, "(", ")");
          // Class::method(...) { -> members of Class are visible
          if (open != std::string::npos && open >= 3 && t[open - 1].kind == Tok::Ident && t[open - 2].s == "::")
            b.cls = t[open - 3].s == "~" && open >= 4 ? t[open - 4].s : t[open - 3].s;
        }
        pending.clear();
        stack.push_back(std::move(b));
      } else if (tk.s == "}") {
        if (stack.size() > 1) {
          close_block(stack.back());
          stack.pop_back();
        }
      }
      continue;
    }
    if (tk.kind != Tok::Ident || keywords().count(tk.s)) {
      // class / struct names are declarations of the enclosing scope
      if ((tk.s == "class" || tk.s == "struct") && i + 1 < t.size() && t[i + 1].kind == Tok::Ident &&
          (is(t, i + 2, "{") || is(t, i + 2, ":") || is(t, i + 2, ";") || is(t, i + 2, "final")))
        stack.back().decls.push_back({t[i + 1].s, t[i + 1].s, "class", K::Type});
      continue;
    }
    // declaration: <type> name followed by = ; , ( ) { [ :
    if (i == 0 || i + 1 >= t.size()) continue;
    const std::string& next = t[i + 1].s;
    if (!(next == "=" || next == ";" || next == "," || next == "(" || next == ")" || next == "{" || next == "[" || next == ":"))
      continue;
    std::size_t p = i - 1;
    while (p > 0 && t[p].kind == Tok::Punct && (t[p].s == "*" || t[p].s == "&" || t[p].s == "&&")) --p;
    std::string base, detail;
    if (t[p].s == ">") {
      const std::size_t lt = match_open(t, p, "<", ">");
      if (lt == std::string::npos || lt == 0 || t[lt - 1].kind != Tok::Ident) continue;
      base = t[lt - 1].s;
      for (std::size_t q = lt - 1; q <= p; ++q) detail += t[q].s;
    } else if (t[p].kind == Tok::Ident && type_word(t[p].s) && !is(t, p - 1, ".") && !is(t, p - 1, "->")) {
      base = t[p].s;
      detail = base;
      if (p >= 2 && is(t, p - 1, "::") && t[p - 2].s == "Ui") { // Ui::MainWindow ui_; -> the generated form struct
        base = "Ui::" + base;
        detail = base;
      }
    } else {
      continue;
    }
    if (base == "const" || base == "static" || base == "constexpr" || base == "inline") continue;
    if (base == "auto" && next == "=") {
      base = infer_auto(t, i + 1);
      detail = base == "auto" ? "auto" : "auto → " + base;
    }
    Decl d{tk.s, base, detail, K::Variable};
    const Block::Kind bk = stack.back().kind;
    const bool scope_level = bk == Block::Global || bk == Block::Namespace || bk == Block::Class;
    if (paren > 0) {
      pending.push_back(d); // parameters, for/if/lambda headers -> the next block
    } else {
      if (next == "(" && scope_level) d.kind = K::Function;
      else if (bk == Block::Class) d.kind = K::Member;
      stack.back().decls.push_back(d);
    }
  }

  // what is visible at the end of the scanned text (= the caret)
  for (auto it = stack.rbegin(); it != stack.rend(); ++it) out.visible.push_back(it->decls);
  out.pending = pending;
  out.current_class = enclosing_class();
  for (auto it = stack.rbegin(); it != stack.rend(); ++it)
    if (it->kind == Block::Class) {
      auto& m = out.classes[it->cls];
      m.insert(m.end(), it->decls.begin(), it->decls.end());
    } else if (it->kind == Block::Global || it->kind == Block::Namespace) {
      out.globals.insert(out.globals.end(), it->decls.begin(), it->decls.end());
    }
  return out;
}

// Other project files are rescanned only when their text changes.
const Scan& scan_file(const std::string& name, const std::string& text) {
  struct Entry {
    std::size_t hash;
    Scan scan;
  };
  static std::unordered_map<std::string, Entry> cache;
  const std::size_t h = std::hash<std::string>{}(text);
  auto it = cache.find(name);
  if (it == cache.end() || it->second.hash != h) {
    if (cache.size() > 256) cache.clear();
    it = cache.insert_or_assign(name, Entry{h, scan(tokenize(text, text.size()))}).first;
  }
  return it->second.scan;
}

// ── assembling results ───────────────────────────────────────────────────────

std::string lower(std::string s) {
  for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

struct Collector {
  std::string prefix, lprefix;
  std::vector<std::pair<int, CompletionItem>> ranked;
  std::set<std::string> seen;
  void add(const CompletionItem& it, int rank) {
    if (it.label.empty() || seen.count(it.label)) return;
    const std::string l = lower(it.label);
    int score;
    if (it.label.rfind(prefix, 0) == 0) score = 0;            // exact-case prefix
    else if (l.rfind(lprefix, 0) == 0) score = 1;             // prefix ignoring case
    else if (prefix.size() >= 3 && l.find(lprefix) != std::string::npos) score = 2; // contains
    else return;
    seen.insert(it.label);
    ranked.push_back({score * 100 + rank, it});
  }
  std::vector<CompletionItem> result() {
    std::stable_sort(ranked.begin(), ranked.end(), [](auto& a, auto& b) {
      return a.first != b.first ? a.first < b.first : a.second.label < b.second.label;
    });
    std::vector<CompletionItem> out;
    for (auto& [_, it] : ranked) {
      out.push_back(it);
      if (out.size() >= 80) break;
    }
    return out;
  }
};

CompletionItem as_item(const Decl& d) { return {d.name, d.detail, d.kind, {}}; }

std::string strip_ns(std::string type) {
  const auto p = type.rfind("::");
  return p == std::string::npos ? type : type.substr(p + 2);
}

} // namespace

void collect_form_views(const std::string& form_json, const std::string& form_class, std::vector<FormView>& out) {
  auto j = pulseui::json::Value::parse(form_json);
  if (!j) return;
  std::function<void(const pulseui::json::Value&)> walk = [&](const pulseui::json::Value& v) {
    const std::string id = v["props"]["id"].as_string();
    if (!id.empty()) out.push_back({id, v["type"].as_string(), form_class});
    for (auto& c : v["children"].items()) walk(c);
  };
  walk((*j)["root"]);
}

CompletionResult complete_cpp(const std::vector<std::string>& lines, std::size_t line, std::size_t col,
                              const CompletionContext& ctx) {
  CompletionResult r;
  if (line >= lines.size()) return r;
  const std::string& cur = lines[line];
  col = std::min(col, cur.size());
  const std::string before = cur.substr(0, col);
  std::size_t word = col;
  while (word > 0 && ident_char(cur[word - 1])) --word;
  r.replace_from = word;
  const std::string prefix = cur.substr(word, col - word);
  Collector c{prefix, lower(prefix), {}, {}};

  // in a comment?
  if (before.find("//") != std::string::npos) {
    const std::size_t q = before.find("//");
    if (std::count(before.begin(), before.begin() + static_cast<std::ptrdiff_t>(q), '"') % 2 == 0) return r;
  }

  // ids of the designed views: find<Button>("o|
  static const std::regex find_re(R"(find\s*<\s*(?:\w+\s*::\s*)?(\w+)\s*>\s*\(\s*"([\w\-]*)$)");
  std::smatch m;
  if (std::regex_search(before, m, find_re)) {
    const std::string type = m[1];
    r.replace_from = col - m[2].length();
    c.prefix = m[2];
    c.lprefix = lower(c.prefix);
    for (auto& v : ctx.views)
      if (type == "View" || type == v.type) c.add({v.id, v.type + " · " + v.form, K::Value, {}}, 0);
    for (auto& v : ctx.views) // other types after the matching ones
      if (type != "View" && type != v.type) c.add({v.id, v.type + " · " + v.form, K::Value, {}}, 50);
    r.items = c.result();
    return r;
  }
  // inside any other string literal: nothing
  if (std::count(before.begin(), before.end(), '"') % 2 == 1) return r;
  if (!prefix.empty() && std::isdigit(static_cast<unsigned char>(prefix[0]))) return r;

  // scan the current file up to the caret
  std::string text;
  for (std::size_t i = 0; i < line; ++i) text += lines[i] + '\n';
  text += before.substr(0, word);
  const Scan here = scan(tokenize(text, text.size()));

  // classes and globals of the whole project
  std::map<std::string, std::vector<Decl>> classes = here.classes;
  std::vector<Decl> globals = here.globals;
  for (auto& [name, body] : ctx.other_files) {
    if (name == ctx.current_file) continue;
    const Scan& s = scan_file(name, body);
    for (auto& [k, v] : s.classes) classes[k].insert(classes[k].end(), v.begin(), v.end());
    globals.insert(globals.end(), s.globals.begin(), s.globals.end());
  }

  auto lookup = [&](const std::string& name) -> std::string {
    for (auto& d : here.pending)
      if (d.name == name) return d.type;
    for (auto& scope : here.visible)
      for (auto it = scope.rbegin(); it != scope.rend(); ++it)
        if (it->name == name) return it->type;
    if (!here.current_class.empty())
      for (auto& d : classes[here.current_class])
        if (d.name == name) return d.type;
    for (auto& d : globals)
      if (d.name == name) return d.type;
    return {};
  };
  auto add_form_members = [&](const std::string& form) {
    for (auto& v : ctx.views)
      if (v.form == form) c.add({v.id, v.type + "*", K::Member, {}}, 0);
    c.add({"setup", "(window, model) — build the form", K::Function, {}}, 2);
    c.add({"build", "(model) → Node", K::Function, {}}, 2);
    for (auto* n : {"form_title", "form_width", "form_height"}) c.add({n, "constexpr", K::Member, {}}, 3);
  };
  auto add_members = [&](std::string type) {
    if (type.rfind("Ui::", 0) == 0) return add_form_members(type.substr(4));
    type = strip_ns(type);
    if (auto it = api().members.find(type); it != api().members.end())
      for (auto& mbr : it->second) c.add(mbr, 0);
    if (auto it = classes.find(type); it != classes.end())
      for (auto& d : it->second) c.add(as_item(d), 0);
  };

  const std::string head = before.substr(0, word);
  static const std::regex member_re(R"(([A-Za-z_]\w*)\s*(?:\(\s*\))?\s*(\.|->)\s*$)");
  static const std::regex find_member_re(R"(find\s*<\s*(?:\w+\s*::\s*)?(\w+)\s*>\s*\([^()]*\)\s*(\.|->)\s*$)");
  static const std::regex scope_re(R"(([A-Za-z_]\w*)\s*::\s*$)");
  static const std::regex scope2_re(R"(([A-Za-z_]\w*)\s*::\s*([A-Za-z_]\w*)\s*::\s*$)");

  static const std::regex chain_re(R"(([A-Za-z_]\w*)\s*(?:\.|->)\s*([A-Za-z_]\w*)\s*(\.|->)\s*$)");
  std::string form_view_type; // ui_.ok-> : the type of a view of a designed form
  if (std::regex_search(head, m, chain_re)) {
    const std::string owner = lookup(m[1]);
    if (owner.rfind("Ui::", 0) == 0)
      for (auto& v : ctx.views)
        if (v.form == owner.substr(4) && v.id == m[2].str()) form_view_type = v.type == "Image" ? "ImageView" : v.type;
  }
  if (!form_view_type.empty()) {
    add_members(form_view_type);
  } else if (std::regex_search(head, m, find_member_re)) { // win.find<Button>("ok")->
    add_members(m[1]);
  } else if (std::regex_search(head, m, member_re)) { // obj. / obj-> / this->
    const std::string obj = m[1];
    if (obj == "this") {
      for (auto& d : classes[here.current_class]) c.add(as_item(d), 0);
    } else {
      std::string type = lookup(obj);
      if (type.empty() && api().members.count(obj)) type = obj; // static-like usage
      add_members(type);
    }
  } else if (std::regex_search(head, m, scope2_re) && m[1] == "Ui") { // Ui::MainWindow::
    for (auto* n : {"form_title", "form_width", "form_height"}) c.add({n, "constexpr", K::Member, {}}, 0);
    c.add({"build", "() → Node (layout only)", K::Function, {}}, 1);
  } else if (std::regex_search(head, m, scope_re)) { // Type:: / namespace::
    const std::string scope = m[1];
    if (scope == "Ui") {
      for (auto& f : ctx.form_classes) c.add({f, "form", K::Type, {}}, 0);
    } else if (auto it = api().scopes.find(scope); it != api().scopes.end()) {
      for (auto& it2 : it->second) c.add(it2, 0);
    } else if (auto cl = classes.find(scope); cl != classes.end()) {
      for (auto& d : cl->second) c.add(as_item(d), 0);
    }
  } else {
    if (prefix.empty()) return r; // plain identifiers need at least one character
    for (auto& d : here.pending) c.add(as_item(d), 0);
    int depth = 0;
    for (auto& scope : here.visible) {
      for (auto it = scope.rbegin(); it != scope.rend(); ++it) c.add(as_item(*it), depth < 2 ? 0 : 5);
      ++depth;
    }
    if (!here.current_class.empty())
      for (auto& d : classes[here.current_class]) c.add(as_item(d), 3);
    for (auto& d : globals) c.add(as_item(d), 6);
    for (auto& [name, _] : classes) c.add({name, "class", K::Type, {}}, 6);
    for (auto& g : api().globals) c.add(g, 10);
    for (auto& k : keywords()) c.add({k, "", K::Keyword, {}}, 20);
  }
  r.items = c.result();
  return r;
}

pulseui::CodeEditor::CompletionProvider make_cpp_completion(std::function<CompletionContext()> context) {
  return [context](const std::vector<std::string>& lines, std::size_t line, std::size_t col) {
    return complete_cpp(lines, line, col, context());
  };
}

} // namespace layer
