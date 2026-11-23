// Debugger engines: GDB/MI, CDB and LLDB (Debug Adapter Protocol).
#include "debugger.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <regex>

namespace fs = std::filesystem;

namespace layer {

std::string DebuggerTool::name() const {
  switch (kind) {
    case Kind::Gdb: return "GDB";
    case Kind::Cdb: return "CDB";
    case Kind::LldbDap: return "LLDB";
    default: return "";
  }
}

// ═════════════════════════════════════════════════════════════════════════════
// Debugger (common)
// ═════════════════════════════════════════════════════════════════════════════

Debugger::~Debugger() {
  *alive_ = false;
  if (proc_) proc_->kill();
}

const DebugFrame* Debugger::current() const {
  if (state.get() != Paused) return nullptr;
  const auto& s = stack.get();
  const int f = frame.get();
  return f >= 0 && f < static_cast<int>(s.size()) ? &s[static_cast<std::size_t>(f)] : nullptr;
}

bool Debugger::launch(const std::string& command, const std::string& cwd, const std::map<std::string, std::string>& env,
                      bool replace_env, bool raw) {
  Process::Options opt;
  opt.command = command;
  opt.cwd = cwd;
  opt.env = env;
  opt.replace_env = replace_env;
  opt.input = true;
  opt.raw = raw;
  std::weak_ptr<bool> alive = alive_;
  proc_ = Process::start(
      opt,
      [this, alive](const std::string& text) {
        pulseui::App::post([this, alive, text] {
          if (!alive.expired()) on_text(text);
        });
      },
      [this, alive](int code) {
        pulseui::App::post([this, alive, code] {
          if (!alive.expired()) on_exit(code);
        });
      });
  if (!proc_) return false;
  state = Starting;
  return true;
}

void Debugger::on_exit(int) {
  state = Exited;
  stack = std::vector<DebugFrame>{};
  locals = std::vector<DebugVar>{};
}

void Debugger::send_raw(const std::string& text) {
  if (proc_) proc_->write(text);
}

// ═════════════════════════════════════════════════════════════════════════════
// parsing helpers
// ═════════════════════════════════════════════════════════════════════════════
namespace debug_parse {

const Mi* Mi::get(const std::string& key) const {
  for (auto& [k, v] : items)
    if (k == key) return &v;
  return nullptr;
}

std::string mi_unescape(const std::string& s) {
  std::string out;
  std::size_t i = s.size() >= 2 && s.front() == '"' ? 1 : 0;
  const std::size_t end = i == 1 && s.back() == '"' ? s.size() - 1 : s.size();
  for (; i < end; ++i) {
    if (s[i] != '\\' || i + 1 >= end) {
      out += s[i];
      continue;
    }
    const char c = s[++i];
    switch (c) {
      case 'n': out += '\n'; break;
      case 't': out += '\t'; break;
      case 'r': break;
      case '"': out += '"'; break;
      case '\\': out += '\\'; break;
      default: out += c; break;
    }
  }
  return out;
}

namespace {
Mi parse_value(const std::string& s, std::size_t& i);

void parse_results(const std::string& s, std::size_t& i, Mi& out, char close) {
  while (i < s.size() && s[i] != close) {
    if (s[i] == ',') {
      ++i;
      continue;
    }
    // key=value, or a bare value inside a list
    std::size_t k = i;
    while (k < s.size() && (std::isalnum(static_cast<unsigned char>(s[k])) || s[k] == '-' || s[k] == '_')) ++k;
    if (k < s.size() && s[k] == '=' && k > i) {
      std::string key = s.substr(i, k - i);
      i = k + 1;
      out.items.emplace_back(std::move(key), parse_value(s, i));
    } else {
      out.items.emplace_back(std::string(), parse_value(s, i));
    }
  }
  if (i < s.size() && s[i] == close) ++i;
}

Mi parse_value(const std::string& s, std::size_t& i) {
  Mi v;
  if (i >= s.size()) return v;
  if (s[i] == '"') {
    std::size_t j = i + 1;
    while (j < s.size() && s[j] != '"') {
      if (s[j] == '\\') ++j;
      ++j;
    }
    v.str = mi_unescape(s.substr(i, j + 1 - i));
    i = std::min(s.size(), j + 1);
  } else if (s[i] == '{') {
    ++i;
    parse_results(s, i, v, '}');
  } else if (s[i] == '[') {
    ++i;
    parse_results(s, i, v, ']');
  } else {
    std::size_t j = i;
    while (j < s.size() && s[j] != ',' && s[j] != '}' && s[j] != ']') ++j;
    v.str = s.substr(i, j - i);
    i = j;
  }
  return v;
}
} // namespace

Mi parse_mi(const std::string& results) {
  Mi out;
  std::size_t i = 0;
  parse_results(results, i, out, '\0');
  return out;
}

bool cdb_frame(const std::string& line, DebugFrame& out) {
  // "00 0000003b`9cfdf6e8 00007ff6`1a2b3c4d app!MainWindow::greet+0x2c [C:\src\mainwindow.cpp @ 14]"
  static const std::regex re(R"(^\s*[0-9a-fA-F]{2,}\s+\S+\s+\S+\s+(\S+?)(?:\+0x[0-9a-fA-F]+)?\s*(?:\[(.+) @ (\d+)\])?\s*$)");
  std::smatch m;
  if (!std::regex_match(line, m, re)) return false;
  std::string fn = m[1].str();
  if (const auto bang = fn.find('!'); bang != std::string::npos) fn = fn.substr(bang + 1);
  out.function = fn;
  out.file = m[2].matched ? m[2].str() : std::string();
  out.line = m[3].matched ? std::stoi(m[3].str()) : 0;
  return true;
}

bool cdb_local(const std::string& line, DebugVar& out) {
  // "class std::basic_string<char,...> name = "Ada""   /   "int count = 0n42"
  const auto eq = line.find(" = ");
  if (eq == std::string::npos) return false;
  std::string left = line.substr(0, eq);
  while (!left.empty() && std::isspace(static_cast<unsigned char>(left.front()))) left.erase(left.begin());
  const auto sp = left.find_last_of(' ');
  if (sp == std::string::npos) return false;
  out.type = left.substr(0, sp);
  out.name = left.substr(sp + 1);
  for (const char* prefix : {"class ", "struct "})
    if (out.type.rfind(prefix, 0) == 0) out.type.erase(0, std::char_traits<char>::length(prefix));
  std::string v = line.substr(eq + 3);
  if (v.rfind("0n", 0) == 0) v.erase(0, 2); // cdb writes decimals as 0n42
  out.value = v;
  return !out.name.empty();
}

bool is_mi_record(const std::string& line) {
  std::size_t i = 0;
  while (i < line.size() && std::isdigit(static_cast<unsigned char>(line[i]))) ++i; // an optional token
  if (i >= line.size()) return false;
  const char c = line[i];
  if (c == '~' || c == '@' || c == '&') return line.size() > i + 1 && line[i + 1] == '"';
  if (c != '^' && c != '*' && c != '+' && c != '=') return false;
  // ^done / *stopped / =thread-group-added ... : a lower-case class name follows
  return line.size() > i + 1 && std::islower(static_cast<unsigned char>(line[i + 1]));
}

bool cdb_noise(const std::string& line) {
  static const std::regex re(
      R"(^(ModLoad: |Breakpoint \d+ hit|\([0-9a-f]+\.[0-9a-f]+\): |First chance exceptions|This exception may be expected|)"
      R"(\*\*\* |WARNING: |Unable to |NatVis script|quit:|[re]?[a-d]x=|r(ip|sp|bp|si|di|\d+)=|iopl=|cs=|)"
      R"([0-9a-f]{8}`[0-9a-f]{8}|\s*>?\s*\d+: |[\w.]+![^ ]+:$))");
  return line.empty() || std::regex_search(line, re);
}

} // namespace debug_parse

namespace {

using debug_parse::Mi;

std::string mi_quote(const std::string& s) {
  std::string out = "\"";
  for (char c : s) {
    if (c == '"' || c == '\\') out += '\\';
    out += c;
  }
  return out + "\"";
}

std::string forward_slashes(std::string p) {
  std::replace(p.begin(), p.end(), '\\', '/');
  return p;
}

// ═════════════════════════════════════════════════════════════════════════════
// GDB (machine interface)
// ═════════════════════════════════════════════════════════════════════════════
class GdbDebugger final : public Debugger {
public:
  explicit GdbDebugger(std::string gdb) : gdb_(std::move(gdb)) {}
  std::string name() const override { return "GDB"; }

