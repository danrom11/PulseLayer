// Pulse Layer - a visual form designer for PulseUI, written in PulseUI.
#include "panels.hpp"
#include "settings.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>

using namespace pulseui;

namespace layer {
void run_selftest(Designer& d, Window& win);
}
using namespace layer;

int main(int argc, char** argv) {
  App app(argc, argv);

  // ── languages: built-in ones + any lang/*.json next to the executable or in the working dir ──
  i18n::register_builtin();
  if (!app.args.empty()) i18n::load_directory(std::filesystem::path(app.args[0]).parent_path() / "lang");
  i18n::load_directory(std::filesystem::current_path() / "lang");

  std::string cli_lang, file_to_open;
  for (std::size_t i = 1; i < app.args.size(); ++i) {
    const std::string& a = app.args[i];
    if (a == "--lang" && i + 1 < app.args.size()) cli_lang = app.args[++i];
    else if (a == "--export-lang" && i + 1 < app.args.size()) return i18n::export_template(app.args[++i]) ? 0 : 1;
    else if (a.rfind("--", 0) != 0) file_to_open = a;
  }

  // The self test must not depend on (or overwrite) the user's settings.
  const bool selftest = std::getenv("PULSELAYER_SELFTEST") != nullptr;
  Settings settings = selftest ? Settings{} : Settings::load();
  std::string code = !cli_lang.empty() ? cli_lang : settings.language;
  if (i18n::index_of(code) < 0) code = "en";
  i18n::current() = code;

  Designer d;
  d.language = i18n::index_of(code);
  d.dark = settings.dark;
  d.preferred_kit = settings.kit;
  d.pulseui_dir = settings.pulseui_dir;
  d.sign_identity = settings.sign_identity;
  d.build_config = settings.build_config == 1 ? 1 : 0;
  theme() = settings.dark ? Theme::dark() : Theme::light();

  // Selector index <-> language code; both persist to the settings file.
  d.language.changed.connect([&](int i) {
    if (i < 0 || i >= static_cast<int>(i18n::languages().size())) return;
    i18n::current() = i18n::languages()[static_cast<std::size_t>(i)].code;
  });
  i18n::current().changed.connect([&](const std::string& c) {
    d.language = i18n::index_of(c);
    d.doc.status = i18n::Message{"status.language", i18n::languages()[static_cast<std::size_t>(std::max(0, i18n::index_of(c)))].name};
    settings.language = c;
    if (!selftest) settings.save();
  });
  d.theme = settings.dark ? 0 : 1;
  d.theme.changed.connect([&](int t) { d.dark = (t == 0); });
  d.dark.changed.connect([&](bool dark) {
    d.theme = dark ? 0 : 1;
    theme() = dark ? Theme::dark() : Theme::light();
    settings.dark = dark;
    if (!selftest) settings.save();
  });

  // kit choice + PulseUI path persist
  d.kit_index.changed.connect([&](int i) {
    const auto& kits = d.toolchains.get().kits;
    if (i < 0 || i >= static_cast<int>(kits.size())) return;
    d.preferred_kit = settings.kit = kits[static_cast<std::size_t>(i)].id;
    if (!selftest) settings.save();
  });
  d.pulseui_dir.changed.connect([&](const std::string& dir) {
    settings.pulseui_dir = dir;
    if (!selftest) settings.save();
  });
  d.build_config.changed.connect([&](int c) {
    settings.build_config = c;
    if (!selftest) settings.save();
  });
  d.sign_identity.changed.connect([&](const std::string& id) {
    settings.sign_identity = id;
    if (!selftest) settings.save();
  });
  // builder -> UI
  d.builder.output.connect([&](const std::string& line) {
    if (d.output) d.output->append(line + "\n");
  });
  d.builder.app_output.connect([&](const std::string& line) {
    if (d.app_output) d.app_output->append(line + "\n");
  });
  d.builder.note.connect([&](const std::string& key, const std::string& arg) {
    d.doc.status = i18n::Message{key, arg};
    if (key == "run.started") d.show_app_output(i18n::text("run.starting", d.builder.executable()));
    else if (key == "run.exit" && d.app_output) d.app_output->append(i18n::text("run.exit", arg) + "\n");
  });
  d.wire_project();
  d.wire_editor_tools();
  d.wire_debugger();
  d.detect_kits(); // in the background, like Qt Creator

  // The window title is derived state: form name + unsaved marker, in the current language.
  pulse::computed<std::string> title(
      [&] {
        return i18n::text("app.title", d.project.name.get() + " — " + d.doc.title.get() +
                                           (d.project.dirty.get() || d.doc.dirty.get() ? " •" : ""));
      },
      d.project.name, d.project.dirty, d.doc.title, d.doc.dirty, i18n::current());

  Window win(title, 1320, 820);
  win.set_min_size(900, 560);
  d.window = &win;

  win.content(VStack{
      toolbar(d),
      Divider(),
      Show(d.mode.map([](int m) { return m == 0; }),
      Split(palette(d),
            Split(DesignCanvas().ref(d.canvas).flex(),
                  Split(hierarchy(d), inspector(d), Orientation::Vertical, 250).min_pane(120),
                  Orientation::Horizontal, 330)
                .keep_second()
                .min_pane(240),
            Orientation::Horizontal, 190)
          .min_pane(150),
      code_mode(d))
          .flex(),
      Divider(),
      status_bar(d),
  }
                  .spacing(0));

  // The canvas exists now (mounted): it hosts the edited form.
  d.doc.set_host(*d.canvas);
  d.canvas->attach(&d.doc);
  d.cmd_new();
  if (!file_to_open.empty()) d.open_project(file_to_open);

  win.shortcut("Ctrl+N", [&] { d.cmd_new(); })
      .shortcut("Ctrl+O", [&] { d.cmd_open(); })
      .shortcut("Ctrl+S", [&] { d.cmd_save(false); })
      .shortcut("Ctrl+Shift+S", [&] { d.cmd_save(true); })
      .shortcut("Ctrl+Z", [&] { if (d.mode.get() == 0) d.doc.undo(); })
      .shortcut("Ctrl+Y", [&] { if (d.mode.get() == 0) d.doc.redo(); })
      .shortcut("Ctrl+Shift+Z", [&] { if (d.mode.get() == 0) d.doc.redo(); })
      .shortcut("Ctrl+D", [&] { if (d.mode.get() == 0) d.cmd_duplicate(); })
      .shortcut("Ctrl+C", [&] { if (d.mode.get() == 0) d.doc.copy(d.doc.selection.get()); })
      .shortcut("Ctrl+V", [&] { if (d.mode.get() == 0) d.doc.paste(); })
      .shortcut("Delete", [&] { if (d.mode.get() == 0) d.cmd_delete(); })
      .shortcut("Backspace", [&] { if (d.mode.get() == 0) d.cmd_delete(); })
      .shortcut("Ctrl+E", [&] { if (d.mode.get() == 0) d.cmd_export(); })
      .shortcut("F5", [&] {
        if (d.mode.get() == 0) d.cmd_preview();
        else d.cmd_run(); // Code mode: Run in the chosen configuration (Continue when paused)
      })
      .shortcut("Shift+F5", [&] { d.cmd_debug_stop(); })
      .shortcut("F9", [&] { if (d.mode.get() == 1) d.cmd_toggle_breakpoint(); })
      .shortcut("F10", [&] { d.cmd_step_over(); })
      .shortcut("F11", [&] { d.cmd_step_into(); })
      .shortcut("Shift+F11", [&] { d.cmd_step_out(); })
      .shortcut("F1", [&] { d.cmd_settings(2); })
      .shortcut("Ctrl+B", [&] { d.cmd_build_selected(); })
      .shortcut("Ctrl+R", [&] { d.cmd_run(); })
      .shortcut("Ctrl+Shift+R", [&] { d.cmd_run_plain(); })
      .shortcut("Ctrl+1", [&] { d.mode = 0; })
      .shortcut("Ctrl+2", [&] { d.mode = 1; })
      .shortcut("Escape", [&] {
        if (d.mode.get() == 1) {
          if (d.find_open.get()) d.find_close();
          return;
        }
        if (View* s = d.doc.selection.get(); s && s->parent() && s != d.doc.root()) d.doc.selection = s->parent();
      })
      // code editor tools
      .shortcut("Ctrl+F", [&] { d.cmd_find(false); })
      .shortcut("Ctrl+H", [&] { d.cmd_find(true); })
      .shortcut("F3", [&] { d.find_step(false); })
      .shortcut("Shift+F3", [&] { d.find_step(true); })
      .shortcut("F12", [&] { d.cmd_go_to_definition(); })
      .shortcut("F2", [&] { d.cmd_rename(); })
      .shortcut("Alt+Enter", [&] { d.cmd_create_definition(); })
      .shortcut("F4", [&] { d.cmd_switch_header(); })
#ifdef __APPLE__
      .shortcut("Ctrl+Alt+O", [&] { d.cmd_switch_header(); }) // Option+O types a character on macOS
#else
      .shortcut("Alt+O", [&] { d.cmd_switch_header(); })
#endif
      .shortcut("Ctrl+Shift+F", [&] { d.cmd_format(); });

  // Ctrl+click in the editor: go to the definition (after the click is handled)
  if (d.editor)
    d.editor->definition_requested.connect([&d](std::size_t line, std::size_t col) {
      App::post([&d, line, col] { d.go_to_definition(line, col); });
    });

  // Ask before losing unsaved work: closing with changes saves them first.
  win.on_close = [&] {
    if ((d.project.dirty.get() || d.doc.dirty.get()) && !d.project.file.get().empty()) d.save_project(d.project.file.get());
    return true;
  };

  if (std::getenv("PULSELAYER_SELFTEST")) run_selftest(d, win);
  return app.run();
}
