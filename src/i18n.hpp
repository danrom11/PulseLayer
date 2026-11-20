#pragma once
// Localization for Pulse Layer.
//
// The current language is a pulse::property, and tr() returns a *stream* of
// the translated text, so every label bound to tr(...) re-renders the moment
// the language changes - no restart, no manual refresh.
//
//   Text(tr("panel.palette"))
//   Button(tr("tb.save")).tooltip(tr("tb.save.tip"))
//   doc.status = {"status.saved", path};        // "{0}" is replaced by the argument
//   tr("about.built", a + "\x1f" + b)            // several arguments: "{0}", "{1}"
//
// Adding a language
//   1. Built in: copy src/lang/de.cpp, translate, add one line to register_builtin()
//      in i18n.cpp and the file to CMakeLists.txt.
//   2. Without recompiling: drop a JSON file into the `lang/` folder next to the
//      executable:  { "code": "es", "name": "Español", "strings": { "tb.new": "Nuevo", ... } }
//      `PulseLayer --export-lang template.json` writes all keys with English text.
// Missing keys fall back to English, then to the key's fallback text.
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include <pulse/pulse.hpp>

namespace layer::i18n {

struct Language {
  std::string code; // "en", "ru", ...
  std::string name; // native name shown in the selector: "Русский"
  std::map<std::string, std::string> strings;
};

// A translatable message with an optional "{0}" argument (e.g. a file name).
struct Message {
  std::string key;
  std::string arg;
  bool operator==(const Message&) const = default;
};

void register_builtin();
void register_language(Language lang); // same code: merges / overrides strings
int load_directory(const std::filesystem::path& dir); // returns the number of loaded files
bool export_template(const std::filesystem::path& file);

const std::vector<Language>& languages();
int index_of(const std::string& code); // -1 if unknown

// Current language code (reactive). Defaults to "en".
pulse::property<std::string>& current();

// Immediate lookup in the current language.
std::string text(std::string_view key, std::string_view arg = {}, std::string_view fallback = {});
std::string text(const Message& m);

// Reactive lookup: emits now and on every language change.
pulse::observable<std::string> tr(std::string key, std::string arg = {}, std::string fallback = {});
pulse::observable<std::string> tr(pulse::property<Message>& message);

} // namespace layer::i18n