  bool start(const std::string& exe, const std::string& cwd, const Breakpoints& bps, const std::map<std::string, std::string>& env,
             bool replace_env) override {
    if (!launch(Process::quote(gdb_) + " --interpreter=mi2 --quiet --nx " + Process::quote(exe), cwd, env, replace_env, false))
      return false;
    cmd("-gdb-set breakpoint pending on");
    cmd("-gdb-set print pretty off");
    for (auto& [file, lines] : bps) set_breakpoints(file, lines);
    cmd("-exec-run");
    return true;
  }

  void set_breakpoints(const std::string& file, const std::vector<int>& lines) override {
    auto& ids = bp_ids_[file];
    if (!ids.empty()) {
      std::string del = "-break-delete";
      for (auto& id : ids) del += " " + id;
      cmd(del);
      ids.clear();
    }
    for (int l : lines)
      cmd("-break-insert -f " + mi_quote(forward_slashes(file) + ":" + std::to_string(l)), [this, file](const std::string& cls, const Mi& r) {
        if (cls != "done") return;
        if (const Mi* b = r.get("bkpt")) bp_ids_[file].push_back(b->s("number"));
      });
  }

  void resume() override { run_cmd("-exec-continue"); }
  void step_over() override { run_cmd("-exec-next"); }
  void step_into() override { run_cmd("-exec-step"); }
  void step_out() override { run_cmd("-exec-finish"); }
  void pause() override { cmd("-exec-interrupt"); }
  void stop() override {
    if (!proc_) return;
    cmd("kill");
    cmd("-gdb-exit");
    auto p = proc_;
    pulseui::App::after(std::chrono::milliseconds(1500), [p] { p->kill(); }).release();
  }
  void select_frame(int index) override {
    frame = index;
    cmd("-stack-select-frame " + std::to_string(index));
    list_locals();
  }

private:
  using Done = std::function<void(const std::string& cls, const Mi& results)>;

