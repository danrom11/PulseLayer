#pragma once
// Builds and runs the form + user code with the selected kit.
//
// Generated project (in the build folder):
//   src/CMakeLists.txt   - add_subdirectory(PulseUI) + pulseui_add_app(app <all .cpp>)
//   src/ui_<form>.hpp    - every designed form as declarative C++ (Ui::<Class>::build())
//   src/*.cpp, *.hpp     - the project files written in the editor
//   build/               - CMake binary dir (incremental between builds)
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <pulseui/pulseui.hpp>

#include "process.hpp"
#include "toolchain.hpp"

namespace layer {

struct Issue {
  std::string file;
  int line = 0, column = 0;
  pulseui::Diagnostic::Severity severity = pulseui::Diagnostic::Error;
  std::string message;
  bool user_code = false; // the location is in a project file (then `file` is its name, e.g. "main.cpp")
  bool operator==(const Issue&) const = default;
};

// Parses one line of compiler / linker / CMake output (MSVC, GCC, Clang).
bool parse_issue(const std::string& line, Issue& out);

// release.cpp
std::vector<std::string> pe_imports(const std::string& exe_or_dll);
std::vector<std::string> deploy_windows(const std::string& exe, const std::string& dir, const std::string& name, const Kit& kit,
                                        const std::map<std::string, std::string>& env, std::string* error);

class Builder {
public:
  enum State { Idle = 0, Configuring, Building, Running };

  struct Request {
    Kit kit;
    Tool cmake, ninja;
    std::string pulseui_dir; // folder of PulseUI sources
    std::string project_dir; // where src/ and build/ go
    std::vector<std::pair<std::string, std::string>> files; // name -> content (generated + project files)
    std::vector<std::string> user_files;                     // names written by the user (for error locations)
    // Release: optimized build in build-release/, then packaged into release_dir
    // (Windows: <app_name>.exe + needed DLLs; macOS: signed <app_name>.app + .dmg).
    bool release = false;
    std::string app_name = "app";
    std::string release_dir;
    std::string sign_identity; // macOS codesign identity ("" = ad hoc)
  };

  Builder();
  ~Builder();

  pulse::property<int> state{Idle};
  pulse::property<std::vector<Issue>> issues;
  pulse::signal<std::string> output;           // CMake / compiler output lines (UI thread)
  pulse::signal<std::string> app_output;       // lines the running program writes (UI thread)
  pulse::signal<std::string, std::string> note; // (translation key, argument) for status messages

  bool build(const Request& r, bool run_after);
  // Checks the user's .cpp files without building, with the exact compile commands of the
  // last configured build (compile_commands.json) and -fsyntax-only (/Zs for MSVC).
  // `done` gets the issues on the UI thread. Returns false when there is no configured build.
  bool check(const Request& r, std::function<void(std::vector<Issue>)> done);
  bool run();
  void stop();
  std::string executable() const;
  std::string build_dir() const; // build/ or build-release/
  const std::string& release_result() const { return release_result_; } // the packaged app (after a release)
  const std::string& last_project_dir() const { return project_dir_; }
  bool last_build_ok() const { return last_ok_; }

private:
  void step_configure();
  void step_build();
  void finish_build(bool ok);
  void package(); // after a release build
  void launch(const std::string& command, std::map<std::string, std::string> env, bool replace_env,
              std::function<void(int)> done);
  void on_line(const std::string& line);

  Request req_;
  std::string project_dir_;
  bool run_after_ = false, last_ok_ = false, proc_killed_note_ = false;
  std::string release_result_;
  std::shared_ptr<Process> proc_;
  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
  std::chrono::steady_clock::time_point started_{};
  std::map<std::string, std::string> env_;
  bool replace_env_ = false;
};

} // namespace layer
