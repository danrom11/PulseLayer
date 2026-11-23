#include "toolchain.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <set>
#include <sstream>

#include <pulseui/core/json.hpp>

#include "process.hpp"

namespace fs = std::filesystem;

namespace layer {

namespace {

#ifdef _WIN32
constexpr char kPathSep = ';';
constexpr const char* kExe = ".exe";
#else
constexpr char kPathSep = ':';
constexpr const char* kExe = "";
#endif

std::string env(const char* name) {
  const char* v = std::getenv(name);
  return v ? v : "";
}

bool file_exists(const fs::path& p) {
  std::error_code ec;
  return fs::exists(p, ec) && !fs::is_directory(p, ec);
}

std::string canon(const fs::path& p) {
  std::error_code ec;
  auto c = fs::weakly_canonical(p, ec);
  return (ec ? p : c).string();
}

[[maybe_unused]] std::string cxx_driver_path(fs::path p, std::string_view prefix) {
  std::error_code ec;
  for (int hops = 0; hops < 16 && fs::is_symlink(p, ec); ++hops) {
    fs::path target = fs::read_symlink(p, ec);
    if (ec) break;
    if (target.is_relative()) target = p.parent_path() / target;
    if (target.filename().string().rfind(prefix, 0) != 0) break; // e.g. clang++ -> clang
    p = target;
  }
  auto parent = fs::weakly_canonical(p.parent_path(), ec);
  return ec ? p.string() : (parent / p.filename()).string();
}

std::vector<fs::path> path_dirs() {
  std::vector<fs::path> out;
  std::stringstream ss(env("PATH"));
  std::string d;
  while (std::getline(ss, d, kPathSep))
    if (!d.empty()) out.emplace_back(d);
  return out;
}

std::string trim(std::string s) {
  while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
  std::size_t i = 0;
  while (i < s.size() && (s[i] == ' ' || s[i] == '\n')) ++i;
  return s.substr(i);
}

std::string first_line(const std::string& s) { return trim(s.substr(0, s.find('\n'))); }

std::string query(const std::string& exe, const std::string& args) {
  std::string out;
  if (Process::run(Process::quote(exe) + " " + args, out, 10000) != 0) return {};
  return trim(out);
}

// "cmake version 3.30.3" -> "3.30.3"
std::string version_token(const std::string& line) {
  std::size_t i = line.find_first_of("0123456789");
  if (i == std::string::npos) return line;
  std::size_t j = i;
  while (j < line.size() && (std::isdigit(static_cast<unsigned char>(line[j])) || line[j] == '.')) ++j;
  return line.substr(i, j - i);
}

Tool find_tool(const std::string& name, const std::vector<fs::path>& extra, std::vector<std::string>& log) {
  std::vector<fs::path> candidates;
  for (auto& d : path_dirs()) candidates.push_back(d / (name + kExe));
  for (auto& e : extra) candidates.push_back(e);
  for (auto& c : candidates) {
    if (!file_exists(c)) continue;
    Tool t{canon(c), version_token(first_line(query(c.string(), "--version")))};
    if (t.version.empty()) continue;
    log.push_back(name + " " + t.version + ": " + t.path);
    return t;
  }
  log.push_back(name + ": not found");
  return {};
}

#ifdef _WIN32
std::vector<fs::path> drive_roots() {
  std::vector<fs::path> out;
  for (char d = 'C'; d <= 'Z'; ++d) {
    fs::path root = std::string(1, d) + ":\\";
    std::error_code ec;
    if (fs::exists(root, ec)) out.push_back(root);
  }
  return out;
}

std::vector<fs::path> glob_dirs(const fs::path& parent, const std::string& prefix) {
  std::vector<fs::path> out;
  std::error_code ec;
  if (!fs::is_directory(parent, ec)) return out;
  for (auto& e : fs::directory_iterator(parent, ec)) {
    const std::string n = e.path().filename().string();
    if (e.is_directory(ec) && n.rfind(prefix, 0) == 0) out.push_back(e.path());
  }
  std::sort(out.rbegin(), out.rend()); // newest first
  return out;
}

struct VsInstall {
  std::string path, name, version;
};

std::vector<VsInstall> vs_installs(std::vector<std::string>& log) {
  std::vector<VsInstall> out;
  fs::path vswhere = fs::path(env("ProgramFiles(x86)")) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe";
  if (!file_exists(vswhere)) {
    log.push_back("vswhere.exe: not found (no Visual Studio installer)");
    return out;
  }
  std::string json_text;
  Process::run(Process::quote(vswhere.string()) + " -all -products * -format json -utf8", json_text, 15000);
  auto j = pulseui::json::Value::parse(json_text);
  if (!j) return out;
  for (auto& inst : j->items())
    out.push_back({inst["installationPath"].as_string(), inst["displayName"].as_string("Visual Studio"),
                   inst["installationVersion"].as_string()});
  return out;
}
#endif

void add_kit(Toolchains& tc, std::set<std::string>& seen, Kit k) {
  if (!seen.insert(k.id).second) return;
  tc.log.push_back("kit: " + k.name);
  tc.kits.push_back(std::move(k));
}

} // namespace

const char* kind_name(Kit::Kind k) {
  switch (k) {
    case Kit::Kind::MSVC: return "MSVC";
    case Kit::Kind::ClangMSVC: return "Clang (MSVC)";
    case Kit::Kind::MinGW: return "MinGW";
    case Kit::Kind::Clang: return "Clang";
    case Kit::Kind::GCC: return "GCC";
    case Kit::Kind::AppleClang: return "Apple Clang";
  }
  return "";
}

Toolchains detect_toolchains() {
  Toolchains tc;
  std::set<std::string> seen;
  std::vector<fs::path> cmake_extra, ninja_extra;

#ifdef _WIN32
  // ── MSVC (and the Clang shipped with Visual Studio) ──
  std::string first_vcvars;
  for (auto& vs : vs_installs(tc.log)) {
    const fs::path build = fs::path(vs.path) / "VC" / "Auxiliary" / "Build";
    const fs::path vcvars = build / "vcvars64.bat";
    std::ifstream ver_file(build / "Microsoft.VCToolsVersion.default.txt");
    std::string tools;
    std::getline(ver_file, tools);
    tools = trim(tools);
    const fs::path bin = fs::path(vs.path) / "VC" / "Tools" / "MSVC" / tools / "bin" / "Hostx64" / "x64";
    cmake_extra.push_back(fs::path(vs.path) / "Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe");
    ninja_extra.push_back(fs::path(vs.path) / "Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe");
    if (tools.empty() || !file_exists(vcvars) || !file_exists(bin / "cl.exe")) {
      tc.log.push_back(vs.name + ": no C++ tools (install the 'Desktop development with C++' workload)");
      continue;
    }
    if (first_vcvars.empty()) first_vcvars = vcvars.string();
    Kit k;
    k.kind = Kit::Kind::MSVC;
    k.id = "msvc:" + vs.version + ":" + tools;
    k.name = vs.name + " — MSVC " + tools + " (x64)";
    k.cxx = (bin / "cl.exe").string();
    k.linker = (bin / "link.exe").string();
    k.version = tools;
    k.target = "x86_64-pc-windows-msvc";
    k.env_script = vcvars.string();
    add_kit(tc, seen, k);

    const fs::path vs_clang = fs::path(vs.path) / "VC" / "Tools" / "Llvm" / "x64" / "bin" / "clang++.exe";
    if (file_exists(vs_clang)) {
      Kit c = k;
      c.kind = Kit::Kind::ClangMSVC;
      c.cxx = vs_clang.string();
      c.linker = (vs_clang.parent_path() / "lld-link.exe").string();
      c.version = version_token(first_line(query(c.cxx, "--version")));
      c.id = "clang-msvc:" + canon(vs_clang);
      c.name = "Clang " + c.version + " (Visual Studio, x64)";
      add_kit(tc, seen, c);
    }
  }

  // ── MinGW ──
  std::vector<fs::path> mingw_bins;
  for (auto& d : path_dirs()) mingw_bins.push_back(d);
  for (auto& root : drive_roots()) {
    for (auto& q : {root / "Qt" / "Tools"})
      for (auto& m : glob_dirs(q, "mingw")) mingw_bins.push_back(m / "bin");
    for (auto sub : {"msys64/ucrt64/bin", "msys64/mingw64/bin", "msys64/clang64/bin", "MinGW/bin", "TDM-GCC-64/bin", "mingw64/bin"})
      mingw_bins.push_back(root / sub);
    cmake_extra.push_back(root / "Qt/Tools/CMake_64/bin/cmake.exe");
    ninja_extra.push_back(root / "Qt/Tools/Ninja/ninja.exe");
    cmake_extra.push_back(root / "Program Files/CMake/bin/cmake.exe");
    cmake_extra.push_back(root / "CMake/bin/cmake.exe");
  }
  for (auto& bin : mingw_bins) {
    const fs::path gxx = bin / "g++.exe";
    if (!file_exists(gxx)) continue;
    const std::string id = "mingw:" + canon(gxx);
    if (seen.count(id)) continue;
    Kit k;
    k.kind = Kit::Kind::MinGW;
    k.id = id;
    k.cxx = canon(gxx);
    k.version = query(k.cxx, "-dumpfullversion");
    if (k.version.empty()) k.version = query(k.cxx, "-dumpversion");
    if (k.version.empty()) continue;
    k.target = query(k.cxx, "-dumpmachine");
    k.linker = (bin / "ld.exe").string();
    k.bin_dir = canon(bin);
    k.name = "MinGW GCC " + k.version + " (" + k.target + ") — " + canon(bin.parent_path());
    add_kit(tc, seen, k);
  }

  // ── LLVM Clang (uses the MSVC environment for headers/libs) ──
  std::vector<fs::path> llvm_bins = path_dirs();
  for (auto& root : drive_roots()) {
    llvm_bins.push_back(root / "Program Files" / "LLVM" / "bin");
    llvm_bins.push_back(root / "LLVM" / "bin");
  }
  for (auto& bin : llvm_bins) {
    const fs::path clang = bin / "clang++.exe";
    if (!file_exists(clang) || first_vcvars.empty()) continue;
    const std::string id = "clang:" + canon(clang);
    if (seen.count(id) || file_exists(bin / "g++.exe")) continue; // MSYS2 clang64 is covered by MinGW
    Kit k;
    k.kind = Kit::Kind::ClangMSVC;
    k.id = id;
    k.cxx = canon(clang);
    k.version = version_token(first_line(query(k.cxx, "--version")));
    if (k.version.empty()) continue;
    k.linker = (bin / "lld-link.exe").string();
    k.target = "x86_64-pc-windows-msvc";
    k.env_script = first_vcvars;
    k.name = "LLVM Clang " + k.version + " (x64) — " + canon(bin.parent_path());
    add_kit(tc, seen, k);
  }
#else
  // ── Apple Clang / system compilers ──
  std::string xc = query("xcrun", "-f clang++");
  if (!xc.empty()) {
    Kit k;
    k.kind = Kit::Kind::AppleClang;
    k.cxx = first_line(xc);
    k.id = "appleclang:" + k.cxx;
    k.version = version_token(first_line(query(k.cxx, "--version")));
    k.linker = first_line(query("xcrun", "-f ld"));
    k.target = first_line(query(k.cxx, "-dumpmachine"));
    k.name = "Apple Clang " + k.version + " (" + k.target + ")";
    add_kit(tc, seen, k);
  }
  std::vector<fs::path> bins = path_dirs();
  for (auto d : {"/usr/bin", "/usr/local/bin", "/opt/homebrew/bin", "/opt/homebrew/opt/llvm/bin", "/usr/local/opt/llvm/bin"})
    bins.emplace_back(d);
  for (auto& bin : bins) {
    std::error_code ec;
    if (!fs::is_directory(bin, ec)) continue;
    for (auto& e : fs::directory_iterator(bin, ec)) {
      const std::string n = e.path().filename().string();
      const bool gcc = n == "g++" || n.rfind("g++-", 0) == 0;
      const bool clang = n == "clang++" || n.rfind("clang++-", 0) == 0;
      if (!gcc && !clang) continue;
      // Homebrew: clang++ -> clang -> clang-17. The driver mode comes from the
      // file name, and clang-17 is the C driver (it links without libc++), so
      // follow links only while the name still says clang++ / g++.
      const std::string path = cxx_driver_path(e.path(), clang ? "clang++" : "g++");
      const std::string ver_line = first_line(query(path, "--version"));
      if (ver_line.find("Apple") != std::string::npos) continue; // = Apple Clang above
      Kit k;
      k.kind = clang ? Kit::Kind::Clang : Kit::Kind::GCC;
      k.id = std::string(clang ? "clang:" : "gcc:") + path;
      if (seen.count(k.id)) continue;
      k.cxx = path;
      k.version = version_token(ver_line);
      if (k.version.empty()) continue;
      k.target = first_line(query(path, "-dumpmachine"));
      k.linker = "ld";
#ifdef __APPLE__
      if (clang) {
        std::error_code lec;
        const fs::path prefix = fs::weakly_canonical(fs::path(path).parent_path().parent_path(), lec);
        if (!lec && fs::exists(prefix / "lib" / "c++" / "libc++.1.dylib", lec)) k.libcxx_dir = (prefix / "lib" / "c++").string();
      }
#endif
      k.name = std::string(clang ? "Clang " : "GCC ") + k.version + " — " + path;
      add_kit(tc, seen, k);
    }
  }
  for (auto d : {"/opt/homebrew/bin", "/usr/local/bin", "/Applications/CMake.app/Contents/bin"}) {
    cmake_extra.push_back(fs::path(d) / "cmake");
    ninja_extra.push_back(fs::path(d) / "ninja");
  }
#endif

  tc.cmake = find_tool("cmake", cmake_extra, tc.log);
  tc.ninja = find_tool("ninja", ninja_extra, tc.log);
  if (tc.kits.empty()) tc.log.push_back("no C++ compilers found");
  return tc;
}

std::map<std::string, std::string> kit_environment(const Kit& kit, bool& replace_env) {
  replace_env = false;
  std::map<std::string, std::string> out;
#ifdef _WIN32
  if (!kit.env_script.empty()) {
    // Running vcvars64.bat takes a couple of seconds: capture its result once.
    static std::mutex m;
    static std::map<std::string, std::map<std::string, std::string>> cache;
    std::lock_guard<std::mutex> lock(m);
    auto it = cache.find(kit.env_script);
    if (it == cache.end()) {
      std::string text;
      Process::run("cmd.exe /d /s /c \"\"" + kit.env_script + "\" >nul && set\"", text, 60000);
      std::map<std::string, std::string> vars;
      std::stringstream ss(text);
      std::string line;
      while (std::getline(ss, line)) {
        const auto eq = line.find('=');
        if (eq == std::string::npos || eq == 0) continue;
        vars[line.substr(0, eq)] = trim(line.substr(eq + 1));
      }
      it = cache.emplace(kit.env_script, std::move(vars)).first;
    }
    if (!it->second.empty()) {
      replace_env = true;
      return it->second;
    }
  }
#endif
  if (!kit.bin_dir.empty()) out["PATH"] = kit.bin_dir + kPathSep + env("PATH");
  return out;
}

} // namespace layer