  void cmd(const std::string& c, Done done = {}) {
    const int token = next_token_++;
    if (done) pending_[token] = std::move(done);
    send_raw(std::to_string(token) + c + "\n");
  }
  void run_cmd(const std::string& c) {
    if (state.get() != Paused) return;
    cmd(c);
  }

  void list_locals() {
    cmd("-stack-list-variables --simple-values", [this](const std::string& cls, const Mi& r) {
      std::vector<DebugVar> out;
      if (cls == "done")
        if (const Mi* vars = r.get("variables"))
          for (auto& [k, v] : vars->items) out.push_back({v.s("name"), v.s("type"), v.get("value") ? v.s("value") : std::string("{...}")});
      locals = out;
    });
  }

  void on_text(const std::string& line) override {
    if (line.empty() || line.rfind("(gdb)", 0) == 0) return;
    const char c = line[0];
    if (c == '~' || c == '@' || c == '&') {
      std::string text = debug_parse::mi_unescape(line.substr(1));
      while (!text.empty() && text.back() == '\n') text.pop_back();
      if (c == '@') program_output.emit(text); // the target stream
      else if (c != '&') output.emit(text);    // '&' echoes our own commands
      return;
    }
    if (!debug_parse::is_mi_record(line)) { // the program shares gdb's stdout
      std::string text = line;
      if (!text.empty() && text.back() == '\r') text.pop_back();
      program_output.emit(text);
      return;
    }
    // [token]^class,results  /  *async,results  /  =notify,results
    std::size_t i = 0;
    while (i < line.size() && std::isdigit(static_cast<unsigned char>(line[i]))) ++i;
    const int token = i > 0 ? std::stoi(line.substr(0, i)) : -1;
    if (i >= line.size()) return;
    const char kind = line[i];
    const std::string rest = line.substr(i + 1);
    const auto comma = rest.find(',');
    const std::string cls = rest.substr(0, comma);
    const Mi results = debug_parse::parse_mi(comma == std::string::npos ? std::string() : rest.substr(comma + 1));
    if (kind == '^') {
      auto it = pending_.find(token);
      if (it != pending_.end()) {
        Done fn = std::move(it->second);
        pending_.erase(it);
        fn(cls, results);
      }
      if (cls == "error") output.emit("gdb: " + results.s("msg"));
      if (cls == "exit") on_exit(0);
      return;
    }
    if (kind == '*' && cls == "running") {
      state = Running;
      stack = std::vector<DebugFrame>{};
      locals = std::vector<DebugVar>{};
      return;
    }
    if (kind == '*' && cls == "stopped") {
      const std::string reason = results.s("reason");
      if (reason.rfind("exited", 0) == 0) {
        state = Exited;
        stack = std::vector<DebugFrame>{};
        locals = std::vector<DebugVar>{};
        output.emit("[program exited" + (results.get("exit-code") ? " with code " + results.s("exit-code") : std::string()) + "]");
        cmd("-gdb-exit");
        return;
      }
      state = Paused;
      frame = 0;
      cmd("-stack-list-frames 0 60", [this](const std::string& cls2, const Mi& r) {
        std::vector<DebugFrame> out;
        if (cls2 == "done")
          if (const Mi* st = r.get("stack"))
            for (auto& [k, f] : st->items) {
              std::string file = f.s("fullname");
              if (file.empty()) file = f.s("file");
              out.push_back({f.s("func"), file, f.get("line") ? std::atoi(f.s("line").c_str()) : 0});
            }
        stack = out;
      });
      list_locals();
    }
  }

