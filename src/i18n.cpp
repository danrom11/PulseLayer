#include "i18n.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

#include <pulseui/core/json.hpp>

namespace layer::i18n {

// Built-in languages (one file each in src/lang/).
Language english();
Language russian();
Language german();
Language french();

namespace {

std::vector<Language>& registry() {
  static std::vector<Language> langs;
  return langs;
}

const Language* find(std::string_view code) {
  for (auto& l : registry())
    if (l.code == code) return &l;
  return nullptr;
}

// "{0}", "{1}", ... are replaced by the parts of `arg` separated by '\x1f'.
std::string format(std::string s, std::string_view arg) {
  for (int i = 0; i < 9; ++i) {
    const std::size_t sep = arg.find('\x1f');
    const std::string_view part = arg.substr(0, sep);
    const std::string token = "{" + std::to_string(i) + "}";
    for (auto pos = s.find(token); pos != std::string::npos; pos = s.find(token, pos + part.size()))
      s.replace(pos, token.size(), part);
    if (sep == std::string_view::npos) break;
    arg.remove_prefix(sep + 1);
  }
  return s;
}

} // namespace

void register_builtin() {
  register_language(english()); // English first: it is the default and the fallback
  register_language(russian());
  register_language(german());
  register_language(french());
}

void register_language(Language lang) {
  for (auto& l : registry()) {
    if (l.code == lang.code) {
      if (!lang.name.empty()) l.name = lang.name;
      for (auto& [k, v] : lang.strings) l.strings[k] = v;
      return;
    }
  }
  registry().push_back(std::move(lang));
}

int load_directory(const std::filesystem::path& dir) {
  std::error_code ec;
  if (!std::filesystem::is_directory(dir, ec)) return 0;
  int loaded = 0;
  for (auto& entry : std::filesystem::directory_iterator(dir, ec)) {
    if (entry.path().extension() != ".json") continue;
    std::ifstream in(entry.path(), std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    auto j = pulseui::json::Value::parse(ss.str());
    if (!j || !(*j)["code"].is_string()) continue;
    Language lang;
    lang.code = (*j)["code"].as_string();
    lang.name = (*j)["name"].as_string(lang.code);
    for (auto& [k, v] : (*j)["strings"].fields())
      if (v.is_string()) lang.strings[k] = v.as_string();
    register_language(std::move(lang));
    ++loaded;
  }
  return loaded;
}

bool export_template(const std::filesystem::path& file) {
  const Language* en = find("en");
  if (!en) return false;
  pulseui::json::Value j = pulseui::json::Value::object();
  j["code"] = "xx";
  j["name"] = "Language name";
  pulseui::json::Value strings = pulseui::json::Value::object();
  for (auto& [k, v] : en->strings) strings[k] = v;
  j["strings"] = std::move(strings);
  std::ofstream out(file, std::ios::binary);
  out << j.dump(2) << "\n";
  return static_cast<bool>(out);
}

const std::vector<Language>& languages() { return registry(); }

int index_of(const std::string& code) {
  for (std::size_t i = 0; i < registry().size(); ++i)
    if (registry()[i].code == code) return static_cast<int>(i);
  return -1;
}

pulse::property<std::string>& current() {
  static pulse::property<std::string> code{"en"};
  return code;
}

std::string text(std::string_view key, std::string_view arg, std::string_view fallback) {
  for (const Language* l : {find(current().get()), find("en")}) {
    if (!l) continue;
    auto it = l->strings.find(std::string(key));
    if (it != l->strings.end()) return format(it->second, arg);
  }
  return format(std::string(fallback.empty() ? key : fallback), arg);
}

std::string text(const Message& m) { return text(m.key, m.arg); }

pulse::observable<std::string> tr(std::string key, std::string arg, std::string fallback) {
  return current().map([key = std::move(key), arg = std::move(arg), fallback = std::move(fallback)](const std::string&) {
    return text(key, arg, fallback);
  });
}

pulse::observable<std::string> tr(pulse::property<Message>& message) {
  return pulse::combine_latest(current().as_observable(), message.as_observable(),
                               [](const std::string&, const Message& m) { return text(m); });
}

} // namespace layer::i18n
