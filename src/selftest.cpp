// End-to-end self test: drives the real designer UI with synthetic input.
// Run with PULSELAYER_SELFTEST=1 (optionally PULSEUI_SCREENSHOT=out.png).
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <vector>

#include "cpp_complete.hpp"
#include "panels.hpp"
#include "project.hpp"

namespace layer {

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
  std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", what);
  std::fflush(stdout);
  if (!ok) ++g_failures;
}

InputEvent mouse(InputEvent::Type t, Point p) {
  InputEvent e;
  e.type = t;
  e.pos = e.window_pos = p;
  e.button = MouseButton::Left;
  return e;
}

Point center(View* v) { return v->to_window(v->bounds().center()); }

void click(Window& w, Point p) {
  w.send(mouse(InputEvent::MouseMove, p));
  w.send(mouse(InputEvent::MouseDown, p));
  w.send(mouse(InputEvent::MouseUp, p));
}

void key(Window& w, Key k, unsigned mods = 0) {
  InputEvent e;
  e.type = InputEvent::KeyDown;
  e.key = k;
  e.mods = mods;
  w.send(e);
}

void type_text(Window& w, const std::string& s) {
  InputEvent e;
  e.type = InputEvent::Text;
  e.text = s;
  w.send(e);
}

View* find_if(View* v, const std::function<bool(View*)>& pred) {
  if (pred(v)) return v;
  for (auto& c : v->children())
    if (View* r = find_if(c.get(), pred)) return r;
  return nullptr;
}

// The inspector row editor for property `name`: HStack{ Text(name), editor }.
View* inspector_editor(Window& w, const std::string& name) {
  View* row = find_if(w.root(), [&](View* v) {
    if (std::string(v->type_name()) != "HStack" || v->children().size() != 2) return false;
    auto* t = dynamic_cast<Text*>(v->children()[0].get());
    return t && t->text() == name;
  });
  if (!row) return nullptr;
  View* e = row->children()[1].get();
  // a bindable property: HStack{ Show{ bound member field, normal editor }, link button }
  if (!e->children().empty() && std::string(e->children()[0]->type_name()) == "Show") {
    View* show = e->children()[0].get();
    for (auto& c : show->children())
      if (c->is_visible()) {
        // the bound branch is HStack{ arrow, member field }
        if (std::string(c->type_name()) == "HStack" && c->children().size() == 2) return c->children()[1].get();
        return c.get();
      }
  }
  return e;
}

void layout_now(Window& w) { w.grab(); }

} // namespace

void run_code_tests(Designer& d, Window& win);