  std::string gdb_;
  int next_token_ = 1;
  std::map<int, Done> pending_;
  std::map<std::string, std::vector<std::string>> bp_ids_;
};

// ═════════════════════════════════════════════════════════════════════════════
// CDB (Windows SDK debugger, for MSVC builds)
// ═════════════════════════════════════════════════════════════════════════════
class CdbDebugger final : public Debugger {
public:
  explicit CdbDebugger(std::string cdb) : cdb_(std::move(cdb)) {}
  std::string name() const override { return "CDB"; }
  bool can_pause() const override { return false; }

  bool start(const std::string& exe, const std::string& cwd, const Breakpoints& bps, const std::map<std::string, std::string>& env,
             bool replace_env) override {
    // -G: no stop at the final breakpoint (the session ends with the program)
    if (!launch(Process::quote(cdb_) + " -lines -G " + Process::quote(exe), cwd, env, replace_env, true)) return false;
    // runs at the initial breakpoint (the first prompt)
    queue(".lines -e");
    queue("l+t");  // step by source line
    queue("l-s");  // don't echo source lines
    queue(".symopt+ 0x40"); // load symbols of the program eagerly
    for (auto& [file, lines] : bps) set_breakpoints(file, lines);
    go("g");
    return true;
  }

  void set_breakpoints(const std::string& file, const std::vector<int>& lines) override {
    if (state.get() == Running) return; // cdb only reads commands when the program is stopped
    auto& ids = bp_ids_[file];
    for (int id : ids) queue("bc " + std::to_string(id));
    ids.clear();
    const std::string base = fs::path(file).filename().string();
    for (int l : lines) {
      const int id = next_bp_++;
      ids.push_back(id);
      queue("bu" + std::to_string(id) + " `" + base + ":" + std::to_string(l) + "`");
    }
  }

