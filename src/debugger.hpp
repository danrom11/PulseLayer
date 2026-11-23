#pragma once
// Debugging the built program: breakpoints, stepping, the call stack and locals.
// One interface, three engines:
//   GDB (MI)   - MinGW / GCC builds (gdb next to the compiler, or on PATH)
//   CDB        - MSVC builds on Windows (the Windows SDK debugger)
//   LLDB (DAP) - macOS (lldb-dap from Xcode), or anywhere lldb-dap is available
// Each runs as a child process; its replies are parsed on the UI thread and
// published as reactive properties the debug panel shows.
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <pulseui/pulseui.hpp>

#include "process.hpp"
#include "toolchain.hpp"

namespace layer {

struct DebugFrame {
  std::string function;
  std::string file; // absolute path ("" when there is no source)
  int line = 0;
  bool operator==(const DebugFrame&) const = default;
};

struct DebugVar {
  std::string name, type, value;
  bool operator==(const DebugVar&) const = default;
};

struct DebuggerTool {
  enum class Kind { None, Gdb, Cdb, LldbDap };
  Kind kind = Kind::None;
  std::string path;
  std::string name() const; // "GDB", "CDB", "LLDB"
  bool found() const { return kind != Kind::None; }
};

// The debugger that fits programs built with `kit` (kind None when there is none).
DebuggerTool find_debugger(const Kit& kit);

class Debugger {
public:
  enum State { Idle = 0, Starting, Running, Paused, Exited };
  using Breakpoints = std::map<std::string, std::vector<int>>; // absolute file -> 1-based lines

  pulse::property<int> state{Idle};
  pulse::property<std::vector<DebugFrame>> stack;
  pulse::property<std::vector<DebugVar>> locals;
  pulse::property<int> frame{0};       // selected stack frame
  pulse::signal<std::string> output;          // the debugger's own messages (UI thread)
  pulse::signal<std::string> program_output;  // what the program writes to stdout / stderr

  static std::unique_ptr<Debugger> create(const DebuggerTool& tool);
  virtual ~Debugger();

  virtual bool start(const std::string& exe, const std::string& cwd, const Breakpoints& breakpoints,
                     const std::map<std::string, std::string>& env, bool replace_env) = 0;
  virtual void set_breakpoints(const std::string& file, const std::vector<int>& lines) = 0;
  virtual void resume() = 0;
  virtual void step_over() = 0;
  virtual void step_into() = 0;
  virtual void step_out() = 0;
  virtual void pause() = 0;
  virtual void stop() = 0;
  virtual void select_frame(int index) = 0;
  virtual bool can_pause() const { return true; }
  virtual std::string name() const = 0;

  // The selected frame (null while running).
  const DebugFrame* current() const;

protected:
  bool launch(const std::string& command, const std::string& cwd, const std::map<std::string, std::string>& env,
              bool replace_env, bool raw);
  virtual void on_text(const std::string& text) = 0; // a line (or a chunk in raw mode), UI thread
  void on_exit(int code);
  void send_raw(const std::string& text);

  std::shared_ptr<Process> proc_;
  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

// Parsing helpers (exposed for the self test).
namespace debug_parse {
// GDB/MI: `*stopped,reason="breakpoint-hit",frame={func="main",file="a.cpp",line="7"}`
struct Mi {
  std::string str;
  std::vector<std::pair<std::string, Mi>> items; // tuple / list entries (key empty for plain list values)
  const Mi* get(const std::string& key) const;
  std::string s(const std::string& key) const { const Mi* m = get(key); return m ? m->str : std::string(); }
};
Mi parse_mi(const std::string& results); // the part after "done," / "stopped,"
std::string mi_unescape(const std::string& cstring); // "\"a\\n\"" -> a<newline>
// CDB: one line of `kn` / `dv /t` output
bool cdb_frame(const std::string& line, DebugFrame& out);
bool cdb_local(const std::string& line, DebugVar& out);
// GDB/MI: a record ("^done", "*stopped", "=thread", "~text", "12^done"), not the program's output
bool is_mi_record(const std::string& line);
// CDB: a line cdb prints while the program runs (module loads, breakpoint hits, registers)
bool cdb_noise(const std::string& line);
} // namespace debug_parse

} // namespace layer