void run_selftest(Designer& d, Window& win) {
  static pulse::subscription timer;
  timer = App::after(std::chrono::milliseconds(300), [&d, &win] {
    Document& doc = d.doc;
    layout_now(win);

    // 1) clicking a view on the canvas selects it
    View* ok = doc.root()->find("ok");
    click(win, center(ok));
    check(doc.selection.get() == ok, "canvas click selects #ok");

    // 2) editing a property in the inspector updates the view
    layout_now(win);
    auto* label_field = dynamic_cast<TextField*>(inspector_editor(win, "label"));
    check(label_field != nullptr, "inspector shows a 'label' editor for Button");
    if (label_field) {
      click(win, center(label_field));
      key(win, Key::A, Mod::Primary);
      type_text(win, "Sign in");
      check(static_cast<Button*>(ok)->label() == "Sign in", "inspector edit changes the button label");
    }

    // 3) undo / redo restore the label
    click(win, center(d.canvas)); // focus canvas (selects root or a view)
    key(win, Key::Z, Mod::Primary);
    View* ok2 = doc.root()->find("ok");
    check(ok2 && static_cast<Button*>(ok2)->label() == "OK", "undo restores the label");
    key(win, Key::Y, Mod::Primary);
    ok2 = doc.root()->find("ok");
    check(ok2 && static_cast<Button*>(ok2)->label() == "Sign in", "redo re-applies it");

    // 3a) renaming a view updates the hierarchy
    {
      View* sub = doc.root()->find("subtitle");
      doc.set_prop(sub, meta::property(*sub, "id"), std::string("lead"));
      layout_now(win);
      const bool shown = find_if(win.root(), [](View* v) {
                           auto* t = dynamic_cast<Text*>(v);
                           return t && t->id() == "tree-id" && t->text() == "#lead";
                         }) != nullptr;
      check(shown, "the hierarchy shows a renamed id");
      doc.undo();
    }

    // 3b) bindings: the default form binds name.text -> name and ok.click -> greet
    {
      View* name = doc.root()->find("name");
      doc.selection = name;
      layout_now(win);
      auto* bound = dynamic_cast<TextField*>(inspector_editor(win, "text"));
      check(bound && bound->id() == "bind-text" && bound->text() == "name", "inspector shows the binding of TextField.text");
      auto* cls = win.find<Text>("bind-class-text");
      check(cls && cls->text() == "MainWindow::", "a binding shows the class of the form");
      int marked = 0;
      find_if(win.root(), [&](View* v) {
        if (v->id() == "tree-bound" && v->is_visible()) ++marked;
        return false;
      });
      check(marked == 2, "the hierarchy marks the views with bindings");
      click(win, center(win.find("link-text")));
      layout_now(win);
      check(!name->annotation("bind.text"), "the link button removes a binding");
      click(win, center(win.find("link-text")));
      layout_now(win);
      check(name->annotation("bind.text") && *name->annotation("bind.text") == "name", "the link button binds to a suggested member");
      if (const char* shots = std::getenv("PULSELAYER_SHOTS")) win.screenshot(std::string(shots) + "/bindings_bound.png");
      bound = dynamic_cast<TextField*>(inspector_editor(win, "text"));
      if (bound) {
        click(win, center(bound));
        key(win, Key::A, Mod::Primary);
        type_text(win, "email");
        key(win, Key::Enter);
      }
      check(name->annotation("bind.text") && *name->annotation("bind.text") == "email", "a binding can be renamed in the inspector");
      if (bound) {
        click(win, center(bound));
        key(win, Key::A, Mod::Primary);
        type_text(win, "not valid");
        key(win, Key::Enter);
      }
      check(*name->annotation("bind.text") == "email", "invalid member names are rejected");
      doc.undo();
      name = doc.root()->find("name");
      check(name && *name->annotation("bind.text") == "name", "undo restores a binding");
      View* okv = doc.root()->find("ok");
      doc.selection = okv;
      layout_now(win);
      auto* ev = dynamic_cast<TextField*>(win.find("event-click"));
      check(ev && ev->text() == "greet", "inspector shows the click handler");
      check(doc.form_json().find("\"on\"") != std::string::npos, "bindings are saved in the form");
      const std::string header = Project::ui_header({"mainwindow", "MainWindow", doc.form_json()});
      check(header.find(".ref(name).model(m.name)") != std::string::npos && header.find("pulseui::Button* ok = nullptr;") != std::string::npos &&
                header.find("on_click([&m] { m.greet(); })") != std::string::npos,
            "generated ui header has view pointers and bindings");
      if (const char* shots = std::getenv("PULSELAYER_SHOTS")) win.screenshot(std::string(shots) + "/bindings.png");
    }

    // 4) drag a Checkbox from the palette into the form (below the title)
    layout_now(win);
    View* item = win.find("palette-Checkbox");
    View* title = doc.root()->find("title");
    const std::size_t before = doc.root()->children().size();
    if (item && title) {
      const Point from = center(item);
      const Point to = title->to_window({title->frame().w / 2, title->frame().h - 2});
      win.send(mouse(InputEvent::MouseMove, from));
      win.send(mouse(InputEvent::MouseDown, from));
      win.send(mouse(InputEvent::MouseMove, {from.x + 20, from.y}));
      win.send(mouse(InputEvent::MouseMove, to));
      win.send(mouse(InputEvent::MouseUp, to));
    }
    check(doc.root()->children().size() == before + 1, "palette drag adds a view to the form");
    View* cb = doc.root()->children().size() > 1 ? doc.root()->children()[1].get() : nullptr;
    check(cb && std::string(cb->type_name()) == "Checkbox", "new Checkbox is inserted right after the title");
    check(doc.selection.get() == cb, "the dropped view is selected");

    // 5) delete removes it, undo brings it back
    key(win, Key::Delete);
    check(doc.root()->children().size() == before, "Delete removes the selected view");
    key(win, Key::Z, Mod::Primary);
    check(doc.root()->children().size() == before + 1, "undo restores the deleted view");

    // 6) click-to-insert from the palette into the selected container
    doc.selection = doc.root();
    layout_now(win);
    click(win, center(win.find("palette-Slider")));
    check(std::string(doc.root()->children().back()->type_name()) == "Slider", "palette click appends to the selected container");

    // 7) save -> open round trip, C++ export
    const std::string tmp = (std::filesystem::temp_directory_path() / "pulse_layer_selftest.pform").string();
    const std::string json_before = doc.to_json_text();
    check(doc.save(tmp), "save form");
    doc.new_form();
    check(doc.open(tmp), "open form");
    check(doc.to_json_text() == json_before, "open restores exactly what was saved");
    const std::string code = doc.export_cpp();
    check(code.find("Button(\"Sign in\")") != std::string::npos && code.find("Slider(") != std::string::npos,
          "C++ export contains the edited views");
    std::filesystem::remove(tmp);

    // 8) switching the language re-renders the UI in place
    auto has_button = [&win](const std::string& label) {
      return find_if(win.root(), [&](View* v) {
               auto* b = dynamic_cast<Button*>(v);
               return b && b->label() == label;
             }) != nullptr;
    };
    check(has_button("Save"), "toolbar starts in English");
    d.language = i18n::index_of("ru");
    check(has_button("Сохранить") && !has_button("Save"), "selecting Russian translates the toolbar");
    doc.selection = doc.root()->find("ok");
    layout_now(win);
    check(inspector_editor(win, "подпись") != nullptr, "inspector property labels are translated");
    auto toolbar_shot = [&win](const char* lang) {
      if (const char* shots = std::getenv("PULSELAYER_SHOTS")) {
        layout_now(win);
        win.screenshot(std::string(shots) + "/toolbar_" + lang + ".png");
      }
    };
    toolbar_shot("ru");
    d.language = i18n::index_of("de");
    check(has_button("Speichern"), "German works");
    toolbar_shot("de");
    d.language = i18n::index_of("fr");
    check(has_button("Enregistrer"), "French works");
    toolbar_shot("fr");
    check(i18n::text("no.such.key", {}, "fallback") == "fallback", "missing keys fall back");
    d.language = i18n::index_of("en");
    check(has_button("Save"), "back to English");

    // 9) settings window: language + theme + about
    layout_now(win);
    check(win.find("settings")->to_window({0, 0}).x + win.find("settings")->frame().w <= win.size().w,
          "the toolbar fits the window");
    click(win, center(win.find("settings")));
    check(d.settings_window && d.settings_window->is_open(), "Settings button opens the settings window");
    if (d.settings_window) {
      Window& sw = *d.settings_window;
      layout_now(sw);
      auto* lang = sw.find<Select>("settings-language");
      auto* tabs = sw.find<Tabs>("settings-tabs");
      check(lang && tabs && tabs->titles().size() == 3, "settings has General / Kits / About tabs");
      d.language = i18n::index_of("de");
      check(tabs && tabs->titles()[0] == "Allgemein" && has_button("Speichern"), "tab titles and toolbar follow the language");
      d.language = i18n::index_of("en");
      d.theme = 1;
      check(!d.dark.get() && theme().get().name == "light", "theme selector switches to light");
      d.theme = 0;
      check(d.dark.get() && theme().get().name == "dark", "and back to dark");
      const char* shots = std::getenv("PULSELAYER_SHOTS");
      if (shots) {
        d.language = i18n::index_of("ru");
        sw.screenshot(std::string(shots) + "/settings_general.png");
      }
      d.cmd_settings(2);
      check(tabs && tabs->current() == 2, "F1 / About opens the About tab");
      if (shots) {
        sw.screenshot(std::string(shots) + "/settings_about.png");
        d.language = i18n::index_of("en");
      }
      sw.close();
    }

    // 9b) preview opens a live window
    d.cmd_preview();
    check(d.previews.size() == 1, "preview window opened");

    // 10) panning performance: middle-drag the canvas, render a frame per move
    {
      const Point c = center(d.canvas);
      InputEvent down = mouse(InputEvent::MouseDown, c);
      down.button = MouseButton::Middle;
      win.send(down);
      const auto t0 = std::chrono::steady_clock::now();
      constexpr int kFrames = 60;
      for (int i = 0; i < kFrames; ++i) {
        win.send(mouse(InputEvent::MouseMove, {c.x + static_cast<float>(i % 20) * 6, c.y + static_cast<float>(i % 15) * 4}));
        layout_now(win); // layout + full paint, like one WM_PAINT
      }
      const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / kFrames;
      InputEvent up = mouse(InputEvent::MouseUp, c);
      up.button = MouseButton::Middle;
      win.send(up);
      const auto t1 = std::chrono::steady_clock::now();
      for (int i = 0; i < kFrames; ++i) layout_now(win); // same frames, nothing changed
      const double idle = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count() / kFrames;
      std::printf("[INFO] pan frame: %.2f ms, unchanged frame (measurement overhead): %.2f ms\n", ms, idle);
      // the canvas must stay inside its own bounds (no drawing over the panels)
      check(d.canvas->style().clip, "canvas clips its drawing");
      if (const char* shots = std::getenv("PULSELAYER_SHOTS")) {
        // drag the form far to the left, over where the palette is
        InputEvent dn = mouse(InputEvent::MouseDown, c);
        dn.button = MouseButton::Middle;
        win.send(dn);
        win.send(mouse(InputEvent::MouseMove, {c.x - 900, c.y}));
        InputEvent u = mouse(InputEvent::MouseUp, {c.x - 900, c.y});
        u.button = MouseButton::Middle;
        win.send(u);
        win.screenshot(std::string(shots) + "/pan_left.png");
      }
    }

    run_code_tests(d, win);
  });
}