  void resume() override { go("g"); }
  void step_over() override { go("p"); }
  void step_into() override { go("t"); }
  void step_out() override { go("gu"); }
  void pause() override {}
  void stop() override {
    if (!proc_) return;
    if (state.get() == Paused || ready_) send_raw("q\n");
    auto p = proc_;
    pulseui::App::after(std::chrono::milliseconds(800), [p] { p->kill(); }).release();
  }
  void select_frame(int index) override {
    frame = index;
    queue(".frame " + std::to_string(index));
    list_locals();
  }

private:
  struct Cmd {
    std::string text;
    std::function<void(const std::string&)> done;
    bool runs = false; // resumes the program (g, p, t, gu)
  };

  void queue(std::string text, std::function<void(const std::string&)> done = {}, bool runs = false) {
    cmds_.push_back({std::move(text), std::move(done), runs});
    pump();
  }
  void go(const char* c) {
    if (state.get() == Running) return;
    queue(c, [this](const std::string& out) { stopped(out); }, true);
  }
  void pump() {
    if (!ready_ || cmds_.empty() || busy_) return;
    busy_ = true;
    reply_.clear();
    if (cmds_.front().runs) state = Running;
    send_raw(cmds_.front().text + "\n");
  }

  void list_locals() {
    queue("dv /t", [this](const std::string& out) {
      std::vector<DebugVar> vars;
      std::size_t a = 0;
      while (a < out.size()) {
        auto b = out.find('\n', a);
        if (b == std::string::npos) b = out.size();
        DebugVar v;
        if (debug_parse::cdb_local(out.substr(a, b - a), v)) vars.push_back(v);
        a = b + 1;
      }
      locals = vars;
    });
  }

  void stopped(const std::string& out) {
    if (out.find("No runnable debuggees") != std::string::npos || out.find("has exited") != std::string::npos) {
      state = Exited;
      send_raw("q\n");
      return;
    }
    state = Paused;
    frame = 0;
    queue("kn 60", [this](const std::string& o) {
      std::vector<DebugFrame> frames;
      std::size_t a = 0;
      while (a < o.size()) {
        auto b = o.find('\n', a);
        if (b == std::string::npos) b = o.size();
        DebugFrame f;
        if (debug_parse::cdb_frame(o.substr(a, b - a), f)) frames.push_back(f);
        a = b + 1;
      }
      // the program's own frames first: skip system frames above them
      stack = frames;
    });
    list_locals();
  }

  void on_text(const std::string& chunk) override {
    buf_ += chunk;
    // complete lines go to the reply / the console
    for (;;) {
      const auto nl = buf_.find('\n');
      if (nl == std::string::npos) break;
      std::string line = buf_.substr(0, nl);
      if (!line.empty() && line.back() == '\r') line.pop_back();
      buf_.erase(0, nl + 1);
      if (busy_) reply_ += line + "\n";
      if (state.get() == Running) { // live: the program's lines, or cdb's own events
        if (debug_parse::cdb_noise(line)) output.emit(line);
        else program_output.emit(line);
      } else if (!busy_) {
        output.emit(line);
      }
    }
    // the prompt ("0:000> ") has no newline: cdb waits for a command
    static const std::regex prompt(R"(^\d+:\d+(:\w+)?> $)");
    if (std::regex_match(buf_, prompt)) {
      buf_.clear();
      const bool first = !ready_;
      ready_ = true;
      if (busy_ && !cmds_.empty()) {
        Cmd c = std::move(cmds_.front());
        cmds_.erase(cmds_.begin());
        busy_ = false;
        if (c.done) c.done(reply_);
      } else if (first) {
        state = Starting;
      }
      pump();
    }
  }