namespace layer {

std::string find_clang_format(const Toolchains& tc) {
  const std::string exe = std::string("clang-format") + kExe;
  std::vector<fs::path> candidates;
  for (auto& d : path_dirs()) candidates.push_back(d / exe);
  for (auto& k : tc.kits) {
    const fs::path cxx(k.cxx);
    candidates.push_back(cxx.parent_path() / exe); // LLVM / Homebrew clang kits
    if (!k.env_script.empty()) {                    // Visual Studio: VC/Auxiliary/Build/vcvars64.bat -> VC/Tools/Llvm
      const fs::path vc = fs::path(k.env_script).parent_path().parent_path().parent_path();
      candidates.push_back(vc / "Tools" / "Llvm" / "x64" / "bin" / exe);
      candidates.push_back(vc / "Tools" / "Llvm" / "bin" / exe);
    }
    // Qt: <Qt>/Tools/mingw*/bin/g++ -> <Qt>/Tools/QtCreator/bin/clang/bin
    for (fs::path p = cxx.parent_path(); !p.empty() && p != p.parent_path(); p = p.parent_path())
      if (p.filename() == "Tools") {
        candidates.push_back(p / "QtCreator" / "bin" / "clang" / "bin" / exe);
        break;
      }
  }
#ifdef _WIN32
  for (const char* var : {"ProgramFiles", "ProgramW6432", "ProgramFiles(x86)"})
    if (!env(var).empty()) candidates.push_back(fs::path(env(var)) / "LLVM" / "bin" / exe);
#else
  for (const char* d : {"/opt/homebrew/bin", "/usr/local/bin", "/opt/homebrew/opt/llvm/bin", "/usr/local/opt/llvm/bin",
                        "/usr/bin"})
    candidates.push_back(fs::path(d) / exe);
#endif
  for (auto& c : candidates)
    if (file_exists(c)) return canon(c);
  return {};
}

} // namespace layer

