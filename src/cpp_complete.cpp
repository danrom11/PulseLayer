#include "cpp_complete.hpp"

#include <algorithm>
#include <cctype>
#include <functional>
#include <map>
#include <memory>
#include <optional>
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

const std::string kArrow = "\xE2\x86\x92"; // "→" in member details: "(args) → Result"
constexpr std::size_t npos = std::string::npos;

std::string trim(const std::string& s) {
  const auto b = s.find_first_not_of(" \t");
  if (b == npos) return {};
  return s.substr(b, s.find_last_not_of(" \t") - b + 1);
}

bool starts_with(const std::string& s, const std::string& p) { return s.rfind(p, 0) == 0; }

// splits at `sep` outside of <> () [] {}
std::vector<std::string> split_top(const std::string& s, char sep) {
  std::vector<std::string> out;
  int depth = 0;
  std::string cur;
  for (char c : s) {
    if (c == '<' || c == '(' || c == '[' || c == '{') ++depth;
    if (c == '>' || c == ')' || c == ']' || c == '}') --depth;
    if (c == sep && depth == 0) {
      out.push_back(trim(cur));
      cur.clear();
    } else {
      cur += c;
    }
  }
  if (!trim(cur).empty() || !out.empty()) out.push_back(trim(cur));
  return out;
}

// ── types ────────────────────────────────────────────────────────────────────
//
// Types are kept as text ("property<std::string>", "Button*", "Ui::MainWindow")
// and parsed on demand. That is all completion needs: a name to look members up
// by, template arguments for T, and whether it is a pointer.

struct Ty {
  std::string base;              // "vector", "Button", "Ui::MainWindow"
  std::vector<std::string> args; // template arguments as written
  int ptr = 0;
  bool empty() const { return base.empty(); }
};

std::string canon(const std::string& b) {
  static const std::map<std::string, std::string> alias = {
      {"string_view", "string"}, {"basic_string", "string"}, {"ViewT", "View"}, {"Model", "property"},
      {"multimap", "map"},       {"unordered_multimap", "unordered_map"},     {"QString", "string"}};
  auto it = alias.find(b);
  return it == alias.end() ? b : it->second;
}

Ty parse_ty(std::string s) {
  Ty t;
  s = trim(s);
  for (bool again = true; again;) {
    again = false;
    for (const char* q : {"const ", "constexpr ", "static ", "inline ", "typename ", "mutable ", "volatile ", "struct ", "class "})
      if (starts_with(s, q)) {
        s = trim(s.substr(std::char_traits<char>::length(q)));
        again = true;
      }
  }
  while (!s.empty()) { // trailing const, &, *
    const char c = s.back();
    if (c == '&' || c == ' ') s.pop_back();
    else if (c == '*') { ++t.ptr; s.pop_back(); }
    else if (s.size() > 5 && s.compare(s.size() - 5, 5, "const") == 0 && !std::isalnum(static_cast<unsigned char>(s[s.size() - 6])) &&
             s[s.size() - 6] != '_')
      s.erase(s.size() - 5);
    else break;
  }
  std::string base = s;
  if (const auto lt = s.find('<'); lt != npos && s.back() == '>') {
    base = trim(s.substr(0, lt));
    t.args = split_top(s.substr(lt + 1, s.size() - lt - 2), ',');
  }
  if (starts_with(base, "Ui::")) {
    t.base = base;
  } else {
    const auto p = base.rfind("::");
    t.base = canon(p == npos ? base : base.substr(p + 2));
  }
  if (t.base == "auto" || t.base == "void" || t.base == "decltype") t.base.clear();
  return t;
}

std::string ty_str(const Ty& t) {
  std::string s = t.base;
  if (!t.args.empty()) {
    s += '<';
    for (std::size_t i = 0; i < t.args.size(); ++i) s += (i ? ", " : "") + t.args[i];
    s += '>';
  }
  return s + std::string(static_cast<std::size_t>(std::max(0, t.ptr)), '*');
}

// T / A / K / E -> 1st template argument, U / B / V -> 2nd, Self -> the owner type
std::string subst(const std::string& type, const Ty& owner, const std::string& explicit_targ) {
  Ty self = owner;
  self.ptr = 0;
  auto arg = [&](std::size_t i) { return i < owner.args.size() ? owner.args[i] : std::string(); };
  std::string out;
  for (std::size_t i = 0; i < type.size();) {
    if (std::isalpha(static_cast<unsigned char>(type[i])) || type[i] == '_') {
      std::size_t j = i;
      while (j < type.size() && (std::isalnum(static_cast<unsigned char>(type[j])) || type[j] == '_')) ++j;
      const std::string w = type.substr(i, j - i);
      if (w == "T" || w == "A" || w == "K" || w == "E") out += !explicit_targ.empty() && w == "T" ? explicit_targ : arg(0);
      else if (w == "B" || w == "V" || w == "U") out += arg(1);
      else if (w == "R") out += arg(0);
      else if (w == "Self") out += ty_str(self);
      else out += w;
      i = j;
    } else {
      out += type[i++];
    }
  }
  return out;
}

// "(a, b) → R" -> "R"; "(a, b)" -> "" ; a data member's detail is its type
std::string result_of(const std::string& detail) {
  if (detail.empty() || (detail[0] != '(' && detail[0] != '<')) return detail;
  std::size_t i = 0;
  if (detail[0] == '<') {
    int d = 0;
    for (; i < detail.size(); ++i) {
      if (detail[i] == '<') ++d;
      if (detail[i] == '>' && --d == 0) { ++i; break; }
    }
  }
  int d = 0;
  for (; i < detail.size(); ++i) {
    if (detail[i] == '(') ++d;
    if (detail[i] == ')' && --d == 0) break;
  }
  const auto a = detail.find(kArrow, i);
  return a == npos ? std::string() : trim(detail.substr(a + kArrow.size()));
}

bool is_function_detail(const std::string& d) { return !d.empty() && (d[0] == '(' || (d[0] == '<' && d.find('(') != npos)); }

// ── API knowledge (PulseUI / Pulse / std) ────────────────────────────────────

struct Api {
  std::map<std::string, std::vector<CompletionItem>> members; // type -> members
  std::map<std::string, std::vector<CompletionItem>> scopes;  // "Alignment" -> enumerators, "pulse" -> names
  std::vector<CompletionItem> globals;                        // names usable without qualification
  std::vector<CompletionItem> view_common;                    // modifiers every view has
  std::set<std::string> views;                                // types that have the common view modifiers
  std::vector<CompletionItem> pulse_ops;                      // operators for `stream | ...`
  std::vector<std::string> headers;                           // #include <...>
};

std::vector<CompletionItem> items(K kind, std::initializer_list<std::pair<const char*, const char*>> list) {
  std::vector<CompletionItem> out;
  for (auto& [n, d] : list) out.push_back({n, d, kind, {}});
  return out;
}

