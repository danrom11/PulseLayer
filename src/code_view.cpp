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

void Designer::cmd_build(bool run_after) {
  if (builder.state.get() != Builder::Idle) return;
  if (output) output->clear();
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
  builder.build(r, run_after);
}

void Designer::cmd_run() {
  // always rebuild first: incremental builds are quick and the code may have changed
  cmd_build(true);
}

void Designer::cmd_stop() { builder.stop(); }

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

  return VStack{
      HStack{
          Text(tr("code.kit")).dim(),
          Select(kit_names(d), d.kit_index).width(380).tooltip(tr("code.kit.tip")),
          Button(tr("kits.redetect"), [&d] { d.detect_kits(); }).flat().enabled(d.detecting.map([](bool b) { return !b; })),
          Spacer(),
          Text(state_text).dim(),
          Button(tr("code.build"), [&d] { d.cmd_build(false); }).id("build").tooltip(tr("code.build.tip")).enabled(idle),
          Button(tr("code.run"), [&d] { d.cmd_run(); }).id("run").primary().tooltip(tr("code.run.tip")).enabled(idle),
          Button(tr("code.stop"), [&d] { d.cmd_stop(); }).danger().tooltip(tr("code.stop.tip")).enabled(busy),
      }
          .spacing(8)
          .align_items(Alignment::Center)
          .padding(Insets(12, 6)),
      Split(VStack{
                HStack{Text(d.project.active_file).weight(FontWeight::SemiBold),
                       Text(pulse::combine_latest(i18n::current().as_observable(), d.project.dirty.as_observable(),
                                                  [](const std::string&, bool dirty) {
                                                    return dirty ? i18n::text("code.unsaved") : std::string();
                                                  }))
                           .dim(),
                       Spacer(), Text(tr("code.completion.hint")).dim().font_size(11.5f)}
                    .spacing(8)
                    .padding(Insets(12, 5)),
                CodeEditor(d.editor_text)
                    .highlighter(cpp_highlighter())
                    .completion(make_cpp_completion([&d] { return d.completion_context(); }))
                    .diagnostics(editor_diags)
                    .id("editor")
                    .ref(d.editor)
                    .flex(),
            }
                .spacing(0),
            Tabs{
                Tab(issues_title, Scroll(std::move(issue_list).padding(Insets(6, 4)))),
                Tab(tr("code.output"), CodeEditor().read_only().line_numbers(false).id("output").ref(d.output)),
            },
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
  }
      .spacing(10)
      .padding(Insets(4, 14));
}

} // namespace layer
