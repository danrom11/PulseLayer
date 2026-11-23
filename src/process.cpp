#include "process.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <csignal>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace layer {

namespace {

// Splits a byte stream into lines and forwards them.
struct LineSplitter {
  std::string pending;
  void feed(const char* data, std::size_t n, const Process::LineFn& fn) {
    for (std::size_t i = 0; i < n; ++i) {
      const char c = data[i];
      if (c == '\n') {
        if (!pending.empty() && pending.back() == '\r') pending.pop_back();
        if (fn) fn(pending);
        pending.clear();
      } else {
        pending.push_back(c);
      }
    }
  }
  void flush(const Process::LineFn& fn) {
    if (!pending.empty() && fn) fn(pending);
    pending.clear();
  }
};

} // namespace

#ifdef _WIN32

namespace {

std::wstring widen(const std::string& s) {
  if (s.empty()) return {};
  const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
  std::wstring w(static_cast<std::size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
  return w;
}

std::string narrow(const std::wstring& w) {
  if (w.empty()) return {};
  const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
  std::string s(static_cast<std::size_t>(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
  return s;
}

// Tools write UTF-8 or the OEM code page; normalize lines to UTF-8.
std::string to_utf8(const std::string& raw) {
  if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, raw.data(), static_cast<int>(raw.size()), nullptr, 0) > 0 ||
      raw.empty())
    return raw;
  const int n = MultiByteToWideChar(CP_OEMCP, 0, raw.data(), static_cast<int>(raw.size()), nullptr, 0);
  std::wstring w(static_cast<std::size_t>(n), L'\0');
  MultiByteToWideChar(CP_OEMCP, 0, raw.data(), static_cast<int>(raw.size()), w.data(), n);
  return narrow(w);
}

class WinProcess final : public Process, public std::enable_shared_from_this<WinProcess> {
public:
  ~WinProcess() override {
    if (reader_.joinable()) reader_.detach();
  }

  bool launch(const Options& opt, LineFn on_line, ExitFn on_exit) {
    on_line_ = std::move(on_line);
    on_exit_ = std::move(on_exit);

    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
    HANDLE read_end = nullptr, write_end = nullptr;
    if (opt.capture) {
      if (!CreatePipe(&read_end, &write_end, &sa, 0)) return false;
      SetHandleInformation(read_end, HANDLE_FLAG_INHERIT, 0);
    }
    HANDLE in_read = nullptr;
    if (opt.input) {
      if (!CreatePipe(&in_read, &in_write_, &sa, 0)) return false;
      SetHandleInformation(in_write_, HANDLE_FLAG_INHERIT, 0);
    }

    // environment block
    std::wstring env_block;
    auto env = opt.replace_env ? opt.env : current_environment();
    if (!opt.replace_env) {
      for (auto& [k, v] : opt.env) {
        // Windows variable names are case-insensitive ("Path" == "PATH")
        for (auto it = env.begin(); it != env.end();) {
          if (_stricmp(it->first.c_str(), k.c_str()) == 0) it = env.erase(it);
          else ++it;
        }
        env[k] = v;
      }
    }
    env["VSLANG"] = "1033"; // English compiler messages: stable to parse
    for (auto& [k, v] : env) {
      env_block += widen(k + "=" + v);
      env_block.push_back(L'\0');
    }
    env_block.push_back(L'\0');

    STARTUPINFOW si{};
    si.cb = sizeof si;
    if (opt.capture) {
      si.dwFlags = STARTF_USESTDHANDLES;
      si.hStdOutput = write_end;
      si.hStdError = write_end;
      si.hStdInput = opt.input ? in_read : GetStdHandle(STD_INPUT_HANDLE);
    }
    PROCESS_INFORMATION pi{};
    std::wstring cmd = widen(opt.command);
    std::wstring cwd = widen(opt.cwd);
    const DWORD flags = CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED | (opt.capture ? CREATE_NO_WINDOW : 0);
    const BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, opt.capture ? TRUE : FALSE, flags,
                                   env_block.data(), cwd.empty() ? nullptr : cwd.c_str(), &si, &pi);
    if (write_end) CloseHandle(write_end);
    if (in_read) CloseHandle(in_read);
    if (!ok) {
      if (read_end) CloseHandle(read_end);
      return false;
    }
    pid_ = static_cast<int>(pi.dwProcessId);
    // A job object lets kill() take down the whole tree (cmake -> ninja -> cl ...).
    job_ = CreateJobObjectW(nullptr, nullptr);
    if (job_) {
      JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
      if (opt.capture) info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
      SetInformationJobObject(job_, JobObjectExtendedLimitInformation, &info, sizeof info);
      AssignProcessToJobObject(job_, pi.hProcess);
    }
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    process_ = pi.hProcess;
    running_ = true;

    auto self = shared_from_this();
    const bool raw = opt.raw;
    reader_ = std::thread([self, read_end, raw] {
      LineSplitter split;
      auto forward = [&](const std::string& l) {
        if (self->on_line_) self->on_line_(to_utf8(l));
      };
      if (read_end) {
        char buf[4096];
        DWORD n = 0;
        while (ReadFile(read_end, buf, sizeof buf, &n, nullptr) && n > 0) {
          if (raw) {
            if (self->on_line_) self->on_line_(std::string(buf, n));
          } else {
            split.feed(buf, n, forward);
          }
        }
        split.flush(forward);
        CloseHandle(read_end);
      }
      WaitForSingleObject(self->process_, INFINITE);
      DWORD code = 0;
      GetExitCodeProcess(self->process_, &code);
      CloseHandle(self->process_);
      if (self->job_) CloseHandle(self->job_);
      self->running_ = false;
      if (self->on_exit_) self->on_exit_(self->killed_ ? -1 : static_cast<int>(code));
    });
    return true;
  }

  void kill() override {
    if (!running_) return;
    killed_ = true;
    if (job_) TerminateJobObject(job_, 1);
    else if (process_) TerminateProcess(process_, 1);
  }
  bool running() const override { return running_; }
  bool write(const std::string& data) override {
    std::lock_guard<std::mutex> lock(write_mx_);
    if (!in_write_) return false;
    DWORD n = 0;
    return WriteFile(in_write_, data.data(), static_cast<DWORD>(data.size()), &n, nullptr) && n == data.size();
  }
  void close_input() override {
    std::lock_guard<std::mutex> lock(write_mx_);
    if (in_write_) CloseHandle(in_write_);
    in_write_ = nullptr;
  }
  int pid() const override { return pid_; }

private:
  LineFn on_line_;
  ExitFn on_exit_;
  HANDLE in_write_ = nullptr;
  std::mutex write_mx_;
  int pid_ = 0;
  HANDLE process_ = nullptr;
  HANDLE job_ = nullptr;
  std::thread reader_;
  std::atomic<bool> running_{false}, killed_{false};
};

} // namespace

