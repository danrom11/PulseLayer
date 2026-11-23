// Code mode: editor + issues + build output, the Kits settings page and the
// build / run commands.
#include <filesystem>
#include <thread>

#include "cpp_complete.hpp"
#include "cpp_highlight.hpp"
#include "panels.hpp"

namespace fs = std::filesystem;

namespace layer {

using i18n::tr;

namespace {

std::string sanitize(const std::string& s) {
  std::string out;
  for (char c : s) out += (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_') ? c : '_';
  return out.empty() ? std::string("form") : out;
}

Color severity_color(Diagnostic::Severity s) {
  return s == Diagnostic::Error ? th().danger : s == Diagnostic::Warning ? th().warning : th().text_dim;
}

pulse::observable<std::vector<std::string>> kit_names(Designer& d) {
  return pulse::combine_latest(i18n::current().as_observable(), d.toolchains.as_observable(),
                               [](const std::string&, const Toolchains& t) {
                                 std::vector<std::string> names;
                                 for (auto& k : t.kits) names.push_back(k.name);
                                 if (names.empty()) names.push_back(i18n::text("code.no_kits"));
                                 return names;
                               });
}

// Ctrl+F / Ctrl+H: find (and replace) in the open file
Node find_bar(Designer& d) {
  return Show(d.find_open,
              VStack{
                  HStack{
                      TextField(d.find_query)
                          .placeholder(tr("find.placeholder"))
                          .on_submit([&d](const std::string&) { d.find_step(false); })
                          .id("find-query")
                          .ref(d.find_field)
                          .width(240),
                      Checkbox(tr("find.case"), d.find_case),
                      Checkbox(tr("find.word"), d.find_word),
                      Text(d.find_status).dim().font_size(11.5f).min_width(60),
                      Spacer(),
                      Button("↑", [&d] { d.find_step(true); }).flat().tooltip(tr("find.prev.tip")).id("find-prev"),
                      Button("↓", [&d] { d.find_step(false); }).flat().tooltip(tr("find.next.tip")).id("find-next"),
                      Button(tr("find.toggle_replace"), [&d] { d.replace_open = !d.replace_open.get(); }).flat(),
                      Button("×", [&d] { d.find_close(); }).flat().tooltip(tr("find.close.tip")).id("find-close"),
                  }
                      .spacing(8)
                      .align_items(Alignment::Center),
                  Show(d.replace_open, HStack{
                                           TextField(d.replace_query)
                                               .placeholder(tr("find.replace.placeholder"))
                                               .on_submit([&d](const std::string&) { d.cmd_replace_one(); })
                                               .id("replace-query")
                                               .width(240),
                                           Button(tr("find.replace"), [&d] { d.cmd_replace_one(); }).id("replace-one"),
                                           Button(tr("find.replace_all"), [&d] { d.cmd_replace_all(); }).id("replace-all"),
                                       }
                                           .spacing(8)
                                           .align_items(Alignment::Center)),
              }
                  .spacing(6)
                  .padding(Insets(12, 6))
                  .background(pulseui::theme().map([](const Theme& t) { return t.surface_alt; })));
}

} // namespace

// ── commands ─────────────────────────────────────────────────────────────────

void Designer::detect_kits() {
  if (detecting.get()) return;
  detecting = true;
  std::weak_ptr<bool> weak = alive;
  std::thread([this, weak] {
    Toolchains tc = detect_toolchains();
    App::post([this, weak, tc] {
      if (weak.expired()) return;
      toolchains = tc;
      detecting = false;
      int index = 0;
      for (std::size_t i = 0; i < tc.kits.size(); ++i)
        if (tc.kits[i].id == preferred_kit) index = static_cast<int>(i);
      kit_index = -1; // force a change notification
      kit_index = index;
    });
  }).detach();
}

std::string Designer::project_dir() {
  if (!project.file.get().empty()) return (fs::path(project.dir()) / ".pulselayer-build").string();
  return (fs::temp_directory_path() / "PulseLayer" / sanitize(project.name.get())).string();
}

std::string Designer::effective_pulseui_dir() {
  std::error_code ec;
  for (const std::string& dir : {pulseui_dir.get(), std::string(PULSELAYER_PULSEUI_DIR)})
    if (!dir.empty() && fs::exists(fs::path(dir) / "CMakeLists.txt", ec)) return dir;
  return pulseui_dir.get().empty() ? std::string(PULSELAYER_PULSEUI_DIR) : pulseui_dir.get();
}

Builder::Request Designer::build_request() {
  const auto& kits = toolchains.get().kits;
  Builder::Request r;
  const int k = kit_index.get();
  if (k >= 0 && k < static_cast<int>(kits.size())) r.kit = kits[static_cast<std::size_t>(k)];
  r.cmake = toolchains.get().cmake;
  r.ninja = toolchains.get().ninja;
  r.pulseui_dir = effective_pulseui_dir();
  r.project_dir = project_dir();
  sync_form();
  r.files = project.build_files();
  for (auto& f : project.files) r.user_files.push_back(f.name);
  return r;
}

void Designer::cmd_build(bool run_after) {
  if (builder.state.get() != Builder::Idle) return;
  if (output) output->clear();
  builder.build(build_request(), run_after);
}

// ── right-click menus ────────────────────────────────────────────────────────

namespace {
void translate_menu(CodeEditor& e) {
  e.set_menu_labels(i18n::text("menu.undo"), i18n::text("menu.redo"), i18n::text("menu.cut"), i18n::text("menu.copy"),
                    i18n::text("menu.paste"), i18n::text("menu.select_all"));
}
} // namespace

pulseui::MenuItems output_menu(CodeEditor* e) {
  if (!e) return {};
  translate_menu(*e);
  MenuItems m = e->standard_menu();
  m.push_back(Separator());
  m.push_back(Action(i18n::text("menu.clear"), [e] { e->clear(); }));
  return m;
}

// Qt Creator style: the code actions for the symbol under the caret (a right
// click moves the caret there first), then breakpoints, then the clipboard.
pulseui::MenuItems Designer::editor_menu() {
  MenuItems m;
  if (!editor) return m;
  m.push_back(Action(i18n::text("menu.go_to_definition"), [this] { cmd_go_to_definition(); }).shortcut("F12"));
  m.push_back(Action(i18n::text("menu.switch_header"), [this] { cmd_switch_header(); }).shortcut("F4"));
  m.push_back(Action(i18n::text("menu.rename"), [this] { cmd_rename(); }).shortcut("F2"));
  m.push_back(Action(i18n::text("menu.create_definition"), [this] { cmd_create_definition(); }).shortcut("Alt+Enter"));
  m.push_back(Separator());
  m.push_back(Action(i18n::text("menu.find"), [this] { cmd_find(false); }).shortcut("Ctrl+F"));
  m.push_back(Action(i18n::text("menu.toggle_comment"), [this] { if (editor) editor->toggle_comment(); }).shortcut("Ctrl+/"));
  m.push_back(Action(i18n::text("menu.format"), [this] { cmd_format(); }).shortcut("Ctrl+Shift+F"));
  m.push_back(Separator());
  m.push_back(Action(i18n::text("menu.toggle_breakpoint"), [this] { cmd_toggle_breakpoint(); }).shortcut("F9"));
  if (debug_state.get() == Debugger::Paused)
    m.push_back(Action(i18n::text("debug.continue.button"), [this] { cmd_run(); }).shortcut("F5"));
  m.push_back(Separator());
  translate_menu(*editor);
  for (auto& e : editor->standard_menu()) m.push_back(std::move(e));
  return m;
}

void Designer::show_app_output(const std::string& first_line) {
  if (app_output) {
    app_output->clear();
    app_output->append(first_line + "\n");
  }
  bottom_tab = kTabAppOutput;
}

void Designer::cmd_run_plain() {
  // always rebuild first: incremental builds are quick and the code may have changed
  cmd_build(true);
}

// Run follows the configuration: Debug starts the program under the debugger
// (it stops at breakpoints; Run again continues), Release builds an optimized,
// packaged program and starts it.
void Designer::cmd_run() {
  if (debugger && debug_state.get() == Debugger::Paused) {
    debugger->resume();
    return;
  }
  if (debugging() || builder.state.get() != Builder::Idle) return;
  if (build_config.get() == 1) {
    run_release_after = true;
    cmd_release();
    return;
  }
  const auto& kits = toolchains.get().kits;
  const int k = kit_index.get();
  if (k >= 0 && k < static_cast<int>(kits.size()) && !find_debugger(kits[static_cast<std::size_t>(k)]).found()) {
    // no debugger for this kit: run it plainly, and say why breakpoints won't stop it
    if (output) output->append(i18n::text("debug.none", kits[static_cast<std::size_t>(k)].name) + "\n");
    cmd_run_plain();
    return;
  }
  cmd_debug();
}

void Designer::cmd_build_selected() {
  if (build_config.get() == 1) cmd_release();
  else cmd_build(false);
}

bool Designer::debugging() const {
  const int s = debug_state.get();
  return debugger && (s == Debugger::Starting || s == Debugger::Running || s == Debugger::Paused);
}

void Designer::cmd_stop() {
  run_release_after = false;
  debug_after_build = false;
  if (debugging()) cmd_debug_stop();
  builder.stop();
}

// ── code mode ────────────────────────────────────────────────────────────────

Node code_view(Designer& d) {
  auto state_text = pulse::combine_latest(i18n::current().as_observable(), d.builder.state.as_observable(),
                                          [](const std::string&, int s) {
                                            static const char* keys[] = {"state.idle", "state.configuring", "state.building", "state.running"};
                                            return i18n::text(keys[std::clamp(s, 0, 3)]);
                                          });
  auto busy = d.builder.state.map([](int s) { return s != Builder::Idle; });
  auto idle = d.builder.state.map([](int s) { return s == Builder::Idle; });

  // Errors located in the open file become squiggles in the editor.
  auto editor_diags = pulse::combine_latest(
      d.builder.issues.as_observable(), d.project.active_file.as_observable(),
      [](const std::vector<Issue>& list, const std::string& file) {
        std::vector<Diagnostic> out;
        for (auto& i : list)
          if (i.user_code && i.file == file && i.line > 0 && i.severity != Diagnostic::Note)
            out.push_back({i.line, i.column, i.severity, i.message});
        return out;
      });
  auto issues_title = pulse::combine_latest(i18n::current().as_observable(), d.builder.issues.as_observable(),
                                            [](const std::string&, const std::vector<Issue>& list) {
                                              int n = 0;
                                              for (auto& i : list) n += i.severity != Diagnostic::Note;
                                              return i18n::text("code.issues", std::to_string(n));
                                            });

  auto issue_list = ForEach(d.builder.issues, [&d](const Issue& is) {
    std::string where;
    if (!is.file.empty()) where = fs::path(is.file).filename().string();
    if (is.line > 0) where += ":" + std::to_string(is.line);
    return HStack{
        Text("●").color(severity_color(is.severity)).width(14),
        Text(is.message).wrap().flex(),
        Text(where).dim().font_size(11.5f),
    }
        .spacing(8)
        .align_items(Alignment::Start)
        .padding(Insets(10, 4))
        .radius(4)
        .on_click([&d, is] {
          if (!is.user_code || !d.editor) return;
          d.project.active_file = is.file;
          d.mode = 1;
          d.editor->go_to(is.line, std::max(1, is.column));
        });
  });
  issue_list.spacing(1);

  // open files as tabs; the active one is highlighted
  auto tabs = ForEach(d.open_files, [&d](const std::string& f) {
    auto look = [f, &d](bool text) {
      return pulse::combine_latest(d.project.active_file.as_observable(), pulseui::theme().as_observable(),
                                   [f, text](const std::string& a, const Theme& t) {
                                     const bool on = a == f;
                                     return text ? (on ? t.text : t.text_dim) : (on ? t.surface_alt : Color::transparent());
                                   });
    };
    return HStack{
        Text(f).font_size(12.5f).color(look(true)),
        Text("×").dim().font_size(13).tooltip(tr("code.tab.close")).cursor(Cursor::Hand).on_click([&d, f] { d.close_tab(f); }),
    }
        .spacing(6)
        .align_items(Alignment::Center)
        .padding(Insets(10, 5))
        .radius(6)
        .background(look(false))
        .cursor(Cursor::Hand)
        .on_click([&d, f] { d.project.active_file = f; })
        .id("tab-" + f);
  });
  tabs.horizontal().spacing(2);

  return VStack{
      HStack{
          Text(tr("code.kit")).dim(),
          Select(kit_names(d), d.kit_index).width(300).tooltip(tr("code.kit.tip")),
          Button("", [&d] { d.detect_kits(); }).icon(R"svg(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M20 11a8 8 0 1 0-2.3 5.7"/><polyline points="20 4 20 11 13 11"/></svg>)svg").flat().tooltip(tr("kits.redetect")).enabled(d.detecting.map([](bool b) { return !b; })),
          Spacer(),
          Text(state_text).dim(),
          debug_controls(d),
          Select(i18n::current().map([](const std::string&) {
                   return std::vector<std::string>{i18n::text("config.debug"), i18n::text("config.release")};
                 }),
                 d.build_config)
              .id("config")
              .width(120)
              .tooltip(tr("config.tip"))
              .enabled(idle),
          Button(tr("code.build"), [&d] { d.cmd_build_selected(); }).id("build").tooltip(tr("code.build.tip")).enabled(idle),
          Button(pulse::combine_latest(i18n::current().as_observable(), d.debug_state.as_observable(),
                                       [](const std::string&, int s) {
                                         return i18n::text(s == Debugger::Paused ? "debug.continue.button" : "code.run");
                                       }),
                 [&d] { d.cmd_run(); })
              .id("run")
              .primary()
              .tooltip(pulse::combine_latest(i18n::current().as_observable(), d.build_config.as_observable(),
                                             [](const std::string&, int c) { return i18n::text(c == 0 ? "code.run.debug.tip" : "code.run.release.tip"); }))
              .enabled(pulse::combine_latest(d.builder.state.as_observable(), d.debug_state.as_observable(), [](int b, int s) {
                return s == Debugger::Paused || (b == Builder::Idle && s != Debugger::Running && s != Debugger::Starting);
              })),
          Button(tr("code.stop"), [&d] { d.cmd_stop(); })
              .danger()
              .id("stop")
              .tooltip(tr("code.stop.tip"))
              .enabled(pulse::combine_latest(d.builder.state.as_observable(), d.debug_state.as_observable(), [](int b, int s) {
                return b != Builder::Idle || s == Debugger::Starting || s == Debugger::Running || s == Debugger::Paused;
              })),
      }
          .spacing(8)
          .align_items(Alignment::Center)
          .padding(Insets(12, 6)),
      Split(VStack{
                HStack{Scroll(std::move(tabs)).horizontal().flex(),
                       Text(pulse::combine_latest(i18n::current().as_observable(), d.project.dirty.as_observable(),
                                                  [](const std::string&, bool dirty) {
                                                    return dirty ? i18n::text("code.unsaved") : std::string();
                                                  }))
                           .dim(),
                       Text(tr("code.completion.hint")).dim().font_size(11.5f).tooltip(tr("code.keys.tip"))}
                    .spacing(10)
                    .align_items(Alignment::Center)
                    .padding(Insets(8, 3, 12, 3)),
                find_bar(d),
                CodeEditor(d.editor_text)
                    .highlighter(cpp_highlighter())
                    .completion(make_cpp_completion([&d] { return d.completion_context(); }))
                    .hover([&d](const std::vector<std::string>& lines, std::size_t line, std::size_t col) {
                      return describe_symbol(lines, line, col, d.completion_context());
                    })
                    .auto_close()
                    .line_comment("//")
                    .diagnostics(editor_diags)
                    .breakpoints(pulse::combine_latest(d.breakpoints.as_observable(), d.project.active_file.as_observable(),
                                                       [](const std::map<std::string, std::vector<int>>& all, const std::string& f) {
                                                         auto it = all.find(f);
                                                         return it == all.end() ? std::vector<int>{} : it->second;
                                                       }))
                    .execution_line(pulse::combine_latest(d.exec_line.as_observable(), d.exec_file.as_observable(),
                                                          d.project.active_file.as_observable(),
                                                          [](int line, const std::string& ef, const std::string& f) { return ef == f ? line : 0; }))
                    .on_mount_fn([&d](CodeEditor& e) {
                      e.breakpoint_toggled.connect([&d](int line) { d.cmd_toggle_breakpoint(line); });
                    })
                    .context_menu([&d] { return d.editor_menu(); })
                    .id("editor")
                    .ref(d.editor)
                    .flex(),
            }
                .spacing(0),
            Tabs{
                Tab(issues_title, Scroll(std::move(issue_list).padding(Insets(6, 4)))),
                Tab(tr("code.app_output"), CodeEditor()
                                               .read_only()
                                               .line_numbers(false)
                                               .id("app-output")
                                               .ref(d.app_output)
                                               .context_menu([&d] { return output_menu(d.app_output); })),
                Tab(tr("code.output"), CodeEditor()
                                           .read_only()
                                           .line_numbers(false)
                                           .id("output")
                                           .ref(d.output)
                                           .context_menu([&d] { return output_menu(d.output); })),
                Tab(tr("debug.tab"), debug_panel(d)),
            }
                .selected(d.bottom_tab),
            Orientation::Vertical, 220)
          .keep_second()
          .min_pane(90)
          .flex(),
  }
      .spacing(0);
}

Node code_mode(Designer& d) {
  return Split(project_tree(d), code_view(d), Orientation::Horizontal, 230).min_pane(160);
}

// ── settings: Kits page ──────────────────────────────────────────────────────

Node kits_settings(Designer& d) {
  auto summary = pulse::combine_latest(
      i18n::current().as_observable(), d.detecting.as_observable(), d.toolchains.as_observable(),
      [](const std::string&, bool busy, const Toolchains& t) {
        if (busy) return i18n::text("kits.detecting");
        if (t.kits.empty()) return i18n::text("kits.none");
        return i18n::text("kits.found", std::to_string(t.kits.size()));
      });
  auto tool_text = [&d](bool cmake) {
    return pulse::combine_latest(i18n::current().as_observable(), d.toolchains.as_observable(),
                                 [cmake](const std::string&, const Toolchains& t) {
                                   const Tool& tool = cmake ? t.cmake : t.ninja;
                                   return tool.found() ? tool.version + "  —  " + tool.path : i18n::text("kits.not_found");
                                 });
  };
  auto detail = [](const char* key, const std::string& value) -> Node {
    if (value.empty()) return Spacer().flex(0).height(0);
    return HStack{Text(tr(key)).dim().width(110).font_size(11.5f), Text(value).font_size(11.5f).flex()}.spacing(8);
  };
  auto kits = ForEach(d.toolchains.map([](const Toolchains& t) { return t.kits; }), [detail](const Kit& k) {
    return Panel{
        HStack{Text(k.name).weight(FontWeight::SemiBold).flex(), Text(kind_name(k.kind)).dim().font_size(11.5f)}.spacing(8),
        detail("kits.compiler", k.cxx),
        detail("kits.linker", k.linker),
        detail("kits.target", k.target),
        detail("kits.env", k.env_script),
    }
        .spacing(4)
        .padding(Insets(12, 10));
  });
  kits.spacing(8);
  auto row = [](const char* key, Node value) {
    return HStack{Text(tr(key)).width(130), std::move(value)}.spacing(12).align_items(Alignment::Center);
  };
  return VStack{
      HStack{Text(tr("kits.title")).weight(FontWeight::SemiBold), Spacer(),
             Button(tr("kits.redetect"), [&d] { d.detect_kits(); }).enabled(d.detecting.map([](bool b) { return !b; }))}
          .align_items(Alignment::Center),
      Text(summary).dim(),
      Scroll(std::move(kits).padding(Insets(0, 0, 10, 0))).flex(),
      row("kits.cmake", Text(tool_text(true)).font_size(11.5f).flex()),
      row("kits.ninja", Text(tool_text(false)).font_size(11.5f).flex()),
      row("kits.default", Select(kit_names(d), d.kit_index).flex()),
      row("kits.pulseui", TextField(d.pulseui_dir)
                              .placeholder(std::string(PULSELAYER_PULSEUI_DIR))
                              .tooltip(tr("kits.pulseui.tip"))
                              .flex()),
      row("kits.sign", TextField(d.sign_identity).placeholder(tr("kits.sign.placeholder")).tooltip(tr("kits.sign.tip")).flex()),
  }
      .spacing(10)
      .padding(Insets(4, 14));
}

} // namespace layer