  std::string cdb_;
  std::string buf_, reply_;
  std::vector<Cmd> cmds_;
  bool ready_ = false, busy_ = false;
  std::map<std::string, std::vector<int>> bp_ids_;
  int next_bp_ = 1;
};

// ═════════════════════════════════════════════════════════════════════════════
// LLDB through the Debug Adapter Protocol (lldb-dap)
// ═════════════════════════════════════════════════════════════════════════════
class DapDebugger final : public Debugger {
public:
  explicit DapDebugger(std::string adapter) : adapter_(std::move(adapter)) {}
  std::string name() const override { return "LLDB"; }

  bool start(const std::string& exe, const std::string& cwd, const Breakpoints& bps, const std::map<std::string, std::string>& env,
             bool replace_env) override {
    if (!launch(Process::quote(adapter_), cwd, env, replace_env, true)) return false;
    pending_bps_ = bps;
    using pulseui::json::Value;
    Value init = Value::object();
    init["adapterID"] = "pulse-layer";
    init["clientName"] = "Pulse Layer";
    init["linesStartAt1"] = true;
    init["columnsStartAt1"] = true;
    init["pathFormat"] = "path";
    request("initialize", init, [this, exe, cwd](const Value&) {
      Value l = Value::object();
      l["program"] = exe;
      l["cwd"] = cwd;
      l["stopOnEntry"] = false;
      request("launch", l);
    });
    return true;
  }

  void set_breakpoints(const std::string& file, const std::vector<int>& lines) override {
    using pulseui::json::Value;
    if (!configured_) {
      pending_bps_[file] = lines;
      return;
    }
    Value a = Value::object();
    a["source"]["path"] = file;
    Value list = Value::array();
    for (int l : lines) {
      Value b = Value::object();
      b["line"] = l;
      list.push_back(b);
    }
    a["breakpoints"] = list;
    request("setBreakpoints", a);
  }

  void resume() override { thread_cmd("continue"); }
  void step_over() override { thread_cmd("next"); }
  void step_into() override { thread_cmd("stepIn"); }
  void step_out() override { thread_cmd("stepOut"); }
  void pause() override {
    pulseui::json::Value a = pulseui::json::Value::object();
    a["threadId"] = thread_;
    request("pause", a);
  }
  void stop() override {
    pulseui::json::Value a = pulseui::json::Value::object();
    a["terminateDebuggee"] = true;
    request("disconnect", a);
    auto p = proc_;
    if (p) pulseui::App::after(std::chrono::milliseconds(1500), [p] { p->kill(); }).release();
  }
  void select_frame(int index) override {
    frame = index;
    if (index >= 0 && index < static_cast<int>(frame_ids_.size())) list_locals(frame_ids_[static_cast<std::size_t>(index)]);
  }

private:
  using Value = pulseui::json::Value;
  using Done = std::function<void(const Value& body)>;

