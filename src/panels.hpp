#pragma once
#include <memory>
#include <vector>

#include "builder.hpp"
#include "canvas.hpp"
#include "cpp_complete.hpp"
#include "project.hpp"
#include "document.hpp"

namespace layer {

struct Designer {
  Document doc;      // the form being edited on the canvas
  Project project;   // all forms and files
  int loaded_form = -1;                     // index of the form currently in `doc`
  pulse::property<std::string> editor_text; // text of the active file
  bool loading_text = false;
  std::unique_ptr<Window> dialog_window;
  DesignCanvas* canvas = nullptr;
  Window* window = nullptr;
  pulse::property<bool> dark{true};
  pulse::property<int> language{0}; // index into i18n::languages()
  pulse::property<int> theme{0};    // 0 = dark, 1 = light (mirrors `dark`)
  std::unique_ptr<Window> settings_window;

  // ── code & build ──
  pulse::property<int> mode{0};                 // 0 = design, 1 = code
  pulse::property<Toolchains> toolchains;       // detected kits + CMake/Ninja
  pulse::property<bool> detecting{false};
  pulse::property<int> kit_index{0};
  pulse::property<std::string> pulseui_dir;     // override of the PulseUI sources
  std::string preferred_kit;                    // kit id from the settings
  Builder builder;
  CodeEditor* editor = nullptr;
  CodeEditor* output = nullptr;
  std::shared_ptr<bool> alive = std::make_shared<bool>(true);
  ~Designer() { *alive = false; }
  std::vector<std::unique_ptr<Window>> previews;

  // project
  void wire_project();
  void sync_form();                 // canvas -> project
  void load_form(int index);        // project -> canvas
  void open_file(const std::string& name);
  void reload_project_views();
  bool open_project(const std::string& path);
  bool save_project(const std::string& path);
  bool add_form(const std::string& cls, std::string* error = nullptr);
  bool add_file(const std::string& name, std::string* error = nullptr);
  void remove_form(int index);
  void remove_file(const std::string& name);
  void cmd_add_form();
  void cmd_add_file();
  void ask_name(const std::string& title_key, const std::string& hint_key, const std::string& initial,
                std::function<bool(const std::string&, std::string*)> accept);
  CompletionContext completion_context();

  void cmd_new();
  void cmd_open();
  void cmd_save(bool save_as);
  void cmd_export();
  std::string export_file_name(); // "<form>_form.hpp"
  void cmd_preview();
  void cmd_delete();
  void cmd_duplicate();
  void cmd_settings(int tab = 0); // 0 = General, 1 = Kits, 2 = About
  void detect_kits();
  void cmd_build(bool run_after);
  void cmd_run();
  void cmd_stop();
  std::string project_dir();
  std::string effective_pulseui_dir();
};

Node toolbar(Designer& d);
Node palette(Designer& d);
Node hierarchy(Designer& d);
Node inspector(Designer& d);
Node status_bar(Designer& d);
Node code_view(Designer& d);
Node project_tree(Designer& d);
Node code_mode(Designer& d);
Node kits_settings(Designer& d);

} // namespace layer