const Api& api() {
  static const Api a = [] {
    Api a;
    // modifiers return the view itself (Self), so they chain: Button(...).primary().flex()
    a.view_common = items(K::Function, {
        {"id", "(string id)"}, {"padding", "(Insets p)"}, {"width", "(float w)"}, {"height", "(float h)"},
        {"size", "(float w, float h)"}, {"min_width", "(float w)"}, {"min_height", "(float h)"}, {"max_width", "(float w)"},
        {"max_height", "(float h)"}, {"flex", "(float grow = 1)"}, {"align", "(Alignment a)"}, {"position", "(float x, float y)"},
        {"radius", "(float r)"}, {"clip", "(bool c = true)"}, {"opacity", "(float o)"}, {"tooltip", "(Bind<string> text)"},
        {"cursor", "(Cursor c)"}, {"focusable", "(bool f = true)"}, {"background", "(Bind<Color> c)"},
        {"border", "(Bind<Color> c, float width = 1)"}, {"visible", "(Bind<bool> b)"}, {"enabled", "(Bind<bool> b)"},
        {"on_click", "(function<void()> fn)"}, {"on_hover", "(function<void(bool)> fn)"},
        {"on_event_fn", "(function<void(const InputEvent&)> fn)"}, {"ref", "(T*& out)"}, {"on_mount_fn", "(function<void(T&)> fn)"},
        {"find", "<T>(string id) → T*"}, {"get", "<T>(string id) → T&"}, {"add", "(Node n) → View&"},
        {"insert", "(size_t index, Node n) → View&"}, {"remove", "(View* child) → void"}, {"clear_children", "() → void"},
        {"children", "() → vector<unique_ptr<View>>"}, {"parent", "() → View*"}, {"window", "() → Window*"},
        {"frame", "() → Rect"}, {"bounds", "() → Rect"}, {"content_rect", "() → Rect"}, {"to_window", "(Point local) → Point"},
        {"set_visible", "(bool v) → void"}, {"set_enabled", "(bool e) → void"}, {"is_visible", "() → bool"},
        {"is_enabled", "() → bool"}, {"focused", "() → bool"}, {"hovered", "() → bool"}, {"pressed", "() → bool"},
        {"focus", "() → void"}, {"invalidate", "() → void"}, {"set_tooltip", "(string t) → void"}, {"style", "() → Style&"},
        {"set_id", "(string id) → void"}, {"type_name", "() → const char*"}, {"mounted", "() → bool"},
        {"annotation", "(string key) → const string*"}, {"set_annotation", "(string key, string value) → void"}});
    auto add_view = [&](const std::string& t, std::vector<CompletionItem> own) {
      a.views.insert(t);
      a.members[t] = std::move(own);
    };
    add_view("View", {});
    add_view("Button", items(K::Member, {
        {"clicked", "signal<>"}, {"clicks", "() → observable<unit>"}, {"label", "() → string"},
        {"set_label", "(string l) → void"}, {"bind_label", "(Bind<string> text)"}, {"primary", "()"}, {"flat", "()"},
        {"danger", "()"}, {"kind", "(ButtonKind k)"}, {"icon", "(string glyph)"}, {"set_kind", "(ButtonKind k) → void"}}));
    add_view("Text", items(K::Member, {
        {"text", "() → string"}, {"set_text", "(string t) → void"}, {"bind_text", "(Bind<string> text)"},
        {"font_size", "(float s)"}, {"weight", "(FontWeight w)"}, {"bold", "()"}, {"title", "()"}, {"dim", "()"},
        {"wrap", "(bool w = true)"}, {"text_align", "(int align)"}, {"color", "(Bind<Color> c)"}, {"mono", "()"},
        {"font", "() → Font"}}));
    add_view("TextField", items(K::Member, {
        {"text", "() → string"}, {"set_text", "(string t) → void"}, {"model", "(property<string>& m)"},
        {"placeholder", "(Bind<string> p)"}, {"password", "(bool p = true)"},
        {"on_change", "(function<void(const string&)> fn)"}, {"on_submit", "(function<void(const string&)> fn)"},
        {"on_commit", "(function<void(const string&)> fn)"}, {"changed", "signal<string>"}, {"submitted", "signal<string>"},
        {"committed", "signal<string>"}, {"text_changes", "() → observable<string>"}, {"has_selection", "() → bool"}}));
    const auto boolean = items(K::Member, {
        {"checked", "() → bool"}, {"set_checked", "(bool c, bool notify = true) → void"}, {"model", "(property<bool>& m)"},
        {"on_change", "(function<void(bool)> fn)"}, {"changed", "signal<bool>"}, {"label", "() → string"},
        {"set_label", "(string l) → void"}});
    add_view("Checkbox", boolean);
    add_view("Toggle", boolean);
    add_view("Slider", items(K::Member, {
        {"value", "() → double"}, {"set_value", "(double v, bool notify = true) → void"}, {"model", "(property<double>& m)"},
        {"on_change", "(function<void(double)> fn)"}, {"changed", "signal<double>"}, {"step", "(double s)"},
        {"min", "() → double"}, {"max", "() → double"}, {"set_range", "(double lo, double hi) → void"}}));
    add_view("ProgressBar", items(K::Member, {
        {"value", "() → double"}, {"set_value", "(double v) → void"}, {"bind_value", "(Bind<double> fraction)"}}));
    add_view("Select", items(K::Member, {
        {"index", "() → int"}, {"set_index", "(int i, bool notify = true) → void"}, {"options", "() → vector<string>"},
        {"set_options", "(vector<string> o) → void"}, {"model", "(property<int>& m)"},
        {"bind_options", "(Bind<vector<string>> o)"}, {"on_change", "(function<void(int)> fn)"}, {"changed", "signal<int>"},
        {"open_popup", "() → void"}}));
    add_view("Segmented", items(K::Member, {
        {"index", "() → int"}, {"set_index", "(int i, bool notify = true) → void"}, {"on_change", "(function<void(int)> fn)"},
        {"changed", "signal<int>"}, {"set_options", "(vector<string> o) → void"}}));
    add_view("Tabs", items(K::Member, {
        {"current", "() → int"}, {"set_current", "(int i, bool notify = true) → void"}, {"selected", "(property<int>& m)"},
        {"add_tab", "(Tab t)"}, {"changed", "signal<int>"}, {"titles", "() → vector<string>"},
        {"set_title", "(int i, string t) → void"}}));
    add_view("CodeEditor", items(K::Member, {
        {"text", "() → string"}, {"set_text", "(string text) → void"}, {"append", "(string text) → void"},
        {"clear", "() → void"}, {"model", "(property<string>& m)"}, {"read_only", "(bool r = true)"},
        {"line_numbers", "(bool on = true)"}, {"highlighter", "(Highlighter h)"}, {"completion", "(CompletionProvider p)"},
        {"on_change", "(function<void(const string&)> fn)"}, {"changed", "signal<string>"},
        {"diagnostics", "(Bind<vector<Diagnostic>> d)"}, {"go_to", "(int line, int column = 1) → void"},
        {"caret_line", "() → int"}, {"caret_column", "() → int"}, {"line_count", "() → size_t"}}));
    const auto stack = items(K::Member, {{"spacing", "(float s)"}, {"align_items", "(Alignment a)"}, {"justify", "(Justify j)"}});
    for (auto t : {"VStack", "HStack", "Panel"}) add_view(t, stack);
    add_view("Scroll", items(K::Member, {{"horizontal", "(bool h = true)"}}));
    add_view("ForEach", items(K::Member, {{"horizontal", "()"}}));
    add_view("Split", items(K::Member, {{"min_pane", "(float m)"}, {"keep_second", "()"}}));
    add_view("Canvas", items(K::Member, {{"redraw_on", "(source)"}}));
    add_view("ImageView", items(K::Member, {{"source", "() → string"}, {"set_source", "(string path) → void"},
                                           {"set_image", "(Image i) → void"}}));
    for (auto t : {"Layer", "Spacer", "Divider", "Show"}) add_view(t, {});

    a.members["Window"] = items(K::Function, {
        {"content", "(Node root) → Window&"}, {"show", "() → void"}, {"hide", "() → void"}, {"close", "() → void"},
        {"set_title", "(string title) → void"}, {"resize", "(float w, float h) → void"}, {"size", "() → Size"},
        {"set_min_size", "(float w, float h) → void"}, {"find", "<T>(string id) → T*"}, {"get", "<T>(string id) → T&"},
        {"shortcut", "(string keys, function<void()> fn) → Window&"}, {"input", "() → observable<InputEvent>"},
        {"closed", "signal<>"}, {"resized", "signal<Size>"}, {"on_close", "function<bool()>"},
        {"open_popup", "(Node popup, Point at, function<void()> on_dismiss = {}) → View*"},
        {"close_popup", "(View* popup) → void"}, {"focus_view", "() → View*"}, {"set_focus", "(View* v) → void"},
        {"focus_next", "(bool backwards = false) → void"}, {"screenshot", "(string png_path) → bool"},
        {"grab", "() → Image"}, {"root", "() → View*"}, {"scale", "() → float"}, {"is_open", "() → bool"},
        {"send", "(InputEvent e) → void"}, {"update_layout", "() → void"}, {"request_paint", "() → void"}});
    a.members["App"] = items(K::Function, {
        {"run", "() → int"}, {"quit", "(int code = 0) → void"}, {"post", "(function<void()> fn) → void"},
        {"ui", "() → executor&"}, {"after", "(milliseconds d, function<void()> fn) → subscription"},
        {"every", "(milliseconds d, function<void()> fn) → subscription"}, {"set_clipboard", "(string text) → void"},
        {"clipboard", "() → string"}, {"process_events", "(milliseconds d) → void"}, {"is_ui_thread", "() → bool"}});
    a.members["property"] = items(K::Function, {
        {"get", "() → T"}, {"set", "(T v) → bool"}, {"changed", "signal<T>"}, {"on_change", "(function<void(T)> fn) → connection"},
        {"map", "(fn) → observable"}, {"bind", "(source) → void"}, {"unbind", "() → void"}, {"update", "(function<void(T&)> fn) → bool"},
        {"as_observable", "() → observable<T>"}, {"bind_bidirectional", "(property<T>& other) → subscription"},
        {"is_bound", "() → bool"}});
    a.members["computed"] = items(K::Function, {
        {"get", "() → T"}, {"changed", "signal<T>"}, {"map", "(fn) → observable"}, {"as_observable", "() → observable<T>"},
        {"recompute", "() → void"}});
    a.members["signal"] = items(K::Function, {
        {"connect", "(fn) → connection"}, {"emit", "(args...) → void"}, {"as_observable", "() → observable<T>"},
        {"disconnect_all", "() → void"}, {"set_blocked", "(bool b) → void"}, {"forward_to", "(signal& other) → connection"},
        {"slot_count", "() → size_t"}});
    a.members["observable"] = items(K::Function, {
        {"subscribe", "(on_next, on_error = {}, on_completed = {}) → subscription"}, {"valid", "() → bool"}});
    a.members["subject"] = items(K::Function, {
        {"on_next", "(T v) → void"}, {"on_error", "(exception_ptr e) → void"}, {"on_completed", "() → void"},
        {"as_observable", "() → observable<T>"}, {"subscribe", "(on_next, on_error = {}, on_completed = {}) → subscription"}});
    a.members["behavior_subject"] = a.members["subject"];
    a.members["behavior_subject"].push_back({"value", "() → T", K::Function, {}});
    a.members["subscription"] = items(K::Function, {{"reset", "() → void"}, {"release", "() → void"}, {"active", "() → bool"},
                                                    {"unsubscribe", "() → void"}});
    a.members["composite_subscription"] = items(K::Function, {{"add", "(subscription s) → void"}, {"clear", "() → void"},
                                                              {"size", "() → size_t"}});
    a.members["connection"] = items(K::Function, {{"disconnect", "() → void"}, {"connected", "() → bool"},
                                                  {"scoped", "() → subscription"}});
    a.members["executor"] = items(K::Function, {{"post", "(function<void()> fn) → void"}});
    a.members["thread_pool"] = a.members["executor"];
    a.members["strand"] = a.members["executor"];
    a.members["topic"] = items(K::Function, {{"publish", "(T v) → void"}, {"subscribe", "(fn) → subscription"}});

    a.members["string"] = items(K::Function, {
        {"size", "() → size_t"}, {"length", "() → size_t"}, {"empty", "() → bool"}, {"c_str", "() → const char*"},
        {"data", "() → const char*"}, {"substr", "(size_t pos, size_t n = npos) → string"}, {"find", "(string s, size_t pos = 0) → size_t"},
        {"rfind", "(string s) → size_t"}, {"append", "(string s) → string&"}, {"push_back", "(char c) → void"},
        {"pop_back", "() → void"}, {"clear", "() → void"}, {"begin", "() → iterator"}, {"end", "() → iterator"},
        {"back", "() → char&"}, {"front", "() → char&"}, {"at", "(size_t i) → char&"}, {"erase", "(size_t pos, size_t n) → string&"},
        {"insert", "(size_t pos, string s) → string&"}, {"replace", "(size_t pos, size_t n, string s) → string&"},
        {"starts_with", "(string s) → bool"}, {"ends_with", "(string s) → bool"}, {"contains", "(string s) → bool"},
        {"compare", "(string s) → int"}, {"resize", "(size_t n) → void"}, {"reserve", "(size_t n) → void"}});
    a.members["vector"] = items(K::Function, {
        {"push_back", "(T v) → void"}, {"emplace_back", "(args...) → T&"}, {"size", "() → size_t"}, {"empty", "() → bool"},
        {"clear", "() → void"}, {"begin", "() → iterator"}, {"end", "() → iterator"}, {"front", "() → T&"}, {"back", "() → T&"},
        {"erase", "(iterator it) → iterator"}, {"insert", "(iterator it, T v) → iterator"}, {"resize", "(size_t n) → void"},
        {"reserve", "(size_t n) → void"}, {"at", "(size_t i) → T&"}, {"data", "() → T*"}, {"pop_back", "() → void"}});
    a.members["array"] = a.members["vector"];
    a.members["deque"] = a.members["vector"];
    a.members["list"] = a.members["vector"];
    a.members["map"] = items(K::Function, {
        {"size", "() → size_t"}, {"empty", "() → bool"}, {"clear", "() → void"}, {"at", "(K key) → V&"},
        {"find", "(K key) → iterator"}, {"count", "(K key) → size_t"}, {"contains", "(K key) → bool"},
        {"insert", "(pair<K, V> v) → pair"}, {"emplace", "(args...) → pair"}, {"insert_or_assign", "(K key, V v) → pair"},
        {"try_emplace", "(K key, args...) → pair"}, {"erase", "(K key) → size_t"}, {"begin", "() → iterator"},
        {"end", "() → iterator"}});
    a.members["unordered_map"] = a.members["map"];
    a.members["set"] = items(K::Function, {
        {"size", "() → size_t"}, {"empty", "() → bool"}, {"clear", "() → void"}, {"insert", "(T v) → pair"},
        {"erase", "(T v) → size_t"}, {"count", "(T v) → size_t"}, {"contains", "(T v) → bool"}, {"find", "(T v) → iterator"},
        {"begin", "() → iterator"}, {"end", "() → iterator"}});
    a.members["unordered_set"] = a.members["set"];
    a.members["pair"] = items(K::Member, {{"first", "A"}, {"second", "B"}});
    a.members["optional"] = items(K::Function, {
        {"has_value", "() → bool"}, {"value", "() → T&"}, {"value_or", "(T fallback) → T"}, {"reset", "() → void"},
        {"emplace", "(args...) → T&"}});
    a.members["unique_ptr"] = items(K::Function, {{"get", "() → T*"}, {"reset", "(T* p = nullptr) → void"},
                                                  {"release", "() → T*"}});
    a.members["shared_ptr"] = items(K::Function, {{"get", "() → T*"}, {"reset", "() → void"}, {"use_count", "() → long"}});
    a.members["weak_ptr"] = items(K::Function, {{"lock", "() → shared_ptr<T>"}, {"expired", "() → bool"}, {"reset", "() → void"}});
    a.members["function"] = items(K::Function, {{"operator()", "(args...)"}});

    a.members["Color"] = items(K::Function, {
        {"with_alpha", "(float a) → Color"}, {"lighter", "(float k) → Color"}, {"darker", "(float k) → Color"},
        {"luminance", "() → float"}, {"to_hex", "() → string"}, {"r", "float"}, {"g", "float"}, {"b", "float"}, {"a", "float"}});
    a.members["Point"] = items(K::Member, {{"x", "float"}, {"y", "float"}});
    a.members["Size"] = items(K::Member, {{"w", "float"}, {"h", "float"}});
    a.members["Insets"] = items(K::Member, {{"left", "float"}, {"top", "float"}, {"right", "float"}, {"bottom", "float"},
                                            {"horizontal", "() → float"}, {"vertical", "() → float"}});
    a.members["Rect"] = items(K::Member, {
        {"x", "float"}, {"y", "float"}, {"w", "float"}, {"h", "float"}, {"right", "() → float"}, {"bottom", "() → float"},
        {"pos", "() → Point"}, {"size", "() → Size"}, {"center", "() → Point"}, {"empty", "() → bool"},
        {"contains", "(Point p) → bool"}, {"intersects", "(Rect r) → bool"}, {"intersected", "(Rect r) → Rect"},
        {"united", "(Rect r) → Rect"}, {"inset", "(Insets i) → Rect"}, {"outset", "(float d) → Rect"},
        {"translated", "(float dx, float dy) → Rect"}});
    a.members["Font"] = items(K::Member, {{"family", "string"}, {"size", "float"}, {"weight", "FontWeight"}, {"italic", "bool"},
                                          {"with_size", "(float s) → Font"}, {"with_weight", "(FontWeight w) → Font"}});
    a.members["Image"] = items(K::Function, {{"valid", "() → bool"}, {"width", "() → int"}, {"height", "() → int"},
                                             {"save_png", "(string path) → bool"}});
    a.members["Theme"] = items(K::Member, {
        {"name", "string"}, {"window", "Color"}, {"surface", "Color"}, {"surface_alt", "Color"}, {"control", "Color"},
        {"control_hover", "Color"}, {"control_pressed", "Color"}, {"input", "Color"}, {"border", "Color"},
        {"border_focus", "Color"}, {"text", "Color"}, {"text_dim", "Color"}, {"text_disabled", "Color"}, {"accent", "Color"},
        {"accent_hover", "Color"}, {"accent_pressed", "Color"}, {"on_accent", "Color"}, {"selection", "Color"},
        {"danger", "Color"}, {"success", "Color"}, {"warning", "Color"}, {"shadow", "Color"}, {"tooltip", "Color"},
        {"scrollbar", "Color"}, {"radius", "float"}, {"radius_small", "float"}, {"padding", "float"}, {"spacing", "float"},
        {"control_height", "float"}, {"font", "Font"}, {"font_small", "Font"}, {"font_title", "Font"}, {"font_mono", "Font"}});
    a.members["Painter"] = items(K::Function, {
        {"save", "() → void"}, {"restore", "() → void"}, {"translate", "(float dx, float dy) → void"}, {"clip", "(Rect r) → void"},
        {"set_opacity", "(float a) → void"}, {"fill_rect", "(Rect r, Color c, float radius = 0) → void"},
        {"stroke_rect", "(Rect r, Color c, float width = 1, float radius = 0) → void"},
        {"fill_ellipse", "(Rect r, Color c) → void"}, {"stroke_ellipse", "(Rect r, Color c, float width = 1) → void"},
        {"line", "(Point a, Point b, Color c, float width = 1) → void"},
        {"polyline", "(vector<Point> pts, Color c, float width = 1, bool closed = false) → void"},
        {"fill_polygon", "(vector<Point> pts, Color c) → void"},
        {"fill_gradient", "(Rect r, Color top, Color bottom, float radius = 0) → void"},
        {"text", "(Rect r, string s, Font f, Color c, int align = Align::LeftCenter, bool wrap = false) → void"},
        {"image", "(Image img, Rect dst, float opacity = 1) → void"}});
    a.members["InputEvent"] = items(K::Member, {
        {"type", "Type"}, {"pos", "Point"}, {"window_pos", "Point"}, {"button", "MouseButton"}, {"clicks", "int"},
        {"key", "Key"}, {"mods", "unsigned"}, {"text", "string"}, {"wheel_dy", "float"}, {"is_mouse", "() → bool"},
        {"primary", "() → bool"}});
    a.members["Style"] = items(K::Member, {{"padding", "Insets"}, {"background", "optional<Color>"}, {"radius", "float"},
                                           {"opacity", "float"}});
    a.members["Harness"] = items(K::Function, {
        {"view", "(string id) → View&"}, {"text", "(string id) → string"}, {"visible", "(string id) → bool"},
        {"enabled", "(string id) → bool"}, {"click", "(string id) → Harness&"}, {"double_click", "(string id) → Harness&"},
        {"hover", "(string id) → Harness&"}, {"drag", "(string id, Point delta) → Harness&"},
        {"scroll", "(string id, float lines) → Harness&"}, {"focus", "(string id) → Harness&"},
        {"type", "(string text) → Harness&"}, {"fill", "(string id, string text) → Harness&"},
        {"press", "(string keys) → Harness&"}, {"run_for", "(milliseconds d) → Harness&"},
        {"wait_until", "(function<bool()> pred, milliseconds timeout = 5s) → bool"}, {"grab", "() → Image"},
        {"screenshot", "(string path) → bool"}, {"matches_snapshot", "(string path) → bool"}, {"window", "() → Window&"}});

    a.scopes["Alignment"] = items(K::Value, {{"Auto", ""}, {"Start", ""}, {"Center", ""}, {"End", ""}, {"Stretch", ""}});
    a.scopes["Justify"] = items(K::Value, {{"Start", ""}, {"Center", ""}, {"End", ""}, {"SpaceBetween", ""}});
    a.scopes["Orientation"] = items(K::Value, {{"Horizontal", ""}, {"Vertical", ""}});
    a.scopes["ButtonKind"] = items(K::Value, {{"Normal", ""}, {"Primary", ""}, {"Flat", ""}, {"Danger", ""}});
    a.scopes["FontWeight"] = items(K::Value, {{"Light", ""}, {"Normal", ""}, {"Medium", ""}, {"SemiBold", ""}, {"Bold", ""}});
    a.scopes["Cursor"] = items(K::Value, {{"Arrow", ""}, {"IBeam", ""}, {"Hand", ""}, {"Crosshair", ""}, {"SizeWE", ""},
                                          {"SizeNS", ""}, {"SizeAll", ""}});
    a.scopes["Align"] = items(K::Value, {{"Left", ""}, {"HCenter", ""}, {"Right", ""}, {"Top", ""}, {"VCenter", ""},
                                         {"Bottom", ""}, {"Center", ""}, {"LeftCenter", ""}, {"RightCenter", ""}});
    a.scopes["Mod"] = items(K::Value, {{"Shift", ""}, {"Ctrl", ""}, {"Alt", ""}, {"Super", ""}, {"Primary", "Ctrl / Cmd"}});
    a.scopes["MouseButton"] = items(K::Value, {{"Left", ""}, {"Right", ""}, {"Middle", ""}});
    a.scopes["Theme"] = items(K::Function, {{"dark", "() → Theme"}, {"light", "() → Theme"}});
    a.scopes["Color"] = items(K::Function, {
        {"hex", "(uint32_t rgb, float alpha = 1) → Color"}, {"rgb8", "(int r, int g, int b) → Color"},
        {"parse", "(string text) → Color"}, {"mix", "(Color a, Color b, float t) → Color"}, {"white", "() → Color"},
        {"black", "() → Color"}, {"transparent", "() → Color"}});
    a.scopes["App"] = items(K::Function, {
        {"post", "(function<void()> fn) → void"}, {"ui", "() → executor&"},
        {"after", "(milliseconds d, function<void()> fn) → subscription"},
        {"every", "(milliseconds d, function<void()> fn) → subscription"}, {"quit", "(int code = 0) → void"},
        {"instance", "() → App*"}, {"set_clipboard", "(string text) → void"}, {"clipboard", "() → string"},
        {"process_events", "(milliseconds d) → void"}, {"is_ui_thread", "() → bool"}});
    a.scopes["Window"] = items(K::Function, {{"all", "() → vector<Window*>"}});
    a.scopes["Image"] = items(K::Function, {{"load", "(string path) → Image"}, {"from_rgba", "(int w, int h, const uint8_t* rgba) → Image"}});
    a.scopes["Rect"] = items(K::Function, {{"from_points", "(Point a, Point b) → Rect"}});
    a.scopes["dialogs"] = items(K::Function, {
        {"open_file", "(Window* parent, string title, filters = {}) → optional<string>"},
        {"save_file", "(Window* parent, string title, filters = {}, string name = {}) → optional<string>"}});
    a.scopes["testing"] = items(K::Type, {{"Harness", "(Window& w) drives a window in tests"}, {"compare", "(Image a, Image b)"}});
    {
      std::vector<CompletionItem> keys;
      for (const char* k : {"Enter", "Escape", "Tab", "Space", "Backspace", "Delete", "Left", "Right", "Up", "Down", "Home", "End",
                            "PageUp", "PageDown", "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12"})
        keys.push_back({k, "", K::Value, {}});
      for (char c = 'A'; c <= 'Z'; ++c) keys.push_back({std::string(1, c), "", K::Value, {}});
      a.scopes["Key"] = keys;
    }
    a.pulse_ops = items(K::Function, {
        {"map", "(fn) → observable"}, {"filter", "(pred) → observable"}, {"tap", "(fn) → observable"},
        {"scan", "(seed, fn) → observable"}, {"debounce", "(milliseconds d, executor& ex) → observable"},
        {"throttle", "(milliseconds d, executor& ex) → observable"},
        {"throttle_latest", "(milliseconds d, executor& ex) → observable"}, {"delay", "(milliseconds d, executor& ex) → observable"},
        {"timeout", "(milliseconds d, executor& ex) → observable"}, {"switch_map", "(fn) → observable"},
        {"flat_map", "(fn) → observable"}, {"concat_map", "(fn) → observable"}, {"merge", "(others...) → observable"},
        {"combine_latest", "(others..., fn) → observable"}, {"with_latest_from", "(other, fn) → observable"},
        {"zip", "(other, fn) → observable"}, {"distinct_until_changed", "() → observable"}, {"take", "(size_t n) → observable"},
        {"skip", "(size_t n) → observable"}, {"take_until", "(observable stop) → observable"}, {"first", "() → observable"},
        {"last", "() → observable"}, {"retry", "(size_t times) → observable"}, {"catch_error", "(fn) → observable"},
        {"share", "() → observable"}, {"observe_on", "(executor& ex) → observable"}, {"subscribe_on", "(executor& ex) → observable"},
        {"start_with", "(value) → observable"}, {"buffer", "(size_t n) → observable"}});
    a.scopes["pulse"] = items(K::Type, {
        {"property", "<T> observable value"}, {"computed", "<T> derived value"}, {"signal", "<Args...> event"},
        {"subject", "<T>"}, {"behavior_subject", "<T>"}, {"observable", "<T>"}, {"subscription", ""},
        {"composite_subscription", ""}, {"connection", ""}, {"scoped_connection", ""}, {"topic", "<T>"},
        {"thread_pool", "(size_t threads)"}, {"strand", ""}, {"executor", ""}, {"unit", ""}});
    a.scopes["pulse"].insert(a.scopes["pulse"].end(), a.pulse_ops.begin(), a.pulse_ops.end());
    for (auto& f : std::vector<std::pair<const char*, const char*>>{
             {"timer", "(milliseconds d, executor& ex) → observable<unit>"},
             {"interval", "(milliseconds period, executor& ex) → observable<size_t>"}, {"just", "(values...) → observable"},
             {"from", "(container) → observable"}, {"range", "(int first, int count) → observable<int>"},
             {"empty", "() → observable"}, {"never", "() → observable"}, {"defer", "(fn) → observable"}})
      a.scopes["pulse"].push_back({f.first, f.second, K::Function, {}});
    a.scopes["std"] = items(K::Type, {
        {"string", ""}, {"vector", "<T>"}, {"map", "<K, V>"}, {"unordered_map", "<K, V>"}, {"set", "<T>"},
        {"optional", "<T>"}, {"function", "<R(Args...)>"}, {"unique_ptr", "<T>"}, {"shared_ptr", "<T>"},
        {"pair", "<A, B>"}, {"array", "<T, N>"}, {"string_view", ""}, {"size_t", ""}, {"chrono", "namespace"}});
    for (auto& f : std::vector<std::pair<const char*, const char*>>{
             {"to_string", "(value) → string"}, {"move", "(value)"}, {"make_unique", "<T>(args...) → unique_ptr<T>"},
             {"make_shared", "<T>(args...) → shared_ptr<T>"}, {"min", "(a, b)"}, {"max", "(a, b)"}, {"clamp", "(v, lo, hi)"},
             {"sort", "(first, last)"}, {"find", "(first, last, value)"}, {"find_if", "(first, last, pred)"},
             {"erase_if", "(container, pred) → size_t"}, {"stoi", "(string s) → int"}, {"stod", "(string s) → double"},
             {"abs", "(v)"}, {"swap", "(a, b)"}, {"count", "(first, last, value) → size_t"}, {"any_of", "(first, last, pred) → bool"},
             {"getline", "(stream, string& line) → bool"}})
      a.scopes["std"].push_back({f.first, f.second, K::Function, {}});
    for (auto& v : {"cout", "cerr", "endl"}) a.scopes["std"].push_back({v, "", K::Variable, {}});

    a.globals = items(K::Type, {
        {"App", "event loop"}, {"Window", "top-level window"}, {"VStack", "{...} vertical stack"},
        {"HStack", "{...} horizontal stack"}, {"Panel", "{...} card"}, {"Layer", "{...} overlay"}, {"Scroll", "(Node content)"},
        {"Spacer", "(float size = flexible)"}, {"Divider", "(Orientation o = Horizontal)"}, {"Text", "(Bind<string> text)"},
        {"Button", "(Bind<string> label, function<void()> on_click = {})"}, {"TextField", "(property<string>& model)"},
        {"Checkbox", "(Bind<string> label, property<bool>& model)"}, {"Toggle", "(Bind<string> label, property<bool>& model)"},
        {"Slider", "(property<double>& model, double min = 0, double max = 1)"}, {"ProgressBar", "(Bind<double> fraction)"},
        {"Select", "(Bind<vector<string>> options, property<int>& index)"},
        {"Segmented", "(Bind<vector<string>> options, property<int>& index)"}, {"Tabs", "{Tab(...), ...}"},
        {"Tab", "(string title, Node content)"}, {"Split", "(Node a, Node b, Orientation o, float first = 0)"},
        {"Show", "(Bind<bool> cond, Node then, Node otherwise = {})"}, {"ForEach", "(list, fn(item, index) → Node)"},
        {"Canvas", "(function<void(Painter&, const Rect&)> draw)"}, {"ImageView", "(string path)"},
        {"CodeEditor", "(property<string>& model)"}, {"Node", "any view"}, {"View", "base view"}, {"Bind", "<T> value or stream"},
        {"Model", "<T> two-way property"}, {"Color", ""}, {"Insets", "(all | h, v | l, t, r, b)"}, {"Point", "(x, y)"},
        {"Size", "(w, h)"}, {"Rect", "(x, y, w, h)"}, {"Font", ""}, {"Theme", ""}, {"Painter", ""}, {"Alignment", "enum"},
        {"Justify", "enum"}, {"Orientation", "enum"}, {"ButtonKind", "enum"}, {"FontWeight", "enum"}, {"Align", "flags"},
        {"Cursor", "enum"}, {"Key", "enum"}, {"Mod", "modifiers"}, {"MouseButton", "enum"}, {"InputEvent", ""},
        {"Image", ""}, {"Ui", "designed forms"}, {"dialogs", "namespace"}, {"testing", "namespace"}, {"pulse", "namespace"},
        {"pulseui", "namespace"}, {"std", "namespace"}});
    for (auto& f : std::vector<std::pair<const char*, const char*>>{
             {"theme", "() → property<Theme>&"}, {"th", "() → Theme&"}, {"run", "(string title, float w, float h, Node root) → int"},
             {"load_form", "(string path, string* error = nullptr) → optional<Form>"},
             {"measure_text", "(string s, Font f, float max_width = 0) → Size"}, {"line_height", "(Font f) → float"}})
      a.globals.push_back({f.first, f.second, K::Function, {}});
    a.scopes["pulseui"] = a.globals;

    a.headers = {"pulseui/pulseui.hpp", "pulseui/testing.hpp", "pulse/pulse.hpp", "algorithm", "array", "atomic", "chrono",
                 "cmath", "cstdint", "cstdio", "filesystem", "fstream", "functional", "iomanip", "iostream", "map", "memory",
                 "mutex", "numeric", "optional", "random", "set", "sstream", "string", "string_view", "thread",
                 "unordered_map", "unordered_set", "utility", "variant", "vector"};
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
  int line = 0, col = 0; // 0-based line, byte column
};

bool ident_start(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
bool ident_char(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

std::vector<Tok> tokenize(const std::string& t) {
  std::vector<Tok> out;
  std::size_t i = 0;
  const std::size_t n = t.size();
  bool line_start = true;
  int line = 0;
  std::size_t counted = 0, line_begin = 0;
  auto push = [&](Tok::Kind k, std::size_t at, std::size_t len) {
    for (; counted < at; ++counted)
      if (t[counted] == '\n') {
        ++line;
        line_begin = counted + 1;
      }
    out.push_back({k, t.substr(at, len), line, static_cast<int>(at - line_begin)});
  };
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
      i = e == npos ? n : e + 2;
      continue;
    }
    if (c == '"' || c == '\'') {
      std::size_t j = i + 1;
      while (j < n && t[j] != c && t[j] != '\n') j += t[j] == '\\' ? 2 : 1;
      push(Tok::Str, i, std::min(j + 1, n) - i);
      i = std::min(j + 1, n);
      continue;
    }
    if (ident_start(c)) {
      std::size_t j = i;
      while (j < n && ident_char(t[j])) ++j;
      push(Tok::Ident, i, j - i);
      i = j;
      continue;
    }
    if (std::isdigit(static_cast<unsigned char>(c))) {
      std::size_t j = i;
      while (j < n && (std::isalnum(static_cast<unsigned char>(t[j])) || t[j] == '.' || t[j] == '\'')) ++j;
      push(Tok::Num, i, j - i);
      i = j;
      continue;
    }
    if (i + 1 < n && ((c == ':' && t[i + 1] == ':') || (c == '-' && t[i + 1] == '>') || (c == '&' && t[i + 1] == '&') ||
                      (c == '|' && t[i + 1] == '|'))) {
      push(Tok::Punct, i, 2);
      i += 2;
      continue;
    }
    push(Tok::Punct, i, 1);
    ++i;
  }
  return out;
}

bool is(const std::vector<Tok>& t, std::size_t i, const char* s) { return i < t.size() && t[i].s == s; }
bool is_word_tok(const Tok& t) { return t.kind == Tok::Ident || t.kind == Tok::Num; }

// index of the opener matching the closer at `close`, or npos
std::size_t match_open(const std::vector<Tok>& t, std::size_t close, const char* open, const char* shut) {
  int depth = 0;
  for (std::size_t k = close + 1; k-- > 0;) {
    if (t[k].kind != Tok::Punct) continue;
    if (t[k].s == shut) ++depth;
    else if (t[k].s == open && --depth == 0) return k;
  }
  return npos;
}

// index of the closer matching the opener at `open` (before `limit`), or npos
std::size_t match_close(const std::vector<Tok>& t, std::size_t open, const char* o, const char* c, std::size_t limit) {
  int depth = 0;
  for (std::size_t k = open; k < limit && k < t.size(); ++k) {
    if (t[k].kind != Tok::Punct) continue;
    if (t[k].s == o) ++depth;
    else if (t[k].s == c && --depth == 0) return k;
  }
  return npos;
}

// end of the expression starting at `s`: the first ; , ) ] } at depth 0
std::size_t expr_end(const std::vector<Tok>& t, std::size_t s) {
  int depth = 0;
  for (std::size_t k = s; k < t.size(); ++k) {
    const std::string& x = t[k].s;
    if (t[k].kind != Tok::Punct) continue;
    if (x == "(" || x == "[" || x == "{") ++depth;
    else if (x == ")" || x == "]" || x == "}") {
      if (depth == 0) return k;
      --depth;
    } else if (depth == 0 && (x == ";" || x == ",")) {
      return k;
    }
  }
  return t.size();
}

std::string join(const std::vector<Tok>& t, std::size_t b, std::size_t e) {
  std::string s;
  for (std::size_t k = b; k < e && k < t.size(); ++k) {
    if (k > b) {
      const Tok& p = t[k - 1];
      if ((is_word_tok(p) && is_word_tok(t[k])) || p.s == "," ||
          (t[k].kind == Tok::Ident && (p.s == "&" || p.s == "*" || p.s == "&&")))
        s += ' ';
    }
    s += t[k].s;
  }
  return s;
}

// ── scope scanner ────────────────────────────────────────────────────────────

struct Decl {
  std::string name;
  std::string type;   // full type; for functions the return type
  std::string params; // functions: "(int a, int b)"
  K kind = K::Variable;
  std::vector<Tok> init; // auto x = <init>;  for (auto& x : <init>)
  bool range = false;
  std::string file;      // where it is declared
  int line = -1, col = 0;
  bool is_const = false; // void f() const;
  bool has_body = false; // defined in place: void f() { ... }
  bool no_stub = false;  // = 0 / = default / = delete
};

struct Loc {
  std::string file;
  int line = 0, col = 0; // 0-based
};

struct Scan {
  std::vector<std::vector<Decl>> visible;           // innermost scope first
  std::vector<int> visible_kind;                     // Block::Kind of each visible scope
  std::vector<Decl> pending;                         // declarations in an open ( ... ) at the caret
  std::string current_class;                         // inside a member function / class body
  std::map<std::string, std::vector<Decl>> classes;  // class / enum / namespace -> members
  std::map<std::string, std::vector<std::string>> bases;
  std::vector<Decl> globals;                         // namespace scope declarations
  bool in_function = false;                          // the caret is inside a function body
  bool at_scope_level = true;                        // the caret is at namespace / class scope
  bool using_pulse = false;
  std::set<std::string> locals;                      // names declared inside function bodies
  std::map<std::string, Loc> definitions;            // "Class::method" / "function" -> where its body starts
};

struct Block {
  enum Kind { Global, Namespace, Class, Function, Plain } kind;
  std::string cls;  // class whose members are visible here
  std::vector<Decl> decls;
  std::string name; // namespace / enum name
  bool is_enum = false, scoped_enum = false;
};

// Class::method(...) [: init(...), ...] {  ->  "Class" and the index of "method" (looks back from `from`)
std::pair<std::string, std::size_t> owner_def(const std::vector<Tok>& t, std::size_t from) {
  std::string found;
  std::size_t method = npos;
  std::size_t j = from + 1;
  for (int guard = 0; j-- > 0 && guard < 20000; ++guard) {
    if (t[j].kind != Tok::Punct) continue;
    const std::string& s = t[j].s;
    if (s == ";" || s == "{") break;
    if (s == "}") { // a lambda inside a member initializer is followed by , or )
      if (!(is(t, j + 1, ",") || is(t, j + 1, ")"))) break;
      j = match_open(t, j, "{", "}");
      if (j == npos) break;
      continue;
    }
    if (s == ")" || s == "]") {
      const std::size_t o = match_open(t, j, s == ")" ? "(" : "[", s.c_str());
      if (o == npos) break;
      j = o;
      if (s == ")" && j >= 3 && t[j - 1].kind == Tok::Ident) {
        if (t[j - 2].s == "::") {
          found = t[j - 3].s;
          method = j - 1;
        } else if (t[j - 2].s == "~" && j >= 4 && t[j - 3].s == "::") {
          found = t[j - 4].s;
          method = j - 1;
        }
      }
    }
  }
  return {found, method};
}

std::string owner_class(const std::vector<Tok>& t, std::size_t from) { return owner_def(t, from).first; }

Scan scan(const std::vector<Tok>& t, const std::string& file = {}) {
  std::vector<Block> stack{{Block::Global, {}, {}, {}}};
  std::vector<Decl> pending;
  Scan out;
  int paren = 0;

  auto enclosing_class = [&]() -> std::string {
    for (auto it = stack.rbegin(); it != stack.rend(); ++it)
      if (!it->cls.empty()) return it->cls;
    return {};
  };
  auto close_block = [&](Block& b) {
    if (b.kind == Block::Function || b.kind == Block::Plain)
      for (auto& d : b.decls) out.locals.insert(d.name);
    if (b.kind == Block::Class && !b.cls.empty()) {
      auto& m = out.classes[b.cls];
      m.insert(m.end(), b.decls.begin(), b.decls.end());
    } else if (b.kind == Block::Global || b.kind == Block::Namespace) {
      out.globals.insert(out.globals.end(), b.decls.begin(), b.decls.end());
      if (b.kind == Block::Namespace && !b.name.empty()) {
        auto& m = out.classes[b.name];
        m.insert(m.end(), b.decls.begin(), b.decls.end());
      }
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
        Block b{Block::Plain, enclosing_class(), {}, {}};
        // look back for what opens this block
        std::size_t k = i;
        while (k > 0 && t[k - 1].kind == Tok::Ident &&
               (t[k - 1].s == "const" || t[k - 1].s == "override" || t[k - 1].s == "noexcept" ||
                t[k - 1].s == "mutable" || t[k - 1].s == "final"))
          --k;
        std::string cls_name, ns_name;
        std::size_t cls_pos = npos;
        bool is_enum = false, scoped = false;
        for (std::size_t j = i; j-- > 0;) {
          const std::string& s = t[j].s;
          if (s == ";" || s == "{" || s == "}" || s == ")") break;
          if ((s == "class" || s == "struct" || s == "union") && j + 1 < i && t[j + 1].kind == Tok::Ident) {
            cls_name = t[j + 1].s;
            cls_pos = j + 1;
            if (j > 0 && t[j - 1].s == "enum") is_enum = scoped = true;
          }
          if (s == "enum" && !is_enum) {
            is_enum = true;
            if (j + 1 < i && t[j + 1].kind == Tok::Ident && cls_name.empty()) cls_name = t[j + 1].s;
          }
          if (s == "namespace") ns_name = j + 1 < i && t[j + 1].kind == Tok::Ident ? t[j + 1].s : "_";
        }
        if (is_enum) {
          b.kind = Block::Class;
          b.cls = cls_name;
          b.name = cls_name;
          b.is_enum = true;
          b.scoped_enum = scoped;
        } else if (!cls_name.empty()) {
          b.kind = Block::Class;
          b.cls = cls_name;
          // class X : public Base<...>, Other {
          std::size_t colon = npos;
          for (std::size_t j = cls_pos + 1; j < i; ++j)
            if (t[j].s == ":") { colon = j; break; }
          if (colon != npos) {
            int depth = 0;
            std::string last;
            auto& bases = out.bases[cls_name];
            for (std::size_t j = colon + 1; j <= i; ++j) {
              const std::string& s = t[j].s;
              if (s == "<") ++depth;
              else if (s == ">") --depth;
              else if (depth == 0 && (s == "," || j == i)) {
                if (!last.empty()) bases.push_back(canon(last));
                last.clear();
              } else if (depth == 0 && t[j].kind == Tok::Ident && s != "public" && s != "private" && s != "protected" &&
                         s != "virtual") {
                last = s;
              }
            }
          }
        } else if (!ns_name.empty()) {
          b.kind = Block::Namespace;
          b.name = ns_name == "_" ? "" : ns_name;
        } else if (k > 0 && t[k - 1].s == ")") {
          b.kind = Block::Function;
          b.decls = pending;
          const auto [owner, method] = owner_def(t, k - 1);
          const Block::Kind outer = stack.back().kind;
          if (!owner.empty()) {
            b.cls = owner;
            if (outer == Block::Global || outer == Block::Namespace) // void Class::method() { : a definition
              out.definitions[owner + "::" + t[method].s] = {file, t[method].line, t[method].col};
          } else if (outer == Block::Global || outer == Block::Namespace) { // void helper() {
            const std::size_t open = match_open(t, k - 1, "(", ")");
            if (open != npos && open > 0 && t[open - 1].kind == Tok::Ident && !keywords().count(t[open - 1].s))
              out.definitions[t[open - 1].s] = {file, t[open - 1].line, t[open - 1].col};
          }
        } else if (b.cls.empty()) { // e.g. a lambda inside a constructor's member initializer
          const std::string owner = owner_class(t, i - (i > 0 ? 1 : 0));
          if (!owner.empty()) b.cls = owner;
        }
        if (b.kind == Block::Plain && k > 0 && t[k - 1].s == "]") b.decls = pending; // [captures] { ... }
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
      // class / struct / enum names are declarations of the enclosing scope
      if ((tk.s == "class" || tk.s == "struct" || tk.s == "enum") && i + 1 < t.size() && t[i + 1].kind == Tok::Ident &&
          !keywords().count(t[i + 1].s) &&
          (is(t, i + 2, "{") || is(t, i + 2, ":") || is(t, i + 2, ";") || is(t, i + 2, "final")))
      {
        Decl d{t[i + 1].s, t[i + 1].s, {}, K::Type, {}, false};
        d.file = file;
        d.line = t[i + 1].line;
        d.col = t[i + 1].col;
        stack.back().decls.push_back(d);
      }
      if (tk.s == "using" && is(t, i + 1, "namespace") && is(t, i + 2, "pulse")) out.using_pulse = true;
      continue;
    }
    // enumerators: enum class Mode { Edit, View = 2 };
    if (stack.back().is_enum && paren == 0 && i > 0 && (t[i - 1].s == "{" || t[i - 1].s == ",")) {
      Decl d{tk.s, stack.back().name, {}, K::Value, {}, false};
      d.file = file;
      d.line = tk.line;
      d.col = tk.col;
      stack.back().decls.push_back(d);
      if (!stack.back().scoped_enum && stack.size() >= 2) stack[stack.size() - 2].decls.push_back(d);
      continue;
    }
    // declaration: <type> name followed by = ; , ( ) { [ :
    if (i == 0 || i + 1 >= t.size()) continue;
    const std::string& next = t[i + 1].s;
    if (!(next == "=" || next == ";" || next == "," || next == "(" || next == ")" || next == "{" || next == "[" || next == ":"))
      continue;
    std::size_t p = i - 1;
    int stars = 0;
    while (p > 0 && t[p].kind == Tok::Punct && (t[p].s == "*" || t[p].s == "&" || t[p].s == "&&")) {
      if (t[p].s == "*") ++stars;
      --p;
    }
    std::string base, type;
    if (t[p].s == ">") {
      const std::size_t lt = match_open(t, p, "<", ">");
      if (lt == npos || lt == 0 || t[lt - 1].kind != Tok::Ident) continue;
      base = t[lt - 1].s;
      type = join(t, lt - 1, p + 1);
    } else if (t[p].kind == Tok::Ident && type_word(t[p].s) && !is(t, p - 1, ".") && !is(t, p - 1, "->")) {
      base = t[p].s;
      type = base;
      if (p >= 2 && is(t, p - 1, "::") && t[p - 2].s == "Ui") type = "Ui::" + base; // Ui::MainWindow ui_;
    } else {
      continue;
    }
    if (base == "const" || base == "static" || base == "constexpr" || base == "inline" || base == "virtual") continue;
    type += std::string(static_cast<std::size_t>(stars), '*');
    Decl d{tk.s, type, {}, K::Variable, {}, false};
    d.file = file;
    d.line = tk.line;
    d.col = tk.col;
    if (base == "auto") {
      if (next == "=") {
        d.init.assign(t.begin() + static_cast<std::ptrdiff_t>(i + 2), t.begin() + static_cast<std::ptrdiff_t>(expr_end(t, i + 2)));
      } else if (next == ":" && paren > 0) { // for (auto& x : items)
        d.range = true;
        d.init.assign(t.begin() + static_cast<std::ptrdiff_t>(i + 2), t.begin() + static_cast<std::ptrdiff_t>(expr_end(t, i + 2)));
      }
    } else if (next == ":" && paren > 0) {
      // for (const Task& x : items): the type is already known
    }
    const Block::Kind bk = stack.back().kind;
    const bool scope_level = bk == Block::Global || bk == Block::Namespace || bk == Block::Class;
    if (paren > 0) {
      pending.push_back(d); // parameters, for/if/lambda headers -> the next block
    } else {
      if (next == "(" && scope_level) {
        const std::size_t close = match_close(t, i + 1, "(", ")", t.size());
        const bool ctor_call = close != npos && close > i + 2 && (t[i + 2].kind == Tok::Str || t[i + 2].kind == Tok::Num);
        if (!ctor_call) {
          d.kind = K::Function;
          d.params = close == npos ? "()" : "(" + join(t, i + 2, close) + ")";
          if (close != npos) { // what follows the parameters: const, a body, = 0 ...
            std::size_t q = close + 1;
            for (; q < t.size(); ++q) {
              const std::string& w = t[q].s;
              if (w == "const") d.is_const = true;
              else if (w != "override" && w != "noexcept" && w != "final" && w != "&" && w != "&&" && w != "volatile") break;
            }
            if (is(t, q, "{") || is(t, q, ":")) d.has_body = true;
            if (is(t, q, "=")) d.no_stub = true;
          }
        } else if (bk == Block::Class) {
          d.kind = K::Member;
        }
      } else if (bk == Block::Class) {
        d.kind = K::Member;
      }
      stack.back().decls.push_back(d);
    }
  }

  // what is visible at the end of the scanned text (= the caret)
  for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
    out.visible.push_back(it->decls);
    out.visible_kind.push_back(static_cast<int>(it->kind));
  }
  out.pending = pending;
  out.current_class = enclosing_class();
  out.in_function = std::any_of(stack.begin(), stack.end(), [](const Block& b) { return b.kind == Block::Function; });
  out.at_scope_level = stack.back().kind != Block::Function && stack.back().kind != Block::Plain;
  for (auto it = stack.rbegin(); it != stack.rend(); ++it) close_block(*it);
  return out;
}