  void request(const std::string& command, const Value& args, Done done = {}) {
    Value m = Value::object();
    const int seq = seq_++;
    m["seq"] = seq;
    m["type"] = "request";
    m["command"] = command;
    m["arguments"] = args;
    if (done) pending_[seq] = std::move(done);
    const std::string body = m.dump(0);
    send_raw("Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body);
  }
  void thread_cmd(const char* c) {
    if (state.get() != Paused) return;
    Value a = Value::object();
    a["threadId"] = thread_;
    state = Running;
    request(c, a);
  }

  void list_locals(int frame_id) {
    Value a = Value::object();
    a["frameId"] = frame_id;
    request("scopes", a, [this](const Value& body) {
      for (auto& s : body["scopes"].items()) {
        if (s["name"].as_string() != "Locals" && s["presentationHint"].as_string() != "locals") continue;
        Value v = Value::object();
        v["variablesReference"] = s["variablesReference"].as_number();
        request("variables", v, [this](const Value& b) {
          std::vector<DebugVar> out;
          for (auto& x : b["variables"].items()) out.push_back({x["name"].as_string(), x["type"].as_string(), x["value"].as_string()});
          locals = out;
        });
        return;
      }
      locals = std::vector<DebugVar>{};
    });
  }

  void message(const Value& m) {
    const std::string type = m["type"].as_string();
    if (type == "response") {
      auto it = pending_.find(static_cast<int>(m["request_seq"].as_number()));
      if (!m["success"].as_bool(true)) output.emit("lldb: " + m["message"].as_string());
      if (it != pending_.end()) {
        Done fn = std::move(it->second);
        pending_.erase(it);
        if (m["success"].as_bool(true)) fn(m["body"]);
      }
      return;
    }
    if (type != "event") return;
    const std::string ev = m["event"].as_string();
    const Value& body = m["body"];
    if (ev == "initialized") {
      configured_ = true;
      for (auto& [file, lines] : pending_bps_) set_breakpoints(file, lines);
      pending_bps_.clear();
      request("configurationDone", Value::object());
      state = Running;
    } else if (ev == "stopped") {
      state = Paused;
      frame = 0;
      thread_ = static_cast<int>(body["threadId"].as_number(thread_));
      Value a = Value::object();
      a["threadId"] = thread_;
      a["levels"] = 60;
      request("stackTrace", a, [this](const Value& b) {
        std::vector<DebugFrame> out;
        frame_ids_.clear();
        for (auto& f : b["stackFrames"].items()) {
          out.push_back({f["name"].as_string(), f["source"]["path"].as_string(), static_cast<int>(f["line"].as_number())});
          frame_ids_.push_back(static_cast<int>(f["id"].as_number()));
        }
        stack = out;
        if (!frame_ids_.empty()) list_locals(frame_ids_.front());
      });
    } else if (ev == "continued") {
      state = Running;
    } else if (ev == "exited" || ev == "terminated") {
      if (ev == "exited") output.emit("[program exited with code " + std::to_string(static_cast<int>(body["exitCode"].as_number())) + "]");
      state = Exited;
      stack = std::vector<DebugFrame>{};
      locals = std::vector<DebugVar>{};
    } else if (ev == "output") {
      std::string text = body["output"].as_string();
      while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
      if (text.empty()) return;
      const std::string cat = body["category"].as_string();
      if (cat == "stdout" || cat == "stderr") program_output.emit(text);
      else output.emit(text);
    }
  }

  void on_text(const std::string& chunk) override {
    buf_ += chunk;
    for (;;) {
      const auto hdr_end = buf_.find("\r\n\r\n");
      if (hdr_end == std::string::npos) return;
      const auto cl = buf_.find("Content-Length:");
      if (cl == std::string::npos || cl > hdr_end) {
        buf_.erase(0, hdr_end + 4);
        continue;
      }
      const std::size_t len = static_cast<std::size_t>(std::strtoul(buf_.c_str() + cl + 15, nullptr, 10));
      if (buf_.size() < hdr_end + 4 + len) return; // the body is still coming
      const std::string body = buf_.substr(hdr_end + 4, len);
      buf_.erase(0, hdr_end + 4 + len);
      if (auto m = Value::parse(body)) message(*m);
    }
  }

  std::string adapter_;
  std::string buf_;
  int seq_ = 1, thread_ = 1;
  bool configured_ = false;
  std::map<int, Done> pending_;
  Breakpoints pending_bps_;
  std::vector<int> frame_ids_;
};

} // namespace

std::unique_ptr<Debugger> Debugger::create(const DebuggerTool& tool) {
  switch (tool.kind) {
    case DebuggerTool::Kind::Gdb: return std::make_unique<GdbDebugger>(tool.path);
    case DebuggerTool::Kind::Cdb: return std::make_unique<CdbDebugger>(tool.path);
    case DebuggerTool::Kind::LldbDap: return std::make_unique<DapDebugger>(tool.path);
    default: return nullptr;
  }
}

} // namespace layer
