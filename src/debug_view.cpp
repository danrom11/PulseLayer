// Debugging in Code mode: breakpoints in the editor gutter, Debug / Continue /
// Step buttons (F5, F10, F11, Shift+F11, Shift+F5), the call stack and locals.
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

#include "panels.hpp"

namespace fs = std::filesystem;

namespace layer {

using i18n::tr;
using namespace std::chrono_literals;

namespace {

// "C:/.../.pulselayer-build/src/mainwindow.cpp" -> "mainwindow.cpp" when it is a project file
std::string project_file_of(Designer& d, const std::string& path) {
  if (path.empty()) return {};
  const std::string name = fs::path(path).filename().string();
  return d.project.find_file(name) ? name : std::string();
}

} // namespace

std::string Designer::debug_source_path(const std::string& file) {
  return (fs::path(project_dir()) / "src" / file).string();
}

void Designer::cmd_toggle_breakpoint(int line) {
  const std::string file = project.active_file.get();
  if (file.empty()) return;
  if (line <= 0) line = editor ? editor->caret_line() : 0;
  if (line <= 0) return;
  auto all = breakpoints.get();
  auto& lines = all[file];
  if (auto it = std::find(lines.begin(), lines.end(), line); it != lines.end()) lines.erase(it);
  else {
    lines.push_back(line);
    std::sort(lines.begin(), lines.end());
  }
  const std::vector<int> now = lines;
  if (now.empty()) all.erase(file);
  breakpoints = all;
  if (debugger && debug_state.get() != Debugger::Exited) debugger->set_breakpoints(debug_source_path(file), now);
}

void Designer::cmd_debug() {
  if (debugger && debug_state.get() == Debugger::Paused) {
    debugger->resume();
    return;
  }
  if (debugger && (debug_state.get() == Debugger::Running || debug_state.get() == Debugger::Starting)) return;
  if (builder.state.get() != Builder::Idle) return;
  const auto& kits = toolchains.get().kits;
  const int k = kit_index.get();
  if (k < 0 || k >= static_cast<int>(kits.size())) {
    doc.status = i18n::Message{"debug.no_kit", ""};
    return;
  }
  const DebuggerTool tool = find_debugger(kits[static_cast<std::size_t>(k)]);
  if (!tool.found()) {
    doc.status = i18n::Message{"debug.none", kits[static_cast<std::size_t>(k)].name};
    if (output) output->append(i18n::text("debug.none", kits[static_cast<std::size_t>(k)].name) + "\n");
    return;
  }
  // build first (debug info is always on); start when the build succeeds
  debug_after_build = true;
  debug_tool = tool;
  cmd_build(false);
}

void Designer::start_debugger() {
  debug_after_build = false;
  if (!builder.last_build_ok()) return;
  debug_subs.clear();
  debugger = Debugger::create(debug_tool);
  if (!debugger) return;
  Debugger& g = *debugger;
  // mirror the engine's state into the designer's properties (the panel binds to these)
  debug_subs.push_back(g.state.as_observable().subscribe([this](int s) {
    debug_state = s;
    if (s == Debugger::Exited) {
      exec_line = 0;
      doc.status = i18n::Message{"debug.exited", ""};
    } else if (s == Debugger::Running) {
      exec_line = 0;
    } else if (s == Debugger::Paused) {
      bottom_tab = kTabDebug; // the call stack and the locals
    }
  }));
  debug_subs.push_back(g.stack.as_observable().subscribe([this](const std::vector<DebugFrame>& s) {
    debug_stack = s;
    show_frame(0);
  }));
  debug_subs.push_back(g.locals.as_observable().subscribe([this](const std::vector<DebugVar>& v) { debug_locals = v; }));
  debug_subs.push_back(g.output.connect([this](const std::string& line) {
    if (output) output->append(line + "\n");
  }).scoped());
  debug_subs.push_back(g.program_output.connect([this](const std::string& line) {
    if (app_output) app_output->append(line + "\n");
  }).scoped());
  Debugger::Breakpoints bps;
  for (auto& [file, lines] : breakpoints.get()) bps[debug_source_path(file)] = lines;
  bool replace = false;
  const auto& kits = toolchains.get().kits;
  const int k = kit_index.get();
  std::map<std::string, std::string> env;
  if (k >= 0 && k < static_cast<int>(kits.size())) env = kit_environment(kits[static_cast<std::size_t>(k)], replace);
  env["PULSELAYER_CONSOLE"] = "1"; // unbuffered stdout (see the generated pulselayer_console.cpp)
  const std::string exe = builder.executable();
  if (output) output->append(i18n::text("debug.starting", g.name() + ": " + exe) + "\n");
  show_app_output(i18n::text("debug.starting", g.name() + ": " + exe));
  if (!g.start(exe, fs::path(exe).parent_path().string(), bps, env, replace)) {
    doc.status = i18n::Message{"debug.failed", debug_tool.path};
    debugger.reset();
    return;
  }
  doc.status = i18n::Message{"debug.started", g.name()};
}

// The selected frame: open its file (when it is a project file) at the line.
void Designer::show_frame(int index) {
  const auto& s = debug_stack.get();
  debug_frame = index;
  if (index < 0 || index >= static_cast<int>(s.size())) {
    exec_line = 0;
    return;
  }
  // the first frame with project source, when the top is inside a library
  int shown = index;
  if (index == 0)
    for (std::size_t i = 0; i < s.size(); ++i)
      if (!project_file_of(*this, s[i].file).empty()) {
        shown = static_cast<int>(i);
        break;
      }
  const DebugFrame& f = s[static_cast<std::size_t>(shown)];
  const std::string file = project_file_of(*this, f.file);
  if (file.empty()) {
    exec_line = 0;
    return;
  }
  exec_file = file;
  if (project.active_file.get() != file) project.active_file = file;
  mode = 1;
  exec_line = f.line;
  if (shown != index && debugger) debugger->select_frame(shown), debug_frame = shown;
}

void Designer::cmd_step_over() {
  if (debugger) debugger->step_over();
}
void Designer::cmd_step_into() {
  if (debugger) debugger->step_into();
}
void Designer::cmd_step_out() {
  if (debugger) debugger->step_out();
}
void Designer::cmd_debug_pause() {
  if (debugger && debugger->can_pause()) debugger->pause();
}
void Designer::cmd_debug_stop() {
  if (debugger) debugger->stop();
}

void Designer::wire_debugger() {
  // the build ends: start the debugger when Debug asked for it
  builder.state.changed.connect([this](int s) {
    if (s == Builder::Idle && debug_after_build) App::post([this] { start_debugger(); });
  });
  // a release is ready: say where, with a button that opens the folder
  builder.note.connect([this](const std::string& key, const std::string& arg) {
    if (key == "release.failed" || key == "build.failed" || key == "build.stopped") run_release_after = false;
    if (key != "release.done" || !window) return;
    if (run_release_after) { // Run in the Release configuration: start what was just packaged
      run_release_after = false;
      Process::Options o;
#ifdef _WIN32
      o.command = Process::quote((fs::path(arg) / (release_app_name + ".exe")).string());
#elif defined(__APPLE__)
      o.command = "open " + Process::quote((fs::path(arg) / (release_app_name + ".app")).string());
#else
      o.command = Process::quote((fs::path(arg) / release_app_name).string());
#endif
      o.cwd = arg;
      o.capture = false;
      Process::start(o, {}, {});
      doc.status = i18n::Message{"release.started", release_app_name};
      return;
    }
    ToastOptions o;
    o.kind = ToastKind::Success;
    o.duration = std::chrono::milliseconds(8000);
    o.action_label = i18n::text("release.open");
    o.action = [this, arg] { open_folder(arg); };
    toast(*window, i18n::text("release.done", arg), o);
  });
}

// ── release ──────────────────────────────────────────────────────────────────

std::string Designer::release_dir() {
  if (!project.file.get().empty()) return (fs::path(project.dir()) / "release").string();
  return (fs::path(project_dir()) / "release").string();
}

void Designer::cmd_release() {
  if (builder.state.get() != Builder::Idle) return;
  Builder::Request r = build_request();
  r.release = true;
  std::string name;
  for (char c : project.name.get())
    if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-') name += c;
  r.app_name = name.empty() ? std::string("App") : name;
  release_app_name = r.app_name;
  r.release_dir = release_dir();
  r.sign_identity = sign_identity.get();
  if (output) output->clear();
  doc.status = i18n::Message{"release.building", r.release_dir};
  builder.build(r, false);
}

void Designer::open_folder(const std::string& path) {
  Process::Options o;
#ifdef _WIN32
  o.command = "explorer.exe " + Process::quote(path);
#elif defined(__APPLE__)
  o.command = "open " + Process::quote(path);
#else
  o.command = "xdg-open " + Process::quote(path);
#endif
  o.capture = false;
  Process::start(o, {}, {});
}

// ── the panel ────────────────────────────────────────────────────────────────

Node debug_controls(Designer& d) {
  auto st = [&d](std::function<bool(int)> pred) { return d.debug_state.map(pred); };
  auto debugging = st([](int s) { return s == Debugger::Starting || s == Debugger::Running || s == Debugger::Paused; });
  auto paused = st([](int s) { return s == Debugger::Paused; });
  auto running = st([](int s) { return s == Debugger::Running; });
  // shown only while debugging: Continue is the Run button, Stop the Stop button
  return Show(debugging,
              HStack{
                  Button("\xE2\x86\xB7", [&d] { d.cmd_step_over(); }).flat().id("debug-over").tooltip(tr("debug.over.tip")).enabled(paused),
                  Button("\xE2\x86\x93", [&d] { d.cmd_step_into(); }).flat().id("debug-into").tooltip(tr("debug.into.tip")).enabled(paused),
                  Button("\xE2\x86\x91", [&d] { d.cmd_step_out(); }).flat().id("debug-out").tooltip(tr("debug.out.tip")).enabled(paused),
                  Button("\xE2\x8F\xB8", [&d] { d.cmd_debug_pause(); }).flat().id("debug-pause").tooltip(tr("debug.pause.tip")).enabled(running),
                  Divider(Orientation::Vertical).height(22),
              }
                  .spacing(2)
                  .align_items(Alignment::Center))
      .transition(200ms);
}

Node debug_panel(Designer& d) {
  auto frames = ForEach(d.debug_stack, [&d](const DebugFrame& f, std::size_t i) {
    const std::string where = f.file.empty() ? std::string() : fs::path(f.file).filename().string() + ":" + std::to_string(f.line);
    auto selected = d.debug_frame.map([i](int sel) { return sel == static_cast<int>(i); });
    return HStack{
        Text(f.function.empty() ? std::string("??") : f.function).mono().font_size(12).flex(),
        Text(where).dim().font_size(11.5f),
    }
        .spacing(8)
        .padding(Insets(8, 3))
        .radius(4)
        .background(pulse::combine_latest(selected, theme().as_observable(), [](bool on, const Theme& t) {
          return on ? t.selection : Color::transparent();
        }))
        .on_click([&d, i] {
          if (d.debugger) d.debugger->select_frame(static_cast<int>(i));
          d.show_frame(static_cast<int>(i));
        });
  });
  frames.spacing(1);
  auto vars = Table(d.debug_locals, {Column<DebugVar>{"Name", 140, [](const DebugVar& v) { return v.name; }},
                                     Column<DebugVar>{"Value", 220, [](const DebugVar& v) { return v.value; }},
                                     Column<DebugVar>{"Type", 220, [](const DebugVar& v) { return v.type; }}});
  auto state_text = pulse::combine_latest(i18n::current().as_observable(), d.debug_state.as_observable(), [](const std::string&, int s) {
    static const char* keys[] = {"debug.state.idle", "debug.state.starting", "debug.state.running", "debug.state.paused",
                                 "debug.state.exited"};
    return i18n::text(keys[std::clamp(s, 0, 4)]);
  });
  return Split(VStack{Text(state_text).dim().font_size(11.5f), Scroll(std::move(frames).padding(Insets(2))).flex()}
                   .spacing(4)
                   .padding(Insets(8, 6)),
               std::move(vars).id("debug-locals"), Orientation::Horizontal, 320)
      .min_pane(140);
}

} // namespace layer
