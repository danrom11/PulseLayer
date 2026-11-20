#pragma once
// Builds and runs the form + user code with the selected kit.
//
// Generated project (in the build folder):
//   src/CMakeLists.txt   - add_subdirectory(PulseUI) + pulseui_add_app(app <all .cpp>)
//   src/ui_<form>.hpp    - every designed form as declarative C++ (Ui::<Class>::build())
//   src/*.cpp, *.hpp     - the project files written in the editor
//   build/               - CMake binary dir (incremental between builds)
#include <chrono>
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
  };

  Builder();
  ~Builder();

  pulse::property<int> state{Idle};
  pulse::property<std::vector<Issue>> issues;
  pulse::signal<std::string> output;           // every output line (UI thread)
  pulse::signal<std::string, std::string> note; // (translation key, argument) for status messages

  bool build(const Request& r, bool run_after);
  bool run();
  void stop();
  std::string executable() const;
  const std::string& last_project_dir() const { return project_dir_; }
  bool last_build_ok() const { return last_ok_; }

private:
  void step_configure();
  void step_build();
  void finish_build(bool ok);
  void launch(const std::string& command, std::map<std::string, std::string> env, bool replace_env,
              std::function<void(int)> done);
  void on_line(const std::string& line);

  Request req_;
  std::string project_dir_;
  bool run_after_ = false, last_ok_ = false, proc_killed_note_ = false;
  std::shared_ptr<Process> proc_;
  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
  std::chrono::steady_clock::time_point started_{};
  std::map<std::string, std::string> env_;
  bool replace_env_ = false;
};

} // namespace layer
