#include "settings.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>

#include <pulseui/core/json.hpp>

namespace layer {

std::filesystem::path Settings::file() {
  std::filesystem::path base;
#if defined(_WIN32)
  if (const char* appdata = std::getenv("APPDATA")) base = appdata;
#elif defined(__APPLE__)
  if (const char* home = std::getenv("HOME")) base = std::filesystem::path(home) / "Library" / "Application Support";
#else
  if (const char* xdg = std::getenv("XDG_CONFIG_HOME")) base = xdg;
  else if (const char* home = std::getenv("HOME")) base = std::filesystem::path(home) / ".config";
#endif
  if (base.empty()) base = std::filesystem::temp_directory_path();
  return base / "PulseLayer" / "settings.json";
}

Settings Settings::load() {
  Settings s;
  std::ifstream in(file(), std::ios::binary);
  if (!in) return s;
  std::stringstream ss;
  ss << in.rdbuf();
  if (auto j = pulseui::json::Value::parse(ss.str())) {
    s.language = (*j)["language"].as_string(s.language);
    s.dark = (*j)["dark"].as_bool(s.dark);
    s.kit = (*j)["kit"].as_string();
    s.pulseui_dir = (*j)["pulseui_dir"].as_string();
    s.sign_identity = (*j)["sign_identity"].as_string();
    s.build_config = static_cast<int>((*j)["build_config"].as_number(0));
  }
  return s;
}

bool Settings::save() const {
  std::error_code ec;
  std::filesystem::create_directories(file().parent_path(), ec);
  pulseui::json::Value j = pulseui::json::Value::object();
  j["language"] = language;
  j["dark"] = dark;
  j["kit"] = kit;
  j["pulseui_dir"] = pulseui_dir;
  j["sign_identity"] = sign_identity;
  j["build_config"] = build_config;
  std::ofstream out(file(), std::ios::binary);
  out << j.dump(2) << "\n";
  return static_cast<bool>(out);
}

} // namespace layer
