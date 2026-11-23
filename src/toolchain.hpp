#pragma once
// Auto-detection of compilers, linkers and build tools ("kits", as in Qt Creator).
//
// Windows: MSVC through vswhere (cl.exe, link.exe, vcvars64.bat), Clang from
//          Visual Studio / LLVM, MinGW from PATH, Qt, MSYS2; CMake and Ninja
//          from PATH, Visual Studio and Qt.
// macOS:   Apple Clang through xcrun, Homebrew GCC/LLVM; CMake/Ninja from PATH,
//          Homebrew and CMake.app.
#include <map>
#include <string>
#include <vector>

namespace layer {

struct Kit {
  enum class Kind { MSVC, ClangMSVC, MinGW, Clang, GCC, AppleClang };
  std::string id;         // stable identifier (saved in the settings)
  std::string name;       // shown to the user
  Kind kind = Kind::GCC;
  std::string cxx;        // C++ compiler
  std::string linker;
  std::string version;
  std::string target;     // e.g. x86_64-w64-mingw32
  std::string env_script; // vcvars64.bat for MSVC-based kits
  std::string bin_dir;    // prepended to PATH (MinGW, Homebrew GCC)
  // Homebrew LLVM on macOS: its own libc++ (<llvm>/lib/c++). Its headers are newer
  // than the system libc++, so programs must link against this one (clang 18+).
  std::string libcxx_dir;
  bool operator==(const Kit&) const = default;
};

struct Tool {
  std::string path, version;
  bool found() const { return !path.empty(); }
  bool operator==(const Tool&) const = default;
};

struct Toolchains {
  std::vector<Kit> kits;
  Tool cmake, ninja;
  std::vector<std::string> log; // what was looked at (shown in the settings)
  bool operator==(const Toolchains&) const = default;
};

// Blocking (runs `--version` queries): call it on a background thread.
Toolchains detect_toolchains();

// Environment for building with `kit` (MSVC: captured from vcvars64.bat, cached).
std::map<std::string, std::string> kit_environment(const Kit& kit, bool& replace_env);

const char* kind_name(Kit::Kind k);

// clang-format from PATH, Visual Studio (C++ Clang tools), LLVM, Qt Creator or Homebrew; "" if none.
std::string find_clang_format(const Toolchains& tc);

} // namespace layer
