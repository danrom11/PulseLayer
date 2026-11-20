#pragma once
// Child processes with captured output (compilers, CMake, the built program).
#include <functional>
#include <map>
#include <memory>
#include <string>

namespace layer {

class Process {
public:
  struct Options {
    std::string command;                     // full command line (shell-free on Windows, /bin/sh -c on POSIX)
    std::string cwd;                         // working directory ("" = current)
    std::map<std::string, std::string> env;  // added / overridden variables
    bool replace_env = false;                // true: `env` is the whole environment
    bool capture = true;                     // false: launch detached (GUI programs)
  };
  using LineFn = std::function<void(const std::string&)>; // called on a worker thread
  using ExitFn = std::function<void(int)>;                // called on a worker thread

  static std::shared_ptr<Process> start(Options opt, LineFn on_line, ExitFn on_exit);

  // Runs and waits (used by toolchain detection). Returns the exit code, -1 on failure/timeout.
  static int run(const std::string& command, std::string& output, int timeout_ms = 15000,
                 const std::map<std::string, std::string>& env = {}, bool replace_env = false);

  virtual ~Process() = default;
  virtual void kill() = 0; // terminates the whole process tree
  virtual bool running() const = 0;

  static std::string quote(const std::string& arg);
  static std::map<std::string, std::string> current_environment();
};

} // namespace layer