// ── debuggers ────────────────────────────────────────────────────────────────
#include "debugger.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace layer {

namespace {

// lldb-dap may be installed but unusable (a missing Python DLL): try it.
bool lldb_dap_works(const std::string& path) {
  std::string out;
  return Process::run(Process::quote(path) + " --help", out, 4000) == 0;
}

std::vector<fs::path> windows_kits_debuggers() {
  std::vector<fs::path> out;
#ifdef _WIN32
  wchar_t buf[MAX_PATH] = {};
  DWORD size = sizeof buf;
  if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows Kits\\Installed Roots", L"KitsRoot10",
                   RRF_RT_REG_SZ | RRF_SUBKEY_WOW6432KEY, nullptr, buf, &size) == ERROR_SUCCESS)
    out.push_back(fs::path(buf) / "Debuggers" / "x64" / "cdb.exe");
  for (const char* var : {"ProgramFiles(x86)", "ProgramFiles"})
    if (!env(var).empty()) out.push_back(fs::path(env(var)) / "Windows Kits" / "10" / "Debuggers" / "x64" / "cdb.exe");
#endif
  return out;
}

} // namespace

DebuggerTool find_debugger(const Kit& kit) {
  const fs::path dir = fs::path(kit.cxx).parent_path();
  auto first = [](const std::vector<fs::path>& c) -> std::string {
    for (auto& p : c)
      if (file_exists(p)) return canon(p);
    return {};
  };
  auto on_path = [](const std::string& exe) {
    std::vector<fs::path> c;
    for (auto& d : path_dirs()) c.push_back(d / exe);
    return c;
  };
  std::vector<fs::path> gdb{dir / (std::string("gdb") + kExe)};
  if (!kit.bin_dir.empty()) gdb.push_back(fs::path(kit.bin_dir) / (std::string("gdb") + kExe));
  for (auto& p : on_path(std::string("gdb") + kExe)) gdb.push_back(p);
  std::vector<fs::path> dap{dir / (std::string("lldb-dap") + kExe), dir / (std::string("lldb-vscode") + kExe)};
  for (auto& p : on_path(std::string("lldb-dap") + kExe)) dap.push_back(p);
#ifdef __APPLE__
  {
    std::string out;
    if (Process::run("xcrun -f lldb-dap", out, 5000) == 0 && !trim(out).empty()) dap.insert(dap.begin(), trim(out));
  }
#endif
  auto try_dap = [&]() -> DebuggerTool {
    const std::string p = first(dap);
    if (!p.empty() && lldb_dap_works(p)) return {DebuggerTool::Kind::LldbDap, p};
    return {};
  };
  switch (kit.kind) {
    case Kit::Kind::MSVC:
    case Kit::Kind::ClangMSVC: {
      // PDB debug info: the Windows SDK debugger (or lldb)
      if (auto p = first(windows_kits_debuggers()); !p.empty()) return {DebuggerTool::Kind::Cdb, p};
      if (auto p = first(on_path("cdb.exe")); !p.empty()) return {DebuggerTool::Kind::Cdb, p};
      return try_dap();
    }
    case Kit::Kind::AppleClang:
      return try_dap();
    default: {
      // DWARF: gdb (MinGW, GCC), else lldb
#ifdef _WIN32
      if (kit.kind == Kit::Kind::Clang && kit.target.find("msvc") != std::string::npos)
        if (auto p = first(windows_kits_debuggers()); !p.empty()) return {DebuggerTool::Kind::Cdb, p};
#endif
      if (auto p = first(gdb); !p.empty()) return {DebuggerTool::Kind::Gdb, p};
      return try_dap();
    }
  }
}

} // namespace layer