// ── code mode, kits and (optionally) a real build ────────────────────────────

namespace {

std::vector<pulse::subscription> g_waits;

// Polls `pred` on the UI thread, then calls `then` (or reports a timeout).
void wait_until(std::function<bool()> pred, int timeout_ms, std::function<void(bool)> then) {
  auto start = std::chrono::steady_clock::now();
  auto done = std::make_shared<bool>(false);
  g_waits.push_back(App::every(std::chrono::milliseconds(100), [pred, timeout_ms, then, start, done] {
    if (*done) return;
    const bool ok = pred();
    const bool late = std::chrono::steady_clock::now() - start > std::chrono::milliseconds(timeout_ms);
    if (ok || late) {
      *done = true;
      then(ok);
    }
  }));
}

void finish() {
  g_waits.clear(); // cancel pending waits while the app is still alive
  std::printf("selftest: %s (%d failures)\n", g_failures ? "FAILED" : "OK", g_failures);
  std::fflush(stdout);
  if (!std::getenv("PULSEUI_SCREENSHOT")) App::quit(g_failures ? 1 : 0);
}

const char* kBrokenCode = R"(#include "mainwindow.hpp"
using namespace pulseui;

int main() {
    App app;
    MainWindow window;
    window.show();
    int answer = undefined_thing;
    return app.run();
}
)";

