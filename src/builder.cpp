#include "builder.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>

namespace fs = std::filesystem;

namespace layer {

namespace {

std::string read_file(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

// Writes only when the content changed, so unchanged files do not trigger rebuilds.
void write_if_changed(const fs::path& p, const std::string& content) {
  std::error_code ec;
  if (fs::exists(p, ec) && read_file(p) == content) return;
  fs::create_directories(p.parent_path(), ec);
  std::ofstream out(p, std::ios::binary);
  out << content;
}

std::string cmake_path(const std::string& p) {
  std::string s = p;
  for (auto& c : s)
    if (c == '\\') c = '/';
  return s;
}

pulseui::Diagnostic::Severity severity_of(const std::string& s) {
  if (s.find("warning") != std::string::npos) return pulseui::Diagnostic::Warning;
  if (s.find("note") != std::string::npos) return pulseui::Diagnostic::Note;
  return pulseui::Diagnostic::Error;
}

} // namespace

bool parse_issue(const std::string& line, Issue& out) {
  // MSVC: D:\x\main.cpp(12,5): error C2065: 'foo': undeclared identifier
  static const std::regex msvc(R"(^\s*(.+?)\((\d+)(?:,(\d+))?\)\s*:\s*(fatal error|error|warning)\s+\w+\s*:\s*(.*)$)");
  // GCC / Clang: /x/main.cpp:12:5: error: 'foo' was not declared in this scope
  static const std::regex gnu(R"(^(.+?):(\d+):(\d+):\s*(fatal error|error|warning|note):\s*(.*)$)");
  static const std::regex gnu_noline(R"(^(.+?):(\d+):\s*(fatal error|error|warning):\s*(.*)$)");
  // Linkers: main.cpp.obj : error LNK2019: ... / ld: ... undefined reference to `x'
  static const std::regex lnk(R"(^(.*?)\s*:\s*(fatal error|error)\s+(LNK\d+)\s*:\s*(.*)$)");
  static const std::regex cmake_err(R"(^CMake Error at (.+?):(\d+).*$)");
  std::smatch m;
  out = Issue{};
  if (std::regex_match(line, m, msvc)) {
    out.file = m[1];
    out.line = std::stoi(m[2]);
    out.column = m[3].matched ? std::stoi(m[3]) : 0;
    out.severity = severity_of(m[4]);
    out.message = m[5];
  } else if (std::regex_match(line, m, gnu)) {
    out.file = m[1];
    out.line = std::stoi(m[2]);
    out.column = std::stoi(m[3]);
    out.severity = severity_of(m[4]);
    out.message = m[5];
  } else if (std::regex_match(line, m, gnu_noline)) {
    out.file = m[1];
    out.line = std::stoi(m[2]);
    out.severity = severity_of(m[3]);
    out.message = m[4];
  } else if (std::regex_match(line, m, lnk)) {
    out.file = m[1];
    out.message = std::string(m[3]) + ": " + std::string(m[4]);
  } else if (line.find("undefined reference to") != std::string::npos ||
             line.find("Undefined symbols") != std::string::npos) {
    out.message = line;
  } else if (std::regex_match(line, m, cmake_err)) {
    out.file = m[1];
    out.line = std::stoi(m[2]);
    out.message = "CMake error";
  } else {
    return false;
  }
  const std::string f = cmake_path(out.file);
  out.user_code = f.size() >= 8 && f.compare(f.size() - 8, 8, "main.cpp") == 0;
  return true;
}

Builder::Builder() = default;

Builder::~Builder() {
  *alive_ = false;
  if (proc_) proc_->kill();
}

std::string Builder::executable() const {
  const fs::path b = fs::path(project_dir_) / "build";
#if defined(_WIN32)
  return (b / "app.exe").string();
#elif defined(__APPLE__)
  return (b / "app.app" / "Contents" / "MacOS" / "app").string();
#else
  return (b / "app").string();
#endif
}

void Builder::launch(const std::string& command, std::map<std::string, std::string> env, bool replace_env,
                     std::function<void(int)> done) {
  output.emit("> " + command);
  std::weak_ptr<bool> alive = alive_;
  Process::Options opt;
  opt.command = command;
  opt.env = std::move(env);
  opt.replace_env = replace_env;
  opt.cwd = project_dir_;
  proc_ = Process::start(
      opt,
      [this, alive](const std::string& line) {
        pulseui::App::post([this, alive, line] {
          if (!alive.expired()) on_line(line);
        });
      },
      [this, alive, done](int code) {
        pulseui::App::post([this, alive, done, code] {
          if (alive.expired()) return;
          proc_.reset();
          done(code);
        });
      });
  if (!proc_) {
    output.emit("failed to start: " + command);
    done(-1);
  }
}

void Builder::on_line(const std::string& line) {
  output.emit(line);
  Issue is;
  // errors from anywhere, warnings/notes only from the user's code (not from PulseUI)
  if (state.get() != Running && parse_issue(line, is)) {
    // map the copy in build/src back to the project file
    const std::string base = fs::path(is.file).filename().string();
    is.user_code = !is.file.empty() &&
                   std::find(req_.user_files.begin(), req_.user_files.end(), base) != req_.user_files.end();
    if (is.user_code) is.file = base;
  }
  // errors from anywhere, warnings/notes only from the user's code (not from PulseUI)
  if (state.get() != Running && !is.message.empty() && (is.user_code || is.severity == pulseui::Diagnostic::Error)) {
    auto list = issues.get();
    if (std::find(list.begin(), list.end(), is) == list.end()) {
      list.push_back(is);
      issues = list;
    }
  }
}

bool Builder::build(const Request& r, bool run_after) {
  if (state.get() != Idle) return false;
  req_ = r;
  run_after_ = run_after;
  proc_killed_note_ = false;
  project_dir_ = r.project_dir;
  issues = std::vector<Issue>{};
  if (r.kit.cxx.empty()) {
    note.emit("build.no_kit", "");
    return false;
  }
  if (!r.cmake.found()) {
    note.emit("build.no_cmake", "");
    return false;
  }
  std::error_code ec;
  if (!fs::exists(fs::path(r.pulseui_dir) / "CMakeLists.txt", ec)) {
    note.emit("build.no_pulseui", r.pulseui_dir);
    return false;
  }

  const fs::path src = fs::path(project_dir_) / "src";
  std::string sources;
  for (auto& [name, text] : r.files) {
    const std::string ext = fs::path(name).extension().string();
    if (ext == ".cpp" || ext == ".cc" || ext == ".cxx") sources += (sources.empty() ? "" : " ") + name;
  }
  if (sources.empty()) sources = "main.cpp";
  // files removed from the project must not linger in the generated sources
  std::error_code rm_ec;
  if (fs::is_directory(src, rm_ec))
    for (auto& e : fs::directory_iterator(src, rm_ec)) {
      const std::string n = e.path().filename().string();
      if (n == "CMakeLists.txt") continue;
      if (std::none_of(r.files.begin(), r.files.end(), [&](const auto& f) { return f.first == n; })) fs::remove(e.path(), rm_ec);
    }
  write_if_changed(src / "CMakeLists.txt",
                   "# Generated by Pulse Layer\n"
                   "cmake_minimum_required(VERSION 3.21)\n"
                   "project(PulseLayerApp CXX)\n"
                   "set(CMAKE_CXX_STANDARD 20)\n"
                   "set(PULSEUI_BUILD_EXAMPLES OFF CACHE BOOL \"\" FORCE)\n"
                   "set(PULSEUI_BUILD_TESTS OFF CACHE BOOL \"\" FORCE)\n"
                   "add_subdirectory(\"" + cmake_path(r.pulseui_dir) + "\" pulseui)\n"
                   "pulseui_add_app(app " + sources + ")\n"
                   "target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})\n"
                   "if(MSVC)\n  target_compile_options(app PRIVATE /diagnostics:column)\nendif()\n");
  for (auto& [name, text] : r.files) write_if_changed(src / name, text);

  env_ = kit_environment(r.kit, replace_env_);
  started_ = std::chrono::steady_clock::now();

  // A different kit needs a fresh CMake cache.
  const fs::path build_dir = fs::path(project_dir_) / "build";
  const fs::path stamp = build_dir / "pulselayer-kit.txt";
  const bool configured = fs::exists(build_dir / "CMakeCache.txt", ec) && read_file(stamp) == r.kit.id;
  if (!configured) {
    fs::remove_all(build_dir, ec);
    fs::create_directories(build_dir, ec);
    write_if_changed(stamp, r.kit.id);
    note.emit("build.first_time", "");
    step_configure();
  } else {
    step_build();
  }
  return true;
}

void Builder::step_configure() {
  state = Configuring;
  const fs::path src = fs::path(project_dir_) / "src";
  const fs::path build_dir = fs::path(project_dir_) / "build";
  std::string cmd = Process::quote(req_.cmake.path) + " -S " + Process::quote(src.string()) + " -B " +
                    Process::quote(build_dir.string()) + " -DCMAKE_BUILD_TYPE=Debug";
  if (req_.ninja.found()) {
    cmd += " -G Ninja -DCMAKE_MAKE_PROGRAM=" + Process::quote(cmake_path(req_.ninja.path));
  } else {
#ifdef _WIN32
    cmd += req_.kit.kind == Kit::Kind::MinGW ? " -G \"MinGW Makefiles\"" : " -G \"NMake Makefiles\"";
#endif
  }
  cmd += " -DCMAKE_CXX_COMPILER=" + Process::quote(cmake_path(req_.kit.cxx));
  launch(cmd, env_, replace_env_, [this](int code) {
    if (code != 0) return finish_build(false);
    step_build();
  });
}

void Builder::step_build() {
  state = Building;
  const fs::path build_dir = fs::path(project_dir_) / "build";
  launch(Process::quote(req_.cmake.path) + " --build " + Process::quote(build_dir.string()), env_, replace_env_,
         [this](int code) { finish_build(code == 0); });
}

void Builder::finish_build(bool ok) {
  last_ok_ = ok;
  state = Idle;
  const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - started_).count();
  if (ok) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.1f", secs);
    note.emit("build.ok", buf);
    if (run_after_) run();
  } else {
    int errors = 0;
    for (auto& i : issues.get()) errors += i.severity == pulseui::Diagnostic::Error;
    note.emit(proc_killed_note_ ? "build.stopped" : "build.failed", std::to_string(errors));
  }
}

bool Builder::run() {
  if (state.get() != Idle) return false;
  std::error_code ec;
  if (!fs::exists(executable(), ec)) return false;
  state = Running;
  proc_killed_note_ = false;
  note.emit("run.started", "");
  launch(Process::quote(executable()), env_, replace_env_, [this](int code) {
    state = Idle;
    note.emit("run.exit", std::to_string(code));
  });
  return true;
}

void Builder::stop() {
  if (proc_) {
    proc_killed_note_ = true;
    proc_->kill();
  }
}

} // namespace layer