std::shared_ptr<Process> Process::start(Options opt, LineFn on_line, ExitFn on_exit) {
  auto p = std::make_shared<WinProcess>();
  if (!p->launch(opt, std::move(on_line), std::move(on_exit))) return nullptr;
  return p;
}

std::map<std::string, std::string> Process::current_environment() {
  std::map<std::string, std::string> env;
  wchar_t* block = GetEnvironmentStringsW();
  for (wchar_t* p = block; p && *p; p += wcslen(p) + 1) {
    std::wstring entry = p;
    const auto eq = entry.find(L'=', 1); // entries like "=C:=C:\" start with '='
    if (eq == std::wstring::npos) continue;
    env[narrow(entry.substr(0, eq))] = narrow(entry.substr(eq + 1));
  }
  if (block) FreeEnvironmentStringsW(block);
  return env;
}

std::string Process::quote(const std::string& arg) {
  if (!arg.empty() && arg.find_first_of(" \t\"&()") == std::string::npos) return arg;
  std::string out = "\"";
  for (char c : arg) {
    if (c == '"') out += '\\';
    out += c;
  }
  return out + "\"";
}

#else // POSIX

namespace {

class PosixProcess final : public Process, public std::enable_shared_from_this<PosixProcess> {
public:
  ~PosixProcess() override {
    if (reader_.joinable()) reader_.detach();
  }

