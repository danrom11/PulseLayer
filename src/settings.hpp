#pragma once
// User settings (language, theme), stored as JSON in the per-user config dir:
//   Windows: %APPDATA%\PulseLayer\settings.json
//   macOS:   ~/Library/Application Support/PulseLayer/settings.json
//   other:   ~/.config/PulseLayer/settings.json
#include <filesystem>
#include <string>

namespace layer {

struct Settings {
  std::string language = "en";
  bool dark = true;
  std::string kit;         // id of the selected kit ("" = first detected)
  std::string pulseui_dir; // PulseUI sources used to build programs ("" = auto)

  static std::filesystem::path file();
  static Settings load();
  bool save() const;
};

} // namespace layer