void build_tests(Designer& d, Window& win);
void sample_tests(Designer& d, std::vector<std::filesystem::path> projects, std::size_t i);
void choose_kit(Designer& d);

bool has_item(const CompletionResult& r, const std::string& label) {
  for (auto& i : r.items)
    if (i.label == label) return true;
  return false;
}

std::vector<std::string> split(const std::string& text) {
  std::vector<std::string> out(1);
  for (char c : text) {
    if (c == '\n') out.emplace_back();
    else out.back() += c;
  }
  return out;
}

// Completion at the "|" marker of `code`.
CompletionResult complete_at(const std::string& code, const CompletionContext& ctx) {
  const auto pos = code.find('|');
  std::string text = code;
  text.erase(pos, 1);
  const auto lines = split(text);
  std::size_t line = 0, col = 0, i = 0;
  for (; i < pos; ++i) {
    if (text[i] == '\n') { ++line; col = 0; }
    else ++col;
  }
  return complete_cpp(lines, line, col, ctx);
}

void completion_tests(Designer& d) {
  CompletionContext ctx = d.completion_context();
  ctx.current_file = "mainwindow.cpp";
  const std::string head = "#include \"mainwindow.hpp\"\nusing namespace pulseui;\n\n"
                           "void helper() {\n    int hidden = 1;\n}\n\n"
                           "MainWindow::MainWindow() {\n    window_.content(Ui::MainWindow::build());\n"
                           "    int counter = 0;\n    auto* ok = window_.find<Button>(\"ok\");\n";
  check(has_item(complete_at(head + "    for (int index = 0; index < 3; ++index) {\n        co|\n    }\n}\n", ctx), "counter"),
        "completion: local variable of the enclosing scope");
  check(has_item(complete_at(head + "    for (int index = 0; index < 3; ++index) {\n        ind|\n    }\n}\n", ctx), "index"),
        "completion: for-loop variable");
  check(!has_item(complete_at(head + "    hid|\n}\n", ctx), "hidden"), "completion: locals of other functions are not visible");
  check(has_item(complete_at(head + "    na|\n}\n", ctx), "name"), "completion: members of the class inside its methods");
  auto members = complete_at(head + "    window_.|\n}\n", ctx);
  check(has_item(members, "content") && has_item(members, "find") && has_item(members, "set_title"),
        "completion: members after '.' (Window)");
  check(has_item(complete_at(head + "    ok->|\n}\n", ctx), "clicked"), "completion: type of auto* x = find<Button>() after '->'");
  auto ids = complete_at(head + "    window_.find<Button>(\"|\n}\n", ctx);
  check(has_item(ids, "ok"), "completion: view ids of the forms inside find<T>(\"...\")");
  check(has_item(complete_at(head + "    Ui::|\n}\n", ctx), "MainWindow"), "completion: Ui:: lists the forms");
  check(has_item(complete_at(head + "    Alignment::|\n}\n", ctx), "Center"), "completion: enum scope");
  check(has_item(complete_at(head + "    name.|\n}\n", ctx), "changed"), "completion: property<T> members");
  check(complete_at(head + "    // co|\n}\n", ctx).items.empty(), "completion: nothing inside comments");
}

} // namespace

