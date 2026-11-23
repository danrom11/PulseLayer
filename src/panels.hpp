#pragma once
#include <memory>
#include <vector>

#include "builder.hpp"
#include "debugger.hpp"
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
  CodeEditor* output = nullptr;      // Build output: CMake, the compiler, the debugger
  CodeEditor* app_output = nullptr;  // Application output: what the running program prints
  void show_app_output(const std::string& first_line); // clears it, opens the tab
  pulseui::MenuItems editor_menu();  // right click in the code editor
  std::shared_ptr<bool> alive = std::make_shared<bool>(true);
  ~Designer() { *alive = false; }
  std::vector<std::unique_ptr<Window>> previews;
  std::shared_ptr<void> palette_parts; // the "Project" palette entries (other forms, project views)

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
  void cmd_preview_board(); // the form in several themes / sizes / languages side by side
  std::unique_ptr<Window> board_window;
  std::shared_ptr<void> board_state;
  void cmd_delete();
  void cmd_duplicate();
  void cmd_settings(int tab = 0); // 0 = General, 1 = Kits, 2 = About
  void detect_kits();
  void cmd_build(bool run_after);
  void cmd_run();
  void cmd_stop();
  std::string project_dir();
  std::string effective_pulseui_dir();
  Builder::Request build_request();

  // ── editor tools (code_tools.cpp) ──
  pulse::property<std::vector<std::string>> open_files; // tabs above the editor
  pulse::property<bool> find_open{false}, replace_open{false}, find_case{false}, find_word{false};
  pulse::property<std::string> find_query, replace_query, find_status;
  TextField* find_field = nullptr;
  bool checking = false;
  void wire_editor_tools();
  void close_tab(const std::string& name);
  void cmd_find(bool replace);       // Ctrl+F / Ctrl+H
  void find_close();
  void find_refresh(bool select);
  void find_step(bool backwards);    // F3 / Shift+F3
  void cmd_replace_one();
  void cmd_replace_all();
  void cmd_go_to_definition();       // F12, at the caret
  void go_to_definition(std::size_t line, std::size_t col);
  bool go_to(const SourceLocation& loc);
  void cmd_rename();                 // F2
  bool rename_symbol(const std::string& old_name, const std::string& new_name, bool whole_project);
  void cmd_create_definition();      // Alt+Enter
  void cmd_switch_header();          // Alt+O / F4
  void cmd_format();                 // Ctrl+Shift+F
  bool check_syntax();               // on save

  // ── debugging (debug_view.cpp) ──
  std::unique_ptr<Debugger> debugger;
  DebuggerTool debug_tool;
  pulse::property<int> debug_state{Debugger::Idle};
  pulse::property<std::map<std::string, std::vector<int>>> breakpoints; // project file -> 1-based lines
  pulse::property<std::vector<DebugFrame>> debug_stack;
  pulse::property<std::vector<DebugVar>> debug_locals;
  pulse::property<int> debug_frame{0};
  pulse::property<std::string> exec_file;
  pulse::property<int> exec_line{0};
  std::vector<pulse::subscription> debug_subs;
  bool debug_after_build = false;
  void wire_debugger();
  // ── release (debug_view.cpp) ──
  pulse::property<std::string> sign_identity; // macOS codesign identity ("" = ad hoc)
  void cmd_release();                         // optimized build, packaged into <project>/release
  // Qt Creator style: one configuration chosen in the toolbar, Build and Run follow it
  pulse::property<int> build_config{0};
  pulse::property<int> bottom_tab{0}; // Issues / Application output / Build output / Debug
  static constexpr int kTabAppOutput = 1, kTabBuildOutput = 2, kTabDebug = 3;         // Issues / Build output / Debug       // 0 = Debug (Run debugs), 1 = Release (Run packages and starts)
  bool run_release_after = false;
  std::string release_app_name;
  void cmd_build_selected();                  // Ctrl+B
  void cmd_run_plain();                       // Ctrl+Shift+R: a Debug build without the debugger
  bool debugging() const;
  std::string release_dir();
  void open_folder(const std::string& path);
  void cmd_debug();                       // F5 in Code mode: start, or continue when paused
  void cmd_toggle_breakpoint(int line = 0); // F9: the caret line
  void cmd_step_over();                   // F10
  void cmd_step_into();                   // F11
  void cmd_step_out();                    // Shift+F11
  void cmd_debug_pause();
  void cmd_debug_stop();                  // Shift+F5
  void start_debugger();
  void show_frame(int index);
  std::string debug_source_path(const std::string& file);
};

Node debug_controls(Designer& d);
Node debug_panel(Designer& d);

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