  bool launch(const Options& opt, LineFn on_line, ExitFn on_exit) {
    on_line_ = std::move(on_line);
    on_exit_ = std::move(on_exit);
    int fds[2] = {-1, -1};
    if (opt.capture && pipe(fds) != 0) return false;
    int in_fds[2] = {-1, -1};
    if (opt.input && pipe(in_fds) != 0) return false;
    auto env = opt.replace_env ? opt.env : current_environment();
    if (!opt.replace_env)
      for (auto& [k, v] : opt.env) env[k] = v;
    std::vector<std::string> env_strings;
    for (auto& [k, v] : env) env_strings.push_back(k + "=" + v);

    pid_ = fork();
    if (pid_ < 0) return false;
    if (pid_ == 0) {
      setpgid(0, 0); // own group: kill() can signal the whole tree
      if (opt.capture) {
        dup2(fds[1], STDOUT_FILENO);
        dup2(fds[1], STDERR_FILENO);
        close(fds[0]);
        close(fds[1]);
      }
      if (opt.input) {
        dup2(in_fds[0], STDIN_FILENO);
        close(in_fds[0]);
        close(in_fds[1]);
      }
      if (!opt.cwd.empty() && chdir(opt.cwd.c_str()) != 0) _exit(127);
      std::vector<char*> envp;
      for (auto& s : env_strings) envp.push_back(s.data());
      envp.push_back(nullptr);
      execle("/bin/sh", "sh", "-c", opt.command.c_str(), static_cast<char*>(nullptr), envp.data());
      _exit(127);
    }
    setpgid(pid_, pid_);
    if (opt.capture) close(fds[1]);
    if (opt.input) {
      close(in_fds[0]);
      in_fd_ = in_fds[1];
    }
    running_ = true;
    auto self = shared_from_this();
    const int rd = opt.capture ? fds[0] : -1;
    const bool raw = opt.raw;
    reader_ = std::thread([self, rd, raw] {
      LineSplitter split;
      if (rd >= 0) {
        char buf[4096];
        ssize_t n;
        while ((n = read(rd, buf, sizeof buf)) > 0) {
          if (raw) {
            if (self->on_line_) self->on_line_(std::string(buf, static_cast<std::size_t>(n)));
          } else {
            split.feed(buf, static_cast<std::size_t>(n), self->on_line_);
          }
        }
        split.flush(self->on_line_);
        close(rd);
      }
      int status = 0;
      waitpid(self->pid_, &status, 0);
      self->running_ = false;
      const int code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
      if (self->on_exit_) self->on_exit_(self->killed_ ? -1 : code);
    });
    return true;
  }

  void kill() override {
    if (!running_) return;
    killed_ = true;
    ::kill(-pid_, SIGTERM);
  }
  bool running() const override { return running_; }
  bool write(const std::string& data) override {
    std::lock_guard<std::mutex> lock(write_mx_);
    if (in_fd_ < 0) return false;
    return ::write(in_fd_, data.data(), data.size()) == static_cast<ssize_t>(data.size());
  }
  void close_input() override {
    std::lock_guard<std::mutex> lock(write_mx_);
    if (in_fd_ >= 0) close(in_fd_);
    in_fd_ = -1;
  }
  int pid() const override { return static_cast<int>(pid_); }

private:
  LineFn on_line_;
  ExitFn on_exit_;
  int in_fd_ = -1;
  std::mutex write_mx_;
  pid_t pid_ = -1;
  std::thread reader_;
  std::atomic<bool> running_{false}, killed_{false};
};

} // namespace

std::shared_ptr<Process> Process::start(Options opt, LineFn on_line, ExitFn on_exit) {
  auto p = std::make_shared<PosixProcess>();
  if (!p->launch(opt, std::move(on_line), std::move(on_exit))) return nullptr;
  return p;
}

std::map<std::string, std::string> Process::current_environment() {
  std::map<std::string, std::string> env;
  for (char** e = environ; e && *e; ++e) {
    std::string s = *e;
    const auto eq = s.find('=');
    if (eq != std::string::npos) env[s.substr(0, eq)] = s.substr(eq + 1);
  }
  return env;
}

std::string Process::quote(const std::string& arg) {
  if (!arg.empty() && arg.find_first_of(" \t\"'&()$\\;|<>") == std::string::npos) return arg;
  std::string out = "'";
  for (char c : arg) {
    if (c == '\'') out += "'\\''";
    else out += c;
  }
  return out + "'";
}

#endif

int Process::run(const std::string& command, std::string& output, int timeout_ms,
                 const std::map<std::string, std::string>& env, bool replace_env, const std::string& cwd) {
  struct Shared {
    std::mutex m;
    std::condition_variable cv;
    bool done = false;
    int code = -1;
    std::string out;
  };
  auto sh = std::make_shared<Shared>();
  Options opt;
  opt.command = command;
  opt.env = env;
  opt.replace_env = replace_env;
  opt.cwd = cwd;
  auto p = start(
      opt,
      [sh](const std::string& l) {
        std::lock_guard<std::mutex> lock(sh->m);
        sh->out += l;
        sh->out += '\n';
      },
      [sh](int code) {
        std::lock_guard<std::mutex> lock(sh->m);
        sh->code = code;
        sh->done = true;
        sh->cv.notify_all();
      });
  if (!p) return -1;
  std::unique_lock<std::mutex> lock(sh->m);
  if (!sh->cv.wait_for(lock, std::chrono::milliseconds(timeout_ms), [&] { return sh->done; })) {
    lock.unlock();
    p->kill();
    return -1;
  }
  output = sh->out;
  return sh->code;
}

} // namespace layer