// Files are rescanned only when their text changes.
const Scan& scan_file(const std::string& name, const std::string& text, const std::string& file = {}) {
  struct Entry {
    std::size_t hash;
    Scan scan;
  };
  static std::unordered_map<std::string, Entry> cache;
  const std::size_t h = std::hash<std::string>{}(text);
  auto it = cache.find(name);
  if (it == cache.end() || it->second.hash != h) {
    if (cache.size() > 256) cache.clear();
    it = cache.insert_or_assign(name, Entry{h, scan(tokenize(text), file.empty() ? name : file)}).first;
  }
  return it->second.scan;
}

// ── assembling results ───────────────────────────────────────────────────────

std::string lower(std::string s) {
  for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

// "sms" matches "set_min_size": every character in order, the first one at the start
bool subsequence(const std::string& l, const std::string& p) {
  if (p.empty() || l.empty() || l[0] != p[0]) return false;
  std::size_t j = 0;
  for (char c : l)
    if (j < p.size() && c == p[j]) ++j;
  return j == p.size();
}

struct Collector {
  std::string prefix, lprefix;
  std::vector<std::pair<int, CompletionItem>> ranked;
  std::set<std::string> seen;
  void add(const CompletionItem& it, int rank) {
    if (it.label.empty() || seen.count(it.label)) return;
    const std::string l = lower(it.label);
    int score;
    if (it.label.rfind(prefix, 0) == 0) score = 0;                                    // exact-case prefix
    else if (l.rfind(lprefix, 0) == 0) score = 1;                                     // prefix ignoring case
    else if (prefix.size() >= 2 && l.find("_" + lprefix) != npos) score = 2;          // a word inside: "size" -> set_min_size
    else if (prefix.size() >= 3 && l.find(lprefix) != npos) score = 3;                // contains
    else if (prefix.size() >= 2 && subsequence(l, lprefix)) score = 4;                // abbreviation
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

std::string view_type(const FormView& v) { return v.type == "Image" ? "ImageView" : v.type; }

// ── type resolution at the caret ─────────────────────────────────────────────

struct Resolver {
  const Scan& here;
  const CompletionContext& cc;
  std::map<std::string, std::vector<Decl>> classes;
  std::map<std::string, std::vector<std::string>> bases;
  std::vector<Decl> globals;
  std::map<std::string, Loc> definitions;
  mutable int depth = 0;

  bool is_form(const std::string& name) const {
    return std::find(cc.form_classes.begin(), cc.form_classes.end(), name) != cc.form_classes.end();
  }

  // the member and the class that declares it (maybe a base class)
  std::pair<const Decl*, std::string> class_member_in(const std::string& cls, const std::string& name, int d = 0) const {
    if (d > 8) return {nullptr, {}};
    if (auto it = classes.find(cls); it != classes.end())
      for (auto& m : it->second)
        if (m.name == name && m.name != cls) return {&m, cls};
    if (auto b = bases.find(cls); b != bases.end())
      for (auto& base : b->second)
        if (auto r = class_member_in(base, name, d + 1); r.first) return r;
    return {nullptr, {}};
  }
  const Decl* class_member(const std::string& cls, const std::string& name, int d = 0) const {
    return class_member_in(cls, name, d).first;
  }

  // the PulseUI / std type a user class derives from (class MyView : public ViewT<MyView> -> View)
  std::string api_base(const std::string& cls, int d = 0) const {
    if (d > 8) return {};
    if (auto b = bases.find(cls); b != bases.end())
      for (auto& base : b->second) {
        if (api().members.count(base)) return base;
        if (auto r = api_base(base, d + 1); !r.empty()) return r;
      }
    return {};
  }

  const Decl* find_var(const std::string& name) const {
    for (auto it = here.pending.rbegin(); it != here.pending.rend(); ++it)
      if (it->name == name) return &*it;
    for (auto& scope : here.visible)
      for (auto it = scope.rbegin(); it != scope.rend(); ++it)
        if (it->name == name) return &*it;
    if (!here.current_class.empty())
      if (auto* m = class_member(here.current_class, name)) return m;
    for (auto& d : globals)
      if (d.name == name) return &d;
    return nullptr;
  }

  std::string decl_type(const Decl& d) const {
    if (!d.init.empty() && depth < 8) {
      ++depth;
      Ty r = eval(d.init, 0, d.init.size());
      --depth;
      if (d.range) r = parse_ty(element_of(r, true));
      if (!r.empty()) return ty_str(r);
    }
    return d.type;
  }

  static std::string element_of(const Ty& t, bool for_range) {
    static const std::set<std::string> seq = {"vector", "array", "deque", "list", "set", "unordered_set", "span",
                                              "initializer_list", "observable"};
    if (t.ptr > 0 && !for_range) {
      Ty e = t;
      --e.ptr;
      return ty_str(e);
    }
    if (seq.count(t.base) && !t.args.empty()) return t.args[0];
    if ((t.base == "map" || t.base == "unordered_map") && t.args.size() >= 2)
      return for_range ? "pair<" + t.args[0] + ", " + t.args[1] + ">" : t.args[1];
    if (t.base == "string") return "char";
    return {};
  }

  static Ty arrow(Ty t) { // what `->` reaches
    if (t.ptr > 0) {
      --t.ptr;
      return t;
    }
    if ((t.base == "unique_ptr" || t.base == "shared_ptr" || t.base == "optional" || t.base == "Node") && !t.args.empty())
      return parse_ty(t.args[0]);
    if (t.base == "Node") return parse_ty("View");
    return t;
  }

  // Members of a type, with ranks (lower first).
  void members(const Ty& owner, const std::function<void(const CompletionItem&, int)>& add, int d = 0) const {
    if (d > 8 || owner.empty()) return;
    const std::string& b = owner.base;
    if (starts_with(b, "Ui::")) {
      const std::string form = b.substr(4);
      for (auto& v : cc.views)
        if (v.form == form) add({v.id, view_type(v) + "*", K::Member, {}}, 0);
      add({"setup", "(Window& window, Model& model) → void", K::Function, {}}, 2);
      add({"build", "(Model& model) → Node", K::Function, {}}, 2);
      for (auto* n : {"form_title", "form_width", "form_height"}) add({n, "constexpr", K::Member, {}}, 3);
      return;
    }
    if (auto it = classes.find(b); it != classes.end())
      for (auto& m : it->second)
        if (m.name != b && m.kind != K::Type) add(item(m), 0);
    if (auto bs = bases.find(b); bs != bases.end())
      for (auto& base : bs->second) members(parse_ty(base), add, d + 1);
    if (auto it = api().members.find(b); it != api().members.end()) {
      for (auto& m : it->second) add(m, d == 0 ? 0 : 1);
      if (api().views.count(b))
        for (auto& m : api().view_common) add(m, 2);
    }
  }

  CompletionItem item(const Decl& d) const {
    std::string detail;
    if (d.kind == K::Function) {
      detail = d.params;
      const std::string r = d.type;
      if (!r.empty() && r != "void") detail += " " + kArrow + " " + r;
    } else if (d.kind == K::Type) {
      detail = classes.count(d.name) ? "class" : "type";
    } else {
      detail = d.type;
      if (starts_with(detail, "auto") && !d.init.empty()) {
        const std::string r = decl_type(d);
        detail = starts_with(r, "auto") ? r : "auto " + kArrow + " " + r;
      }
    }
    return {d.name, detail, d.kind, {}};
  }

  // Type of `owner.name` (or of calling it); `targ` is an explicit template argument: find<Button>
  std::string member_type(const Ty& owner, const std::string& name, const std::string& targ, int d = 0) const {
    if (d > 8 || owner.empty()) return {};
    const std::string& b = owner.base;
    if (starts_with(b, "Ui::")) {
      for (auto& v : cc.views)
        if (v.form == b.substr(4) && v.id == name) return view_type(v) + "*";
      if (name == "build") return "Node";
      if (name == "form_title") return "const char*";
      if (name == "form_width" || name == "form_height") return "float";
      return {};
    }
    if (auto* m = class_member(b, name)) return decl_type(*m);
    std::vector<std::string> api_types{b};
    if (auto ab = api_base(b); !ab.empty()) api_types.push_back(ab);
    for (auto& at : api_types) {
      auto it = api().members.find(at);
      if (it == api().members.end()) continue;
      const bool view = api().views.count(at) > 0;
      auto from = [&](const CompletionItem& m) -> std::string {
        std::string r = result_of(m.detail);
        if (r.empty() && view && is_function_detail(m.detail) && !starts_with(m.label, "set_")) r = "Self"; // a modifier
        return subst(r, at == b ? owner : parse_ty(b), targ);
      };
      for (auto& m : it->second)
        if (m.label == name) return from(m);
      if (view)
        for (auto& m : api().view_common)
          if (m.label == name) return from(m);
    }
    return {};
  }

  const CompletionItem* api_item(const std::vector<CompletionItem>& list, const std::string& name) const {
    for (auto& m : list)
      if (m.label == name) return &m;
    return nullptr;
  }

  bool known_type(const std::string& name) const {
    if (api().members.count(canon(name)) || classes.count(name)) return true;
    for (auto& g : api().globals)
      if (g.label == name && g.kind == K::Type) return true;
    for (auto& g : api().scopes.at("std"))
      if (g.label == name && g.kind == K::Type) return true;
    return false;
  }

  // Type of the postfix expression t[b, e): a, a.b(), a->b[0], Type(...), find<T>(...), Ui::X::y, ...
  Ty eval(const std::vector<Tok>& t, std::size_t b, std::size_t e) const {
    std::size_t i = b;
    int derefs = 0, addr = 0;
    while (i < e && t[i].kind == Tok::Punct && (t[i].s == "*" || t[i].s == "&" || t[i].s == "!" || t[i].s == "-")) {
      if (t[i].s == "*") ++derefs;
      if (t[i].s == "&") ++addr;
      ++i;
    }
    if (i >= e) return {};
    Ty cur;
    if (t[i].s == "(") { // (expr)
      const std::size_t c = match_close(t, i, "(", ")", e);
      if (c == npos) return {};
      cur = eval(t, i + 1, c);
      i = c + 1;
    } else if (t[i].kind == Tok::Str) {
      cur = parse_ty("const char*");
      ++i;
    } else if (t[i].kind == Tok::Num) {
      cur = parse_ty(t[i].s.find('.') != npos ? "double" : "int");
      ++i;
    } else if (t[i].kind == Tok::Ident) {
      if (t[i].s == "new" && i + 1 < e) {
        ++i;
        while (is(t, i + 1, "::") && i + 2 < e) i += 2;
        cur = parse_ty(t[i].s + "*");
        ++i;
        if (is(t, i, "(") || is(t, i, "{")) {
          const std::size_t c = match_close(t, i, t[i].s.c_str(), t[i].s == "(" ? ")" : "}", e);
          i = c == npos ? e : c + 1;
        }
      } else {
        std::vector<std::string> parts{t[i].s};
        if (parts[0] == "std" || parts[0] == "pulse" || parts[0] == "pulseui") parts.clear();
        while (is(t, i + 1, "::") && i + 2 < e && t[i + 2].kind == Tok::Ident) {
          i += 2;
          parts.push_back(t[i].s);
        }
        if (parts.empty()) parts.push_back(t[i].s);
        std::size_t j = i;
        std::string targ;
        if (is(t, j + 1, "<")) {
          const std::size_t c = match_close(t, j + 1, "<", ">", e);
          if (c != npos && (is(t, c + 1, "(") || is(t, c + 1, "{") || is(t, c + 1, "::") || c + 1 >= e)) {
            targ = join(t, j + 2, c);
            j = c;
          }
        }
        bool called = false;
        if (is(t, j + 1, "(") || is(t, j + 1, "{")) {
          const std::size_t c = match_close(t, j + 1, t[j + 1].s.c_str(), t[j + 1].s == "(" ? ")" : "}", e);
          called = true;
          j = c == npos ? e : c;
        }
        cur = primary(parts, targ, called);
        i = j + 1;
      }
    } else {
      return {};
    }
    // postfix
    while (i < e && !cur.empty()) {
      const std::string& s = t[i].s;
      if ((s == "." || s == "->") && i + 1 < e && t[i + 1].kind == Tok::Ident) {
        const Ty owner = s == "->" ? arrow(cur) : Ty{cur.base, cur.args, 0};
        std::size_t j = i + 1;
        std::string targ;
        if (is(t, j + 1, "<")) {
          const std::size_t c = match_close(t, j + 1, "<", ">", e);
          if (c != npos && is(t, c + 1, "(")) {
            targ = join(t, j + 2, c);
            j = c;
          }
        }
        if (is(t, j + 1, "(")) {
          const std::size_t c = match_close(t, j + 1, "(", ")", e);
          j = c == npos ? e : c;
        }
        cur = parse_ty(member_type(owner, t[i + 1].s, targ));
        i = j + 1;
      } else if (s == "[") {
        const std::size_t c = match_close(t, i, "[", "]", e);
        cur = parse_ty(element_of(cur, false));
        i = c == npos ? e : c + 1;
      } else if (s == "(") { // calling a std::function / callable
        const std::size_t c = match_close(t, i, "(", ")", e);
        if (cur.base == "function" && !cur.args.empty()) {
          const std::string sig = cur.args[0];
          cur = parse_ty(sig.substr(0, sig.find('(')));
        }
        i = c == npos ? e : c + 1;
      } else {
        break;
      }
    }
    for (; derefs > 0 && !cur.empty(); --derefs) cur = arrow(cur);
    cur.ptr += addr;
    return cur;
  }

  Ty primary(const std::vector<std::string>& parts, const std::string& targ, bool called) const {
    const std::string& name = parts.back();
    if (parts.size() == 1) {
      if (name == "this") return Ty{here.current_class, {}, 1};
      if (auto* d = find_var(name)) {
        Ty r = parse_ty(decl_type(*d));
        if (d->kind == K::Type) r = Ty{name, {}, 0};
        return r;
      }
      if ((name == "static_cast" || name == "dynamic_cast" || name == "reinterpret_cast" || name == "const_cast") && !targ.empty())
        return parse_ty(targ);
      if ((name == "find" || name == "get") && !targ.empty()) return parse_ty(name == "find" ? targ + "*" : targ);
      if (name == "make_unique" || name == "make_shared")
        return Ty{name == "make_unique" ? "unique_ptr" : "shared_ptr", {targ}, 0};
      if (called && known_type(name)) {
        Ty r = parse_ty(name);
        if (!targ.empty()) r.args = split_top(targ, ',');
        return r;
      }
      if (auto* g = api_item(api().globals, name)) return parse_ty(result_of(g->detail));
      if (known_type(name)) return parse_ty(targ.empty() ? name : name + "<" + targ + ">");
      return {};
    }
    // qualified: Scope::name
    if (parts[0] == "Ui") {
      if (parts.size() == 2) return Ty{"Ui::" + parts[1], {}, 0};
      return parse_ty(member_type(Ty{"Ui::" + parts[1], {}, 0}, parts[2], targ));
    }
    const std::string& scope = parts[parts.size() - 2];
    if (auto it = api().scopes.find(scope); it != api().scopes.end())
      if (auto* m = api_item(it->second, name)) {
        if (m->kind == K::Value) return Ty{scope, {}, 0};
        if (m->kind == K::Type) return parse_ty(name);
        if (scope == "std" && (name == "make_unique" || name == "make_shared"))
          return Ty{name == "make_unique" ? "unique_ptr" : "shared_ptr", {targ}, 0};
        return parse_ty(subst(result_of(m->detail), Ty{}, targ));
      }
    if (auto* m = class_member(scope, name)) {
      if (m->kind == K::Value) return Ty{scope, {}, 0};
      if (m->kind == K::Type) return Ty{name, {}, 0};
      return parse_ty(decl_type(*m));
    }
    if (called || known_type(name)) return parse_ty(name);
    return {};
  }

  // First token of the postfix expression that ends at t[last].
  static std::size_t expr_start(const std::vector<Tok>& t, std::size_t last) {
    std::size_t i = last;
    for (int guard = 0; guard < 4000; ++guard) {
      const std::string& s = t[i].s;
      if (t[i].kind == Tok::Punct && (s == ")" || s == "]" || s == "}")) {
        const std::size_t o = match_open(t, i, s == ")" ? "(" : (s == "]" ? "[" : "{"), s.c_str());
        if (o == npos) return npos;
        i = o;
        if (i == 0) return 0;
        const Tok& p = t[i - 1];
        if (s != "]" && !(p.kind == Tok::Ident || p.s == ">" || p.s == ")" || p.s == "]")) return i; // (expr) / {init}
        if (s == "]" && !(p.kind == Tok::Ident || p.s == ")" || p.s == "]")) return npos;
        if (p.kind == Tok::Ident && keywords().count(p.s) && p.s != "this") return i; // return (x)
        --i;
        continue;
      }
      if (t[i].kind == Tok::Punct && s == ">") { // find<Button>
        const std::size_t o = match_open(t, i, "<", ">");
        if (o == npos || o == 0 || t[o - 1].kind != Tok::Ident) return npos;
        i = o - 1;
        continue;
      }
      if (t[i].kind == Tok::Ident || t[i].kind == Tok::Str || t[i].kind == Tok::Num) {
        if (i >= 2 && (t[i - 1].s == "." || t[i - 1].s == "->" || t[i - 1].s == "::")) {
          i -= 2;
          continue;
        }
        return i;
      }
      return npos;
    }
    return npos;
  }

  // ── parameter hints ──
  struct Signature {
    std::string name, detail;
  };

  std::optional<Signature> callee(const std::vector<Tok>& t, std::size_t open) const {
    if (open == 0) return std::nullopt;
    std::size_t n = open - 1;
    if (t[n].s == ">") { // find<Button>(
      const std::size_t o = match_open(t, n, "<", ">");
      if (o == npos || o == 0) return std::nullopt;
      n = o - 1;
    }
    if (t[n].kind != Tok::Ident) return std::nullopt;
    const std::string& name = t[n].s;
    auto from_list = [&](const std::vector<CompletionItem>& list) -> std::optional<Signature> {
      for (auto& m : list)
        if (m.label == name && is_function_detail(m.detail)) return Signature{name, m.detail};
      return std::nullopt;
    };
    auto from_decl = [&](const Decl* d) -> std::optional<Signature> {
      if (d && d->kind == K::Function) return Signature{name, d->params};
      return std::nullopt;
    };
    if (n >= 2 && (t[n - 1].s == "." || t[n - 1].s == "->")) {
      const std::size_t s = expr_start(t, n - 2);
      if (s == npos) return std::nullopt;
      Ty owner = eval(t, s, n - 1);
      if (t[n - 1].s == "->") owner = arrow(owner);
      else owner.ptr = 0;
      if (starts_with(owner.base, "Ui::")) return std::nullopt;
      if (auto r = from_decl(class_member(owner.base, name))) return r;
      for (auto& at : {owner.base, api_base(owner.base)}) {
        if (auto it = api().members.find(at); it != api().members.end()) {
          if (auto r = from_list(it->second)) return r;
          if (api().views.count(at))
            if (auto r = from_list(api().view_common)) return r;
        }
      }
      return std::nullopt;
    }
    if (n >= 2 && t[n - 1].s == "::" && t[n - 2].kind == Tok::Ident) {
      const std::string& scope = t[n - 2].s;
      if (auto it = api().scopes.find(scope); it != api().scopes.end())
        if (auto r = from_list(it->second)) return r;
      if (auto r = from_decl(class_member(scope, name))) return r;
      return std::nullopt;
    }
    if (auto r = from_decl(find_var(name))) return r;
    if (auto r = from_list(api().globals)) return r;
    if (here.using_pulse)
      if (auto r = from_list(api().scopes.at("pulse"))) return r;
    if (auto r = from_list(api().pulse_ops)) return r;
    return std::nullopt;
  }
};

// Everything known at a position of the current file: its tokens up to there,
// the scopes open there, and the classes / globals / definitions of the project.
struct Analysis {
  std::string text; // the current file up to the position
  std::vector<Tok> toks;
  Scan here;
  std::string whole;
  const Scan* full = nullptr;
  std::unique_ptr<Resolver> res;

  Analysis(const std::vector<std::string>& lines, std::size_t line, std::size_t col, const CompletionContext& ctx) {
    for (std::size_t i = 0; i < line && i < lines.size(); ++i) text += lines[i] + '\n';
    if (line < lines.size()) text += lines[line].substr(0, std::min(col, lines[line].size()));
    toks = tokenize(text);
    here = scan(toks, ctx.current_file);
    for (auto& l : lines) whole += l + '\n';
    full = &scan_file("\x01" + ctx.current_file, whole, ctx.current_file); // the current file, also after the caret
    res = std::make_unique<Resolver>(Resolver{here, ctx, full->classes, full->bases, full->globals});
    res->definitions = full->definitions;
    for (auto& [name, body] : ctx.other_files) {
      if (name == ctx.current_file) continue;
      const Scan& s = scan_file(name, body);
      for (auto& [k, v] : s.classes) res->classes[k].insert(res->classes[k].end(), v.begin(), v.end());
      for (auto& [k, v] : s.bases) res->bases[k].insert(res->bases[k].end(), v.begin(), v.end());
      res->globals.insert(res->globals.end(), s.globals.begin(), s.globals.end());
      for (auto& [k, v] : s.definitions) res->definitions.emplace(k, v);
    }
  }
  Analysis(const Analysis&) = delete;
  Analysis& operator=(const Analysis&) = delete;
};

// insert text for an item: function calls get parentheses, the caret goes inside when it takes arguments
std::string call_insert(const CompletionItem& it) {
  if (it.kind != K::Function && it.kind != K::Member) return {};
  const std::string& d = it.detail;
  if (!is_function_detail(d)) return {};
  if (d[0] == '<') return it.label + "<$0>()";
  if (starts_with(d, "()")) return it.label + "()";
  return it.label + "($0)";
}

std::string indent_of(const std::string& line) { return line.substr(0, line.find_first_not_of(" \t") == npos ? line.size() : line.find_first_not_of(" \t")); }

void add_snippets(Collector& c, const std::string& ind, bool statement, bool in_function, bool expression) {
  const std::string in = ind + "    ";
  auto snip = [&](const char* label, const char* detail, std::string text, int rank) {
    c.add({label, detail, K::Keyword, std::move(text)}, rank);
  };
  if (statement) {
    snip("if", "if (…) { }", "if ($0) {\n" + in + "\n" + ind + "}", 1);
    snip("else", "else { }", "else {\n" + in + "$0\n" + ind + "}", 1);
    snip("for", "for (int i = 0; i < …; ++i)", "for (int i = 0; i < $0; ++i) {\n" + in + "\n" + ind + "}", 1);
    snip("foreach", "for (auto& x : …)", "for (auto& item : $0) {\n" + in + "\n" + ind + "}", 1);
    snip("while", "while (…) { }", "while ($0) {\n" + in + "\n" + ind + "}", 1);
    snip("switch", "switch (…) { case … }", "switch ($0) {\n" + ind + "case 0:\n" + in + "break;\n" + ind + "}", 1);
    snip("return", "", "return $0;", 1);
  }
  if (in_function) {
    snip("lambda", "[this] { … }", "[this] {$0}", 4);
    snip("after", "App::after(ms, fn)", "App::after(std::chrono::milliseconds($0), [this] {})", 6);
  }
  if (expression) { // building views: VStack{ ... }, Button("...", [this] { ... })
    snip("VStack", "{ … } vertical stack", "VStack{\n" + in + "$0\n" + ind + "}", 1);
    snip("HStack", "{ … } horizontal stack", "HStack{\n" + in + "$0\n" + ind + "}", 1);
    snip("Panel", "{ … } card", "Panel{\n" + in + "$0\n" + ind + "}", 1);
    snip("Button", "(label, [this] { … })", "Button(\"$0\", [this] {})", 1);
    snip("Text", "(\"…\")", "Text(\"$0\")", 1);
  }
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

  // #include "..." / <...>
  static const std::regex include_re(R"(^\s*#\s*include\s*([<"])([\w./\-]*)$)");
  std::smatch m;
  if (std::regex_search(before, m, include_re)) {
    const bool quoted = m[1] == "\"";
    const std::string typed = m[2];
    r.replace_from = col - typed.size();
    c.prefix = typed;
    c.lprefix = lower(typed);
    const char close = quoted ? '"' : '>';
    const bool closed = col < cur.size() && cur[col] == close;
    auto add = [&](const std::string& h, const char* detail, int rank) {
      c.add({h, detail, K::Value, closed ? std::string() : h + close}, rank);
    };
    if (quoted)
      for (auto& h : ctx.headers) add(h, "project", 0);
    for (auto& h : api().headers) add(h, starts_with(h, "pulse") ? "library" : "standard", quoted ? 10 : 0);
    r.items = c.result();
    return r;
  }

  // in a comment?
  if (const std::size_t q = before.find("//"); q != npos)
    if (std::count(before.begin(), before.begin() + static_cast<std::ptrdiff_t>(q), '"') % 2 == 0) return r;

  // ids of the designed views: find<Button>("o|  get<Button>("o|
  static const std::regex find_re(R"((?:find|get)\s*<\s*(?:\w+\s*::\s*)?(\w+)\s*>\s*\(\s*"([\w\-]*)$)");
  if (std::regex_search(before, m, find_re)) {
    const std::string type = m[1];
    r.replace_from = col - m[2].length();
    c.prefix = m[2];
    c.lprefix = lower(c.prefix);
    for (auto& v : ctx.views)
      if (type == "View" || type == view_type(v) || type == v.type) c.add({v.id, view_type(v) + " · " + v.form, K::Value, {}}, 0);
    for (auto& v : ctx.views) // other types after the matching ones
      c.add({v.id, view_type(v) + " · " + v.form, K::Value, {}}, 50);
    r.items = c.result();
    return r;
  }
  const bool in_string = std::count(before.begin(), before.end(), '"') % 2 == 1;

  // scan the current file up to the caret, and the whole project
  std::string text;
  for (std::size_t i = 0; i < line; ++i) text += lines[i] + '\n';
  const Analysis an(lines, line, word, ctx);
  const std::vector<Tok>& toks = an.toks;
  const Scan& here = an.here;
  const Scan& full = *an.full;
  const Resolver& res = *an.res;
  const std::string& whole = an.whole;

  // ── parameter hint: the innermost open '(' before the caret ──
  if (!in_string) {
    const std::vector<Tok> all = prefix.empty() ? toks : tokenize(text + before);
    int depth = 0, commas = 0;
    for (std::size_t k = all.size(); k-- > 0;) {
      const std::string& s = all[k].s;
      if (all[k].kind != Tok::Punct) continue;
      if (s == ")" || s == "]" || s == "}") ++depth;
      else if (s == "[" || s == "{") {
        if (depth == 0) break;
        --depth;
      } else if (s == "(") {
        if (depth > 0) { --depth; continue; }
        if (auto sig = res.callee(all, k)) {
          std::string d = sig->detail;
          // the parameter list: up to the ')' matching the first '('
          const std::size_t po = d.find('(');
          std::size_t pc = po;
          for (int dd = 0; pc < d.size(); ++pc) {
            if (d[pc] == '(') ++dd;
            if (d[pc] == ')' && --dd == 0) break;
          }
          const std::string params = po == npos || pc >= d.size() ? std::string() : d.substr(po + 1, pc - po - 1);
          const std::string tail = pc < d.size() ? d.substr(pc + 1) : std::string();
          r.signature = sig->name + d.substr(0, po) + "(" + params + ")" + tail;
          const std::size_t base = sig->name.size() + po + 1;
          std::size_t off = 0;
          const auto list = split_top(params, ',');
          for (std::size_t a = 0; a < list.size(); ++a) {
            const std::size_t at = params.find(list[a], off);
            if (at == npos) break;
            const bool variadic = list[a].find("...") != npos;
            if (static_cast<int>(a) == commas || (variadic && static_cast<int>(a) < commas) ||
                (a + 1 == list.size() && variadic)) {
              r.active_begin = base + at;
              r.active_end = base + at + list[a].size();
              break;
            }
            off = at + list[a].size();
          }
        }
        break;
      } else if (depth == 0 && s == ",") {
        ++commas;
      } else if (depth == 0 && s == ";") {
        break;
      }
    }
  }
  if (in_string) return r;
  if (!prefix.empty() && std::isdigit(static_cast<unsigned char>(prefix[0]))) return r;

  // what comes right after the caret decides whether a call gets "()"
  std::size_t after = col;
  while (after < cur.size() && ident_char(cur[after])) ++after;
  const bool paren_follows = after < cur.size() && cur[after] == '(';
  auto add_item = [&](CompletionItem it, int rank, bool calls = true) {
    if (calls && !paren_follows && it.insert.empty()) it.insert = call_insert(it);
    c.add(it, rank);
  };

  const std::size_t n = toks.size();
  const std::string last = n ? toks[n - 1].s : std::string();
  const bool address_of = n >= 3 && toks[n - 1].s == "::" && (toks[n - 3].s == "&" || (n >= 5 && toks[n - 5].s == "&"));

  if (n && (last == "." || last == "->")) { // obj. / obj-> / this-> / a.b().c[0].
    if (n < 2) return r;
    const std::size_t s = Resolver::expr_start(toks, n - 2);
    if (s == npos) return r;
    Ty owner = res.eval(toks, s, n - 1);
    owner = last == "->" ? Resolver::arrow(owner) : Ty{owner.base, owner.args, 0};
    res.members(owner, [&](const CompletionItem& it, int rank) { add_item(it, rank); });
  } else if (n && last == "::") { // Type:: / namespace:: / Ui::Form::
    std::vector<std::string> parts;
    for (std::size_t k = n - 1; k >= 1 && toks[k].s == "::" && toks[k - 1].kind == Tok::Ident;) {
      parts.insert(parts.begin(), toks[k - 1].s);
      if (k < 3) break;
      k -= 2;
    }
    if (parts.empty()) return r;
    const std::string scope = parts.back();
    const bool definition = here.at_scope_level && !here.in_function; // void MainWindow::|
    if (parts.size() >= 2 && parts[parts.size() - 2] == "Ui") {
      for (auto* f : {"form_title", "form_width", "form_height"}) c.add({f, "constexpr", K::Member, {}}, 0);
      add_item({"build", "() → Node (layout only)", K::Function, {}}, 1);
    } else if (scope == "Ui") {
      for (auto& f : ctx.form_classes) c.add({f, "form", K::Type, {}}, 0);
    } else if (auto it = res.classes.find(scope); it != res.classes.end()) {
      if (definition) {
        // out-of-class definitions: the constructor and the declared methods with their parameters
        c.add({scope, "constructor", K::Function, scope + "()"}, 0);
        c.add({"~" + scope, "destructor", K::Function, "~" + scope + "()"}, 5);
        for (auto& d : it->second)
          if (d.kind == K::Function) c.add({d.name, res.item(d).detail, K::Function, d.name + d.params}, 1);
      }
      for (auto& d : it->second) add_item(res.item(d), 2, !address_of);
    } else if (auto sc = api().scopes.find(scope); sc != api().scopes.end()) {
      for (auto& it2 : sc->second) add_item(it2, 0, !address_of);
    }
  } else {
    const bool after_pipe = n && last == "|";
    if (prefix.empty() && !after_pipe) return r; // plain identifiers need at least one character
    if (after_pipe) {
      for (auto& op : api().pulse_ops) add_item(op, -50);
      if (prefix.empty()) {
        r.items = c.result();
        return r;
      }
    }
    // expression context: an argument, an initializer, an element of a stack
    const bool expression = n && (last == "{" || last == "," || last == "(" || last == "=" || last == "return" ||
                                  last == "?" || last == ":") && here.in_function;
    const bool statement = n == 0 || last == ";" || last == "{" || last == "}" || last == ")" || last == "else";
    add_snippets(c, indent_of(cur), here.in_function && statement, here.in_function, expression);
    for (auto it = here.pending.rbegin(); it != here.pending.rend(); ++it) add_item(res.item(*it), 0);
    int depth = 0;
    for (auto& scope : here.visible) {
      for (auto it = scope.rbegin(); it != scope.rend(); ++it) add_item(res.item(*it), depth < 2 ? 0 : 5);
      ++depth;
    }
    if (!here.current_class.empty())
      res.members(Ty{here.current_class, {}, 0}, [&](const CompletionItem& it, int rank) { add_item(it, 3 + rank); });
    for (auto& d : res.globals) add_item(res.item(d), 6);
    for (auto& [name, body] : res.classes)
      if (!name.empty()) c.add({name, "class", K::Type, {}}, 6);
    for (auto& g : api().globals) add_item(g, 10, g.kind == K::Function);
    if (here.using_pulse)
      for (auto& g : api().scopes.at("pulse")) add_item(g, 12, g.kind == K::Function);
    for (auto& k : keywords()) c.add({k, "", K::Keyword, {}}, 20);
    // any other word of the file, as a last resort
    for (auto& tk : tokenize(whole))
      if (tk.kind == Tok::Ident && tk.s.size() >= 3 && tk.s != prefix && !keywords().count(tk.s) && !full.locals.count(tk.s))
        c.add({tk.s, "", K::Variable, {}}, 40);
  }
  r.items = c.result();
  return r;
}

// ── navigation, hover, refactoring ───────────────────────────────────────────

namespace {

std::pair<std::size_t, std::size_t> word_bounds(const std::string& s, std::size_t col) {
  std::size_t a = std::min(col, s.size()), b = a;
  while (a > 0 && ident_char(s[a - 1])) --a;
  while (b < s.size() && ident_char(s[b])) ++b;
  return {a, b};
}

SourceLocation loc_of(const Decl& d) { return {d.file, d.line + 1, d.col + 1, {}, {}}; }
SourceLocation loc_of(const Loc& l) { return {l.file, l.line + 1, l.col + 1, {}, {}}; }

// The identifier at (line, col) when it is code (not inside a comment or a string).
struct Symbol {
  std::string name;
  std::size_t begin = 0;
};
std::optional<Symbol> symbol_at(const std::vector<std::string>& lines, std::size_t line, std::size_t col) {
  if (line >= lines.size()) return std::nullopt;
  const std::string& cur = lines[line];
  const auto [a, b] = word_bounds(cur, col);
  if (a == b || std::isdigit(static_cast<unsigned char>(cur[a]))) return std::nullopt;
  const std::string name = cur.substr(a, b - a);
  if (keywords().count(name)) return std::nullopt;
  std::string text;
  for (std::size_t i = 0; i < line; ++i) text += lines[i] + '\n';
  const auto toks = tokenize(text + cur.substr(0, b));
  if (toks.empty() || toks.back().kind != Tok::Ident || toks.back().s != name ||
      toks.back().line != static_cast<int>(line) || toks.back().col != static_cast<int>(a))
    return std::nullopt;
  return Symbol{name, a};
}

std::vector<std::string> scope_parts(const std::vector<Tok>& t) { // ... A::B::  ->  {A, B}
  std::vector<std::string> parts;
  for (std::size_t k = t.size(); k >= 2 && t[k - 1].s == "::" && t[k - 2].kind == Tok::Ident; k -= 2)
    parts.insert(parts.begin(), t[k - 2].s);
  return parts;
}

std::string strip_defaults(const std::string& params) { // "(int a, int b = 3)" -> "int a, int b"
  std::string inner = params;
  if (!inner.empty() && inner.front() == '(') inner.erase(0, 1);
  if (!inner.empty() && inner.back() == ')') inner.pop_back();
  std::string out;
  for (auto& p : split_top(inner, ',')) {
    int depth = 0;
    std::size_t cut = p.size();
    for (std::size_t i = 0; i < p.size(); ++i) {
      const char c = p[i];
      if (c == '<' || c == '(' || c == '[' || c == '{') ++depth;
      else if (c == '>' || c == ')' || c == ']' || c == '}') --depth;
      else if (c == '=' && depth == 0) { cut = i; break; }
    }
    const std::string one = trim(p.substr(0, cut));
    if (one.empty()) continue;
    out += (out.empty() ? "" : ", ") + one;
  }
  return out;
}

std::string describe_decl(const Decl& d, const Resolver& res, const std::string& cls) {
  const std::string scope = cls.empty() ? std::string() : cls + "::";
  switch (d.kind) {
    case K::Function: {
      std::string ret = d.type;
      if (ret == "explicit" || d.name == cls) ret.clear();
      return (ret.empty() ? "" : ret + " ") + scope + d.name + d.params + (d.is_const ? " const" : "");
    }
    case K::Type:
      return (res.classes.count(d.name) ? "class " : "type ") + d.name;
    case K::Value:
      return d.type + "::" + d.name;
    default: {
      std::string t = res.decl_type(d);
      if (t.empty()) t = d.type;
      return t + " " + scope + d.name;
    }
  }
}

std::string describe_api(const std::string& owner, const CompletionItem& m) {
  if (is_function_detail(m.detail)) return owner + "::" + m.label + m.detail;
  return m.detail.empty() ? owner + "::" + m.label : m.detail + " " + owner + "::" + m.label;
}

} // namespace

std::optional<SourceLocation> find_definition(const std::vector<std::string>& lines, std::size_t line, std::size_t col,
                                              const CompletionContext& ctx) {
  const auto sym = symbol_at(lines, line, col);
  if (!sym) return std::nullopt;
  const std::string& name = sym->name;
  const Analysis an(lines, line, sym->begin, ctx);
  const Resolver& res = *an.res;
  const auto& t = an.toks;
  const std::size_t n = t.size();
  const std::string last = n ? t[n - 1].s : std::string();
  auto valid = [](const SourceLocation& l) -> std::optional<SourceLocation> {
    if (l.line <= 0 && l.form.empty()) return std::nullopt;
    return l;
  };
  // a function: its body, or its declaration when the caret is on the body already
  auto function_target = [&](const Decl& d, const std::string& cls) {
    const SourceLocation decl = loc_of(d);
    auto it = res.definitions.find(cls.empty() ? d.name : cls + "::" + d.name);
    if (it == res.definitions.end()) return decl;
    const SourceLocation def = loc_of(it->second);
    const bool at_def = def.file == ctx.current_file && def.line == static_cast<int>(line) + 1;
    return at_def ? decl : def;
  };
  auto member = [&](const std::string& cls) -> std::optional<SourceLocation> {
    const auto [d, owner] = res.class_member_in(cls, name);
    if (!d) return std::nullopt;
    return valid(d->kind == K::Function ? function_target(*d, owner) : loc_of(*d));
  };

  if (last == "." || last == "->") {
    if (n < 2) return std::nullopt;
    const std::size_t s = Resolver::expr_start(t, n - 2);
    if (s == npos) return std::nullopt;
    Ty owner = res.eval(t, s, n - 1);
    owner = last == "->" ? Resolver::arrow(owner) : Ty{owner.base, owner.args, 0};
    if (starts_with(owner.base, "Ui::")) return SourceLocation{{}, 0, 0, owner.base.substr(4), name};
    return member(owner.base);
  }
  if (last == "::") {
    const auto parts = scope_parts(t);
    if (parts.empty()) return std::nullopt;
    const std::string& scope = parts.back();
    if (scope == "Ui") return SourceLocation{{}, 0, 0, name, {}};
    if (parts.size() >= 2 && parts[parts.size() - 2] == "Ui") return SourceLocation{{}, 0, 0, scope, {}};
    if (auto r = member(scope)) return r;
    if (name == scope) // Class::Class
      if (auto it = res.definitions.find(scope + "::" + name); it != res.definitions.end()) return loc_of(it->second);
    return std::nullopt;
  }
  if (const Decl* d = res.find_var(name)) {
    if (d->kind == K::Function) {
      std::string cls;
      if (!res.here.current_class.empty()) {
        const auto [m, owner] = res.class_member_in(res.here.current_class, name);
        if (m) cls = owner;
      }
      return valid(function_target(*d, cls));
    }
    return valid(loc_of(*d));
  }
  for (auto& g : res.globals)
    if (g.name == name && g.kind == K::Type) return valid(loc_of(g));
  if (std::find(ctx.form_classes.begin(), ctx.form_classes.end(), name) != ctx.form_classes.end())
    return SourceLocation{{}, 0, 0, name, {}};
  return std::nullopt;
}

std::string describe_symbol(const std::vector<std::string>& lines, std::size_t line, std::size_t col,
                            const CompletionContext& ctx) {
  const auto sym = symbol_at(lines, line, col);
  if (!sym) return {};
  const std::string& name = sym->name;
  const Analysis an(lines, line, sym->begin, ctx);
  const Resolver& res = *an.res;
  const auto& t = an.toks;
  const std::size_t n = t.size();
  const std::string last = n ? t[n - 1].s : std::string();

  auto member_text = [&](const Ty& owner) -> std::string {
    if (starts_with(owner.base, "Ui::")) {
      for (auto& v : ctx.views)
        if (v.form == owner.base.substr(4) && v.id == name)
          return view_type(v) + "* " + name + "\nview of the form " + v.form;
      return {};
    }
    if (const auto [d, cls] = res.class_member_in(owner.base, name); d) return describe_decl(*d, res, cls);
    std::vector<std::string> types{owner.base};
    if (auto ab = res.api_base(owner.base); !ab.empty()) types.push_back(ab);
    for (auto& at : types) {
      auto it = api().members.find(at);
      if (it == api().members.end()) continue;
      for (auto& m : it->second)
        if (m.label == name) return describe_api(at, m);
      if (api().views.count(at))
        for (auto& m : api().view_common)
          if (m.label == name) return describe_api(at, m);
    }
    return {};
  };

  if (last == "." || last == "->") {
    if (n < 2) return {};
    const std::size_t s = Resolver::expr_start(t, n - 2);
    if (s == npos) return {};
    Ty owner = res.eval(t, s, n - 1);
    owner = last == "->" ? Resolver::arrow(owner) : Ty{owner.base, owner.args, 0};
    return member_text(owner);
  }
  if (last == "::") {
    const auto parts = scope_parts(t);
    if (parts.empty()) return {};
    const std::string& scope = parts.back();
    if (scope == "Ui") return "struct Ui::" + name + "\nthe designed form " + name;
    if (auto it = api().scopes.find(scope); it != api().scopes.end())
      for (auto& m : it->second)
        if (m.label == name) return m.kind == K::Value ? scope + "::" + name : describe_api(scope, m);
    if (const auto [d, cls] = res.class_member_in(scope, name); d) return describe_decl(*d, res, cls);
    return {};
  }
  if (const Decl* d = res.find_var(name)) {
    std::string cls;
    if (!res.here.current_class.empty() && res.class_member(res.here.current_class, name) == d) cls = res.here.current_class;
    return describe_decl(*d, res, cls);
  }
  for (auto& g : api().globals)
    if (g.label == name) return is_function_detail(g.detail) ? name + g.detail : name + (g.detail.empty() ? "" : " — " + g.detail);
  if (res.here.using_pulse)
    for (auto& g : api().scopes.at("pulse"))
      if (g.label == name) return is_function_detail(g.detail) ? "pulse::" + name + g.detail : "pulse::" + name + " " + g.detail;
  for (auto& g : res.globals)
    if (g.name == name && g.kind == K::Type) return describe_decl(g, res, {});
  return {};
}

bool symbol_is_shared(const std::vector<std::string>& lines, std::size_t line, std::size_t col, const CompletionContext& ctx) {
  const auto sym = symbol_at(lines, line, col);
  if (!sym) return false;
  const std::string& name = sym->name;
  const Analysis an(lines, line, sym->begin, ctx);
  const Scan& here = an.here;
  const auto& t = an.toks;
  const bool qualified = !t.empty() && (t.back().s == "." || t.back().s == "->" || t.back().s == "::");
  if (!qualified) { // a parameter or a local variable of the function at the caret
    for (auto& d : here.pending)
      if (d.name == name) return false;
    for (std::size_t i = 0; i < here.visible.size(); ++i) {
      const int k = here.visible_kind[i];
      if (k != static_cast<int>(Block::Function) && k != static_cast<int>(Block::Plain)) continue;
      for (auto& d : here.visible[i])
        if (d.name == name) return false;
    }
  }
  for (auto& [c, decls] : an.res->classes)
    for (auto& d : decls)
      if (d.name == name) return true;
  for (auto& d : an.res->globals)
    if (d.name == name) return true;
  return false;
}

std::vector<std::pair<std::size_t, std::size_t>> find_occurrences(const std::vector<std::string>& lines,
                                                                  const std::string& name) {
  std::string whole;
  for (auto& l : lines) whole += l + '\n';
  std::vector<std::pair<std::size_t, std::size_t>> out;
  for (auto& tk : tokenize(whole))
    if (tk.kind == Tok::Ident && tk.s == name) out.push_back({static_cast<std::size_t>(tk.line), static_cast<std::size_t>(tk.col)});
  return out;
}

std::optional<DefinitionStub> definition_stub(const std::vector<std::string>& lines, std::size_t line,
                                              const CompletionContext& ctx) {
  const Analysis an(lines, 0, 0, ctx);
  const Scan& full = *an.full;
  const Decl* found = nullptr;
  std::string cls;
  for (auto& [c, decls] : full.classes)
    for (auto& d : decls)
      if (d.kind == K::Function && d.line == static_cast<int>(line)) {
        found = &d;
        cls = c;
      }
  if (!found)
    for (auto& d : full.globals)
      if (d.kind == K::Function && d.line == static_cast<int>(line)) found = &d;
  if (!found || found->no_stub) return std::nullopt;
  const Decl& d = *found;
  DefinitionStub stub;
  stub.name = (cls.empty() ? "" : cls + "::") + d.name;
  if (d.has_body) {
    stub.existing = loc_of(d);
    return stub;
  }
  if (auto it = an.res->definitions.find(stub.name); it != an.res->definitions.end()) {
    stub.existing = loc_of(it->second);
    return stub;
  }
  std::string ret = d.type;
  if (ret == "explicit" || d.name == cls) ret.clear();
  if (starts_with(ret, "virtual ")) ret = ret.substr(8);
  stub.text = (ret.empty() ? "" : ret + " ") + stub.name + "(" + strip_defaults(d.params) + ")" + (d.is_const ? " const" : "") +
              " {\n    \n}\n";
  // the matching .cpp of a header, otherwise the same file
  stub.file = ctx.current_file;
  const auto dot = ctx.current_file.rfind('.');
  const std::string ext = dot == npos ? std::string() : ctx.current_file.substr(dot);
  if (ext == ".hpp" || ext == ".h" || ext == ".hh") {
    const std::string cpp = ctx.current_file.substr(0, dot) + ".cpp";
    for (auto& [name, _] : ctx.other_files)
      if (name == cpp) stub.file = cpp;
  }
  return stub;
}

pulseui::CodeEditor::CompletionProvider make_cpp_completion(std::function<CompletionContext()> context) {
  return [context](const std::vector<std::string>& lines, std::size_t line, std::size_t col) {
    return complete_cpp(lines, line, col, context());
  };
}

} // namespace layer