void run_code_tests(Designer& d, Window& win) {
  // compiler output parsing
  Issue is;
  check(parse_issue(R"(D:\p\src\main.cpp(8,18): error C2065: 'undefined_thing': undeclared identifier)", is) &&
            is.line == 8 && is.column == 18 && is.severity == Diagnostic::Error,
        "parses MSVC errors");
  check(parse_issue("/p/src/main.cpp:8:18: error: 'undefined_thing' was not declared in this scope", is) &&
            is.line == 8 && is.message.find("undefined_thing") != std::string::npos,
        "parses GCC / Clang errors");
  check(parse_issue("main.cpp.obj : error LNK2019: unresolved external symbol foo", is) && is.line == 0,
        "parses linker errors");

  // ── project with several forms ──
  check(d.project.forms.size() == 1 && d.project.find_file("main.cpp") && d.project.find_file("mainwindow.hpp") &&
            d.project.find_file("mainwindow.cpp"),
        "new project: MainWindow form + main.cpp, mainwindow.hpp/.cpp");
  std::string err;
  check(!d.add_form("1bad", &err) && err == "invalid", "invalid class names are rejected");
  check(d.add_form("LoginDialog"), "add a second form");
  check(d.project.forms.size() == 2 && d.project.find_file("logindialog.hpp") && d.project.find_file("logindialog.cpp"),
        "the form gets logindialog.pform + .hpp + .cpp");
  check(d.doc.title.get() == "LoginDialog" && d.doc.root()->find("title"), "the new form is opened on the canvas");
  check(Project::ui_header(d.project.forms[1]).find("struct LoginDialog") != std::string::npos,
        "ui_logindialog.hpp declares Ui::LoginDialog");
  d.project.active_form = 0;
  check(d.doc.root()->find("ok") != nullptr, "switching back restores the first form");
  d.project.active_form = 1;
  d.doc.insert_near_selection("Button");
  d.project.active_form = 0;
  d.project.active_form = 1;
  check(d.doc.root()->children().size() == 2, "edits of a form survive switching forms");
  d.project.active_form = 0;

  d.mode = 1;
  layout_now(win);
  check(win.find("tree-form-1") && win.find("tree-file-logindialog.cpp"), "project tree lists forms and files");
  click(win, center(win.find("tree-file-logindialog.cpp")));
  check(d.project.active_file.get() == "logindialog.cpp" && d.editor &&
            d.editor->text().find("LoginDialog::LoginDialog") != std::string::npos,
        "clicking a file opens it in the editor");
  click(win, center(win.find("tree-form-1")));
  check(d.mode.get() == 0 && d.project.active_form.get() == 1, "clicking a form opens it in Design mode");
  d.mode = 1;
  layout_now(win);

  // new file through the dialog
  click(win, center(win.find("add-file")));
  check(d.dialog_window && d.dialog_window->is_open(), "+ File opens the name dialog");
  if (d.dialog_window) {
    Window& dw = *d.dialog_window;
    layout_now(dw);
    click(dw, center(dw.find("dialog-ok")));
  }
  check(d.project.find_file("utils.hpp") && d.project.active_file.get() == "utils.hpp", "the dialog creates utils.hpp");
  d.remove_file("utils.hpp");
  check(!d.project.find_file("utils.hpp") && d.project.active_file.get() == "main.cpp", "removing a file");

  // editing a file keeps its text when switching
  d.project.active_file = "main.cpp";
  d.editor_text = d.editor_text.get() + "// edited\n";
  d.project.active_file = "mainwindow.cpp";
  d.project.active_file = "main.cpp";
  check(d.editor->text().find("// edited") != std::string::npos, "file edits survive switching files");

  // save / open the whole project
  const auto dir = std::filesystem::temp_directory_path() / "pulse_layer_selftest_project";
  std::filesystem::remove_all(dir);
  check(d.save_project((dir / "Demo.pulse").string()), "save project");
  check(std::filesystem::exists(dir / "logindialog.pform") && std::filesystem::exists(dir / "mainwindow.cpp"),
        "project files are written");
  d.cmd_new();
  check(d.open_project((dir / "Demo.pulse").string()) && d.project.forms.size() == 2 && d.project.name.get() == "Demo",
        "open project");

  completion_tests(d);

  // completion in the live editor: typing a variable of main.cpp
  d.project.active_file = "main.cpp";
  layout_now(win);
  click(win, center(d.editor));
  InputEvent end;
  end.type = InputEvent::KeyDown;
  end.key = Key::End;
  end.mods = Mod::Primary;
  win.send(end);
  type_text(win, "\nint value = 0;\nva");
  check(d.editor->completion_open() && !d.editor->completion_items().empty() &&
            d.editor->completion_items()[0].label == "value",
        "live editor shows suggestions while typing");
  if (const char* shots = std::getenv("PULSELAYER_SHOTS")) win.screenshot(std::string(shots) + "/completion.png");
  key(win, Key::Escape);
  check(!d.editor->completion_open(), "Escape closes the suggestions");
  d.open_file("main.cpp");

  // kits are detected in the background
  wait_until([&d] { return !d.detecting.get(); }, 90000, [&d, &win](bool ok) {
    check(ok, "kit detection finishes");
    const auto& tc = d.toolchains.get();
    for (auto& k : tc.kits) std::printf("[INFO] kit: %s\n", k.name.c_str());
    std::printf("[INFO] cmake: %s %s, ninja: %s %s\n", tc.cmake.version.c_str(), tc.cmake.path.c_str(),
                tc.ninja.version.c_str(), tc.ninja.path.c_str());
    check(!tc.kits.empty(), "at least one C++ kit found");
    check(tc.cmake.found(), "CMake found");
    d.cmd_settings(1);
    check(d.settings_window && d.settings_window->find<Tabs>("settings-tabs")->current() == 1, "Kits page opens");
    if (const char* shots = std::getenv("PULSELAYER_SHOTS")) d.settings_window->screenshot(std::string(shots) + "/kits.png");
    d.settings_window->close();
    if (const char* samples = std::getenv("PULSELAYER_SELFTEST_SAMPLES")) {
      // copy every sample project to a temp folder (builds write next to the project) and build it
      namespace fs = std::filesystem;
      const fs::path root = fs::temp_directory_path() / "pulse_layer_samples";
      std::error_code ec;
      std::vector<fs::path> projects;
      for (auto& e : fs::directory_iterator(samples, ec)) {
        if (!e.is_directory()) continue;
        for (auto& f : fs::directory_iterator(e.path(), ec)) {
          if (f.path().extension() != ".pulse") continue;
          const fs::path dst = root / e.path().filename();
          fs::create_directories(dst, ec);
          for (auto& item : fs::directory_iterator(e.path(), ec))
            if (item.is_regular_file())
              fs::copy_file(item.path(), dst / item.path().filename(), fs::copy_options::overwrite_existing, ec);
          projects.push_back(dst / f.path().filename());
        }
      }
      check(!projects.empty(), "sample projects found");
      choose_kit(d);
      sample_tests(d, projects, 0);
    } else if (std::getenv("PULSELAYER_SELFTEST_BUILD")) {
      build_tests(d, win);
    } else {
      finish();
    }
  });
}

