// Release packaging: the program built in Release mode, made ready to hand out.
//   Windows: <Name>.exe next to every DLL it needs that Windows does not have
//            (the C/C++ runtime is linked statically, so usually there are none)
//   macOS:   <Name>.app, signed (with the configured identity, or ad hoc), and <Name>.dmg
//   other:   the executable
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>

#include "builder.hpp"

namespace fs = std::filesystem;

namespace layer {

namespace {

std::string lower(std::string s) {
  for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

} // namespace

// The DLL names a Windows executable imports (its PE import table). Portable code:
// it also runs where the program was cross-built.
std::vector<std::string> pe_imports(const std::string& path) {
  std::vector<std::string> out;
  std::ifstream f(path, std::ios::binary);
  if (!f) return out;
  std::vector<unsigned char> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  auto u16 = [&](std::size_t o) -> std::uint32_t { return o + 2 <= d.size() ? d[o] | (d[o + 1] << 8) : 0; };
  auto u32 = [&](std::size_t o) -> std::uint32_t {
    return o + 4 <= d.size() ? d[o] | (d[o + 1] << 8) | (d[o + 2] << 16) | (static_cast<std::uint32_t>(d[o + 3]) << 24) : 0;
  };
  if (d.size() < 0x40 || d[0] != 'M' || d[1] != 'Z') return out;
  const std::size_t pe = u32(0x3C);
  if (pe + 24 > d.size() || u32(pe) != 0x00004550) return out; // "PE\0\0"
  const std::size_t coff = pe + 4;
  const std::uint32_t sections = u16(coff + 2);
  const std::uint32_t opt_size = u16(coff + 16);
  const std::size_t opt = coff + 20;
  const std::uint32_t magic = u16(opt);
  const std::size_t dirs = opt + (magic == 0x20B ? 112 : 96); // data directories (PE32+ / PE32)
  const std::uint32_t import_rva = u32(dirs + 8);             // directory 1: imports
  if (!import_rva) return out;
  const std::size_t sect = opt + opt_size;
  auto to_offset = [&](std::uint32_t rva) -> std::size_t {
    for (std::uint32_t i = 0; i < sections; ++i) {
      const std::size_t s = sect + i * 40;
      const std::uint32_t va = u32(s + 12), vsize = std::max(u32(s + 8), u32(s + 16)), raw = u32(s + 20);
      if (rva >= va && rva < va + vsize) return raw + (rva - va);
    }
    return 0;
  };
  for (std::size_t desc = to_offset(import_rva); desc && desc + 20 <= d.size(); desc += 20) {
    const std::uint32_t name_rva = u32(desc + 12);
    if (!name_rva && !u32(desc)) break;
    std::size_t n = to_offset(name_rva);
    std::string name;
    while (n && n < d.size() && d[n]) name += static_cast<char>(d[n++]);
    if (!name.empty()) out.push_back(name);
  }
  return out;
}

namespace {

// Folders where a DLL of the toolchain may be found (MinGW bin, the compiler's folder,
// the kit's PATH, the Visual C++ redistributable).
std::vector<fs::path> dll_search_dirs(const Kit& kit, const std::map<std::string, std::string>& env) {
  std::vector<fs::path> dirs;
  if (!kit.bin_dir.empty()) dirs.emplace_back(kit.bin_dir);
  if (!kit.cxx.empty()) dirs.push_back(fs::path(kit.cxx).parent_path());
  for (auto& [k, v] : env)
    if (lower(k) == "path") {
      std::size_t a = 0;
      while (a <= v.size()) {
        const auto b = v.find(';', a);
        const std::string p = v.substr(a, b == std::string::npos ? std::string::npos : b - a);
        if (!p.empty()) dirs.emplace_back(p);
        if (b == std::string::npos) break;
        a = b + 1;
      }
    }
  if (!kit.env_script.empty()) { // VC/Auxiliary/Build/vcvars64.bat -> VC/Redist/MSVC/<ver>/x64/Microsoft.VC*.CRT
    const fs::path redist = fs::path(kit.env_script).parent_path().parent_path().parent_path() / "Redist" / "MSVC";
    std::error_code ec;
    if (fs::is_directory(redist, ec))
      for (auto& ver : fs::directory_iterator(redist, ec))
        if (fs::is_directory(ver.path() / "x64", ec))
          for (auto& crt : fs::directory_iterator(ver.path() / "x64", ec))
            if (crt.path().filename().string().find(".CRT") != std::string::npos) dirs.push_back(crt.path());
  }
  return dirs;
}

bool is_system_dll(const std::string& name) {
  const std::string n = lower(name);
  if (n.rfind("api-ms-win-", 0) == 0 || n.rfind("ext-ms-", 0) == 0) return true;
  // the Visual C++ runtime is not part of Windows: ship it when it is used
  static const std::set<std::string> vc{"vcruntime140.dll", "vcruntime140_1.dll", "msvcp140.dll", "msvcp140_1.dll",
                                        "msvcp140_2.dll", "concrt140.dll", "vccorlib140.dll"};
  if (vc.count(n)) return false;
  const char* root = std::getenv("SystemRoot");
  std::error_code ec;
  return root && fs::exists(fs::path(root) / "System32" / name, ec);
}

} // namespace

// Copies the executable and the DLLs it needs into `dir`. Returns the copied DLL names.
std::vector<std::string> deploy_windows(const std::string& exe, const std::string& dir, const std::string& name, const Kit& kit,
                                        const std::map<std::string, std::string>& env, std::string* error) {
  std::vector<std::string> copied;
  std::error_code ec;
  fs::create_directories(dir, ec);
  const fs::path target = fs::path(dir) / (name + ".exe");
  fs::copy_file(exe, target, fs::copy_options::overwrite_existing, ec);
  if (ec) {
    if (error) *error = ec.message();
    return copied;
  }
  const auto dirs = dll_search_dirs(kit, env);
  std::vector<fs::path> todo{target};
  std::set<std::string> seen;
  while (!todo.empty()) {
    const fs::path f = todo.back();
    todo.pop_back();
    for (const std::string& dll : pe_imports(f.string())) {
      const std::string key = lower(dll);
      if (seen.count(key) || is_system_dll(dll)) continue;
      seen.insert(key);
      for (auto& d : dirs) {
        const fs::path src = d / dll;
        if (!fs::exists(src, ec)) continue;
        const fs::path dst = fs::path(dir) / dll;
        fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
        if (!ec) {
          copied.push_back(dll);
          todo.push_back(dst); // its own dependencies
        }
        break;
      }
    }
  }
  return copied;
}

} // namespace layer