namespace {

void choose_kit(Designer& d) {
  // optional: choose a kit by a name fragment, e.g. PULSELAYER_SELFTEST_KIT=MinGW
  if (const char* want = std::getenv("PULSELAYER_SELFTEST_KIT")) {
    const auto& kits = d.toolchains.get().kits;
    for (std::size_t i = 0; i < kits.size(); ++i)
      if (kits[i].name.find(want) != std::string::npos) d.kit_index = static_cast<int>(i);
  }
  std::printf("[INFO] building with: %s\n", d.toolchains.get().kits[static_cast<std::size_t>(d.kit_index.get())].name.c_str());
  std::fflush(stdout);
}

// Opens and builds every sample project, one after another.
void sample_tests(Designer& d, std::vector<std::filesystem::path> projects, std::size_t i) {
  if (i >= projects.size()) return finish();
  const std::string name = projects[i].stem().string();
  check(d.open_project(projects[i].string()), ("open sample " + name).c_str());
  d.mode = 1;
  const auto t0 = std::chrono::steady_clock::now();
  d.cmd_build(false);
  wait_until([&d] { return d.builder.state.get() == Builder::Idle; }, 15 * 60 * 1000,
             [&d, projects = std::move(projects), i, name, t0](bool done) mutable {
               const bool ok = done && d.builder.last_build_ok();
               std::printf("[INFO] %s built in %.0f s\n", name.c_str(),
                           std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
               for (auto& is : d.builder.issues.get())
                 std::printf("  %s %s:%d:%d: %s\n", is.severity == Diagnostic::Error ? "error" : "warning",
                             is.file.c_str(), is.line, is.column, is.message.c_str());
               check(ok, ("sample " + name + " builds").c_str());
               if (ok) std::printf("[INFO] executable: %s\n", d.builder.executable().c_str());
               std::fflush(stdout);
               sample_tests(d, std::move(projects), i + 1);
             });
}

void build_tests(Designer& d, Window& win) {
  choose_kit(d);
  d.mode = 1;
  d.project.active_file = "main.cpp";
  d.editor_text = kBrokenCode;
  layout_now(win);
  click(win, center(win.find("build")));
  check(d.builder.state.get() != Builder::Idle, "Build button starts a build");
  const auto t0 = std::chrono::steady_clock::now();
  wait_until([&d] { return d.builder.state.get() == Builder::Idle; }, 15 * 60 * 1000, [&d, &win, t0](bool done) {
    std::printf("[INFO] first build took %.0f s\n", std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
    check(done && !d.builder.last_build_ok(), "broken program fails to build");
    bool found = false;
    for (auto& i : d.builder.issues.get())
      if (i.user_code && i.file == "main.cpp" && i.line == 8 && i.severity == Diagnostic::Error) found = true;
    check(found, "the error is reported on line 8 of main.cpp");
    check(d.editor && !d.editor->diagnostics_value().empty() && d.editor->diagnostics_value()[0].line == 8,
          "the editor underlines line 8");
    d.project.active_file = "mainwindow.cpp";
    check(d.editor->diagnostics_value().empty(), "other files show no squiggles");
    d.project.active_file = "main.cpp";
    check(d.output && d.output->line_count() > 3, "build output is shown");
    if (const char* shots = std::getenv("PULSELAYER_SHOTS")) win.screenshot(std::string(shots) + "/code_error.png");

    // fix it and build + run: two forms, three .cpp files
    d.editor_text = Project::main_template("MainWindow");
    const auto t1 = std::chrono::steady_clock::now();
    d.cmd_run();
    wait_until([&d] { return d.builder.state.get() == Builder::Running || (d.builder.state.get() == Builder::Idle && !d.builder.last_build_ok()); },
               10 * 60 * 1000, [&d, &win, t1](bool) {
                 std::printf("[INFO] incremental build took %.1f s\n", std::chrono::duration<double>(std::chrono::steady_clock::now() - t1).count());
                 check(d.builder.last_build_ok(), "fixed multi-form program builds");
                 check(d.editor->diagnostics_value().empty(), "errors are cleared");
                 check(std::filesystem::exists(d.builder.executable()), "executable exists");
                 check(d.builder.state.get() == Builder::Running, "Run starts the program");
                 if (const char* shots = std::getenv("PULSELAYER_SHOTS")) win.screenshot(std::string(shots) + "/code_ok.png");
                 App::after(std::chrono::milliseconds(1500), [&d] {
                   d.cmd_stop();
                   wait_until([&d] { return d.builder.state.get() == Builder::Idle; }, 10000, [](bool stopped) {
                     check(stopped, "Stop terminates the program");
                     finish();
                   });
                 }).release();
               });
  });
}

} // namespace

} // namespace layer
