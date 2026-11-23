// End-to-end self test: drives the real designer UI with synthetic input.
// Run with PULSELAYER_SELFTEST=1 (optionally PULSEUI_SCREENSHOT=out.png).
#include <algorithm>
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
      // the binding field suggests members of the form's class (from the code analyzer)
      bound = dynamic_cast<TextField*>(inspector_editor(win, "text"));
      if (bound) {
        click(win, center(bound));
        bool has_name = false, has_method = false;
        for (auto& s : bound->current_suggestions()) {
          has_name = has_name || (s.label == "name" && s.detail.find("property") != std::string::npos);
          has_method = has_method || s.label == "greet()";
        }
        check(bound->suggestions_open() && has_name && !has_method, "the binding field suggests the members of the form class");
        if (const char* shots = std::getenv("PULSELAYER_SHOTS")) win.screenshot(std::string(shots) + "/binding_suggestions.png");
        key(win, Key::Escape);
        check(!bound->suggestions_open(), "Escape closes the binding suggestions");
      }
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
      if (ev) {
        win.set_focus(ev); // the row may be scrolled out of the inspector
        bool greet = false;
        for (auto& s : ev->current_suggestions()) {
          greet = greet || (s.label == "greet()" && s.value == "greet");
        }
        check(greet, "the event field suggests the methods of the form class");
        key(win, Key::Escape);
      }
      check(doc.form_json().find("\"on\"") != std::string::npos, "bindings are saved in the form");
      const std::string header = Project::ui_header({"mainwindow", "MainWindow", doc.form_json()});
      check(header.find(".ref(name).model(m.name)") != std::string::npos && header.find("pulseui::Button* ok = nullptr;") != std::string::npos &&
                header.find("on_click([&m] { m.greet(); })") != std::string::npos,
            "generated ui header has view pointers and bindings");
      if (const char* shots = std::getenv("PULSELAYER_SHOTS")) win.screenshot(std::string(shots) + "/bindings.png");
    }

    // 3c) lists: a ForEach row template bound to a std::vector member
    {
      if (SourceFile* h = d.project.find_file("mainwindow.hpp")) {
        const std::string anchor = "    pulse::property<std::string> name;\n";
        const auto at = h->text.find(anchor);
        if (at != std::string::npos)
          h->text.insert(at + anchor.size(), "    struct Todo { std::string title; bool done = false; };\n"
                                             "    pulse::property<std::vector<Todo>> todos;\n"
                                             "    void remove(const Todo& t);\n");
      }
      doc.selection = doc.root();
      View* list = doc.insert("ForEach", doc.root(), -1);
      check(list && std::string(list->type_name()) == "ForEach" && list->children().size() == 1,
            "a new ForEach comes with a row template");
      layout_now(win);
      const std::string list_id = list ? list->id() : std::string("-");
      // items: link it, clear the suggested name: the vector member comes first
      if (win.find("link-items")) click(win, center(win.find("link-items")));
      layout_now(win);
      auto* items = dynamic_cast<TextField*>(inspector_editor(win, "items"));
      bool vector_first = false;
      if (items) {
        win.set_focus(items);
        key(win, Key::A, Mod::Primary);
        key(win, Key::Backspace);
        const auto& s = items->current_suggestions();
        vector_first = !s.empty() && s.front().label == "todos";
        key(win, Key::Escape);
      }
      check(vector_first, "items suggests the std::vector members first");
      list = doc.root()->find(list_id);
      if (list) doc.set_binding(list, "bind.items", "todos");
      // inside the row: item.<field> suggestions from the element type
      View* row_text = list ? find_if(list, [](View* v) { return std::string(v->type_name()) == "Text"; }) : nullptr;
      if (row_text) {
        doc.selection = row_text;
        layout_now(win);
        click(win, center(win.find("link-text")));
        layout_now(win);
        auto* f = dynamic_cast<TextField*>(inspector_editor(win, "text"));
        bool title = false;
        if (f) {
          win.set_focus(f);
          key(win, Key::A, Mod::Primary);
          type_text(win, "it");
          for (auto& s : f->current_suggestions()) title = title || (s.label == "item.title" && s.detail.find("string") != std::string::npos);
          if (const char* shots = std::getenv("PULSELAYER_SHOTS")) win.screenshot(std::string(shots) + "/list_template.png");
          key(win, Key::Escape);
        }
        check(title, "a row view suggests the fields of the item (item.title)");
        doc.set_binding(row_text, "bind.text", "item.title");
      }
      View* row_box = list ? find_if(list, [](View* v) { return std::string(v->type_name()) == "Checkbox"; }) : nullptr;
      if (row_box) doc.set_binding(row_box, "bind.checked", "item.done");
      const std::string header = Project::ui_header({"mainwindow", "MainWindow", doc.form_json()});
      check(header.find("ForEach(m.todos, [&](const auto& item, std::size_t index) -> pulseui::Node {") != std::string::npos &&
                header.find(".bind_text(item.title)") != std::string::npos && header.find(".bind_checked(item.done)") != std::string::npos,
            "the generated header builds the list from the template");
      // undo removes the list again (and the header change stays out of the default project)
      for (int guard = 0; guard < 20 && doc.root()->find(list_id) && doc.can_undo(); ++guard) doc.undo();
      if (SourceFile* h = d.project.find_file("mainwindow.hpp")) {
        const auto at = h->text.find("    struct Todo");
        if (at != std::string::npos) h->text.erase(at, h->text.find("void remove(const Todo& t);\n") + 28 - at);
      }
      check(!doc.root()->find(list_id), "undo removes the list");
      check(doc.root()->find("ok") && static_cast<Button*>(doc.root()->find("ok"))->label() == "Sign in", "earlier edits stay");
    }

    // 3d) preview board: the form in both themes, a wider window and a pseudo-translation
    {
      d.cmd_preview_board();
      App::process_events(std::chrono::milliseconds(50));
      View* cards = d.board_window ? d.board_window->find("board-cards") : nullptr;
      check(cards && cards->children().size() == 6, "the preview board shows 6 variants (2 themes x normal, wide, pseudo)");
      bool images = cards != nullptr;
      if (cards)
        find_if(cards, [&](View* v) {
          if (auto* iv = dynamic_cast<ImageView*>(v); iv && iv->frame().w <= 0) images = false;
          return false;
        });
      if (auto* compact = d.board_window ? d.board_window->find<Checkbox>("board-compact") : nullptr) compact->set_checked(true);
      App::process_events(std::chrono::milliseconds(600));
      cards = d.board_window ? d.board_window->find("board-cards") : nullptr;
      check(cards && cards->children().size() == 8, "compact sizes add two more variants");
      check(theme().get().name == (d.dark.get() ? "dark" : "light"), "rendering the variants restores the theme");
      if (const char* shots = std::getenv("PULSELAYER_SHOTS"); shots && d.board_window) {
        d.board_window->update_layout();
        d.board_window->screenshot(std::string(shots) + "/preview_board.png");
      }
      if (d.board_window) d.board_window->close();
      App::process_events(std::chrono::milliseconds(50));
      (void)images;
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
void debug_tests(Designer& d);
bool app_output_has(Designer& d, const std::string& s);
void release_tests(Designer& d);
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
CompletionResult complete_at(const std::string& code, const CompletionContext& ctx, char marker = '|') {
  const auto pos = code.find(marker);
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

  // types of whole expressions
  auto has = [&](const std::string& code, const std::string& label) { return has_item(complete_at(code, ctx), label); };
  auto insert_of = [&](const std::string& code, const std::string& label) {
    for (auto& i : complete_at(code, ctx).items)
      if (i.label == label) return i.insert;
    return std::string("<missing>");
  };
  const std::string body = head + "    ";
  check(has(body + "ui_.ok->clicked.|\n}\n", "connect"), "completion: designed view -> signal members (ui_.ok->clicked.)");
  check(has(body + "name.get().|\n}\n", "substr"), "completion: property<std::string>::get() is a string");
  check(has(body + "window_.find<Button>(\"ok\")->|\n}\n", "clicked"), "completion: find<T>(...)-> is a T");
  check(has(body + "window_.root()->|\n}\n", "children"), "completion: method result types (root() -> View*)");
  check(has(body + "auto s = name.get();\n    s.|\n}\n", "substr"), "completion: auto from an expression");
  check(has(body + "Button(\"x\").primary().|\n}\n", "flat"), "completion: chained modifiers keep the view type");
  check(has(body + "th().accent.|\n}\n", "with_alpha"), "completion: th().accent is a Color");
  check(has(body + "App::ui().|\n}\n", "post"), "completion: static call results (App::ui())");
  check(has(body + "this->|\n}\n", "name"), "completion: this-> lists the class members");
  check(has(body + "Ui::MainWindow::|\n}\n", "form_title"), "completion: Ui::Form:: constants");
  check(insert_of(body + "window_.sh|\n}\n", "show") == "show()", "completion: calls without arguments get ()");
  check(insert_of(body + "window_.resi|\n}\n", "resize") == "resize($0)", "completion: the caret goes inside (...)");
  check(insert_of(body + "window_.resi|(1, 2);\n}\n", "resize").empty(), "completion: no () when one follows");
  {
    const auto r = complete_at(body + "window_.resize(10, |\n}\n", ctx);
    check(r.signature.find("resize(") == 0 &&
              r.signature.substr(r.active_begin, r.active_end - r.active_begin) == "float h",
          "completion: parameter hint with the current parameter");
  }
  const std::string types = "#include <pulseui/pulseui.hpp>\nusing namespace pulseui;\n"
                            "struct Task { std::string title; bool done = false; };\n"
                            "enum class Mode { Edit, Preview };\n"
                            "class Gauge : public ViewT<Gauge> {\npublic:\n    float value = 0;\n";
  check(has(types + "};\nvoid f(std::vector<Task>& tasks) {\n    for (auto& t : tasks) {\n        t.|\n    }\n}\n", "title"),
        "completion: range-for element of std::vector<T>");
  check(has(types + "};\nvoid f(std::vector<Task>& tasks) {\n    tasks[0].|\n}\n", "done"), "completion: operator[] of a vector");
  check(has(types + "};\nvoid f(std::vector<Task>& tasks) {\n    tasks.front().|\n}\n", "title"), "completion: T& front()");
  check(has(types + "};\nvoid f() {\n    Mode::|\n}\n", "Preview"), "completion: enumerators of a project enum");
  check(has(types + "    void paint_content(Painter& p) override {\n        this->|\n    }\n};\n", "bounds"),
        "completion: members inherited from PulseUI base classes");
  check(has(types + "    void paint_content(Painter& p) override {\n        p.|\n    }\n};\n", "fill_rect"),
        "completion: Painter parameters");
  check(has(types + "};\nauto c = Canvas([](Painter& p, const Rect& r) {\n    r.|\n});\n", "center"),
        "completion: lambda parameters");
  check(has_item(complete_at(types + "};\nvoid f(pulse::property<int>& v) {\n    auto s = v.as_observable() | deb@\n}\n", ctx, '@'), "debounce"),
        "completion: pulse operators after |");
  check(insert_of(types + "};\nvoid f() {\n    fo|\n}\n", "for").find("$0") != std::string::npos, "completion: snippets");
  check(has("#include <pulseui/|\n", "pulseui/pulseui.hpp") && has("#include \"main|\n", "mainwindow.hpp"),
        "completion: #include paths");
  check(has("#include \"mainwindow.hpp\"\nvoid MainWindow::gre|\n", "greet") &&
            insert_of("#include \"mainwindow.hpp\"\nvoid MainWindow::gre|\n", "greet") == "greet()",
        "completion: out-of-class definitions get the declared parameters");
  check(has("MainWindow::MainWindow() : ok_label([this] { return na|; }, name) {\n}\n", "name") &&
            has("MainWindow::MainWindow() : ok_label([this] { return 1; }, name) {\n    wi|\n}\n", "window_"),
        "completion: members inside constructors with member initializers");
  {
    CompletionContext hpp = ctx; // members declared below an inline method
    hpp.current_file = "mainwindow.hpp";
    const std::string code = "class MainWindow {\npublic:\n    void show() { window_.|\n }\nprivate:\n"
                             "    pulseui::Window window_{\"t\", 1, 2};\n};\n";
    check(has_item(complete_at(code, hpp), "show") && has_item(complete_at(code, hpp), "set_title"),
          "completion: members declared after the method that uses them");
  }
}

struct Caret {
  std::vector<std::string> lines;
  std::size_t line = 0, col = 0;
};
Caret caret_at(const std::string& code) { // "|" marks the position
  Caret c;
  const auto pos = code.find('|');
  std::string text = code;
  text.erase(pos, 1);
  c.lines = split(text);
  for (std::size_t i = 0; i < pos; ++i) {
    if (text[i] == '\n') { ++c.line; c.col = 0; }
    else ++c.col;
  }
  return c;
}

int line_of(const std::vector<std::string>& lines, const std::string& needle) { // 1-based, 0 = missing
  for (std::size_t i = 0; i < lines.size(); ++i)
    if (lines[i].find(needle) != std::string::npos) return static_cast<int>(i) + 1;
  return 0;
}

void editor_tools_tests(Designer& d, Window& win) {
  CompletionContext ctx = d.completion_context();
  ctx.current_file = "mainwindow.cpp";
  auto def = [&](const std::string& code) {
    const Caret c = caret_at(code);
    return find_definition(c.lines, c.line, c.col, ctx);
  };
  auto info = [&](const std::string& code) {
    const Caret c = caret_at(code);
    return describe_symbol(c.lines, c.line, c.col, ctx);
  };
  const std::string src = "#include \"mainwindow.hpp\"\n\nvoid MainWindow::greet() {\n}\n\nvoid MainWindow::other() {\n"
                          "    int counter = 1;\n";
  {
    auto l = def(src + "    gr|eet();\n}\n");
    check(l && l->file == "mainwindow.cpp" && l->line == 3, "go to definition: a method call jumps to its body");
    l = def("#include \"mainwindow.hpp\"\n\nvoid MainWindow::gr|eet() {\n}\n");
    check(l && l->file == "mainwindow.hpp", "go to definition: on the body it jumps to the declaration");
    l = def(src + "    count|er++;\n}\n");
    check(l && l->file == "mainwindow.cpp" && l->line == 7 && l->column == 9, "go to definition: local variables");
    l = def(src + "    window|_.show();\n}\n");
    check(l && l->file == "mainwindow.hpp", "go to definition: members declared in the header");
    l = def(src + "    ui_.o|k->set_label(\"x\");\n}\n");
    check(l && l->form == "MainWindow" && l->view == "ok", "go to definition: a designed view opens the form");
    check(!def(src + "    window_.sh|ow();\n}\n"), "go to definition: library functions have no project location");
    l = def(src + "    MainWin|dow* self = this;\n}\n");
    check(l && l->file == "mainwindow.hpp", "go to definition: class names");
  }
  check(info(src + "    window|_.show();\n}\n").find("Window") != std::string::npos, "hover: member variables show their type");
  check(info(src + "    window_.resi|ze(1, 2);\n}\n").find("resize(float w, float h)") != std::string::npos,
        "hover: library functions show their signature");
  check(info(src + "    gr|eet();\n}\n").find("void MainWindow::greet()") != std::string::npos, "hover: project functions");
  check(info(src + "    ui_.o|k->set_label(\"x\");\n}\n").find("Button") != std::string::npos, "hover: designed views");
  check(info(src + "    // gr|eet\n}\n").empty(), "hover: nothing inside comments");
  check(find_occurrences({"int a = 1; // a", "f(a, \"a\");"}, "a").size() == 2, "rename finds code only (no comments, strings)");
  {
    const Caret c = caret_at(src + "    count|er++;\n}\n");
    const Caret m = caret_at(src + "    na|me.get();\n}\n");
    check(!symbol_is_shared(c.lines, c.line, c.col, ctx) && symbol_is_shared(m.lines, m.line, m.col, ctx),
          "rename: locals stay in the file, members go project-wide");
  }
  {
    CompletionContext hpp = ctx;
    hpp.current_file = "mainwindow.hpp";
    const auto lines = split("class Foo {\npublic:\n    void save(int x = 3) const;\n    int size();\n"
                             "    void inl() {}\n    virtual void v() = 0;\n};\n");
    const auto s2 = definition_stub(lines, 2, hpp), s3 = definition_stub(lines, 3, hpp), s4 = definition_stub(lines, 4, hpp);
    check(s2 && s2->file == "mainwindow.cpp" && s2->text.rfind("void Foo::save(int x) const {", 0) == 0,
          "create definition: the stub goes to the .cpp, without default arguments");
    check(s3 && s3->text.rfind("int Foo::size() {", 0) == 0, "create definition: the return type is kept");
    check(s4 && s4->existing, "create definition: functions with a body are found, not duplicated");
    check(!definition_stub(lines, 5, hpp), "create definition: pure virtual functions get no body");
  }

  // ── live editor ──
  d.mode = 1;
  d.project.active_file = "mainwindow.cpp";
  d.project.active_file = "main.cpp";
  layout_now(win);
  const auto tabs = d.open_files.get();
  check(std::find(tabs.begin(), tabs.end(), "mainwindow.cpp") != tabs.end() &&
            std::find(tabs.begin(), tabs.end(), "main.cpp") != tabs.end() && win.find("tab-mainwindow.cpp"),
        "opened files appear as tabs");
  click(win, center(win.find("tab-mainwindow.cpp")));
  check(d.project.active_file.get() == "mainwindow.cpp", "clicking a tab opens its file");
  key(win, Key::F4);
  check(d.project.active_file.get() == "mainwindow.hpp", "F4 switches to the header");
  key(win, Key::F4);
  check(d.project.active_file.get() == "mainwindow.cpp", "F4 switches back to the source");

  // F12 on window_ in greet() -> its declaration in the header
  {
    const auto& lines = d.editor->lines();
    const int l = line_of(lines, "window_.set_title");
    const int c = l ? static_cast<int>(lines[static_cast<std::size_t>(l - 1)].find("window_")) + 3 : 1;
    d.editor->go_to(l, c);
    key(win, Key::F12);
    check(d.project.active_file.get() == "mainwindow.hpp" &&
              d.editor->caret_line() == line_of(d.editor->lines(), "pulseui::Window window_"),
          "F12 jumps to the declaration");
    key(win, Key::F4);
  }
  check(d.go_to(SourceLocation{{}, 0, 0, "MainWindow", "ok"}) && d.mode.get() == 0 && d.doc.selection.get() &&
            d.doc.selection.get()->id() == "ok",
        "going to a designed view selects it in Design mode");
  d.mode = 1;
  d.project.active_file = "mainwindow.cpp";
  layout_now(win);

  // find / replace bar
  click(win, center(d.editor));
  key(win, Key::F, Mod::Primary);
  check(d.find_open.get() && win.focus_view() == d.find_field, "Ctrl+F opens the find bar and focuses it");
  d.find_query = "window_";
  check(d.editor->search_count() >= 2 && !d.find_status.get().empty() && d.editor->selected_text() == "window_",
        "typing in the find bar highlights and selects matches");
  const std::string first = d.find_status.get();
  key(win, Key::F3);
  check(d.find_status.get() != first, "F3 moves to the next match");
  layout_now(win);
  if (const char* shots = std::getenv("PULSELAYER_SHOTS")) win.screenshot(std::string(shots) + "/find.png");
  key(win, Key::Escape);
  check(!d.find_open.get() && d.editor->search_count() == 0, "Escape closes the find bar");

  // rename across the project (code and form bindings), then back
  {
    check(d.rename_symbol("greet", "hello", true), "rename runs");
    d.sync_form();
    const std::string cpp = d.project.find_file("mainwindow.cpp")->text, hpp = d.project.find_file("mainwindow.hpp")->text;
    const std::string form = d.project.forms[0].json;
    check(cpp.find("MainWindow::hello()") != std::string::npos && hpp.find("void hello();") != std::string::npos &&
              form.find("\"hello\"") != std::string::npos && form.find("\"greet\"") == std::string::npos,
          "rename updates the header, the source and the form binding");
    d.rename_symbol("hello", "greet", true);
    check(d.project.find_file("mainwindow.hpp")->text.find("void greet();") != std::string::npos, "rename back");
    const auto& lines = d.editor->lines();
    const int l = line_of(lines, "MainWindow::greet");
    d.editor->go_to(l, static_cast<int>(lines[static_cast<std::size_t>(l - 1)].find("greet")) + 2);
    key(win, Key::F2);
    auto* field = d.dialog_window ? d.dialog_window->find<TextField>("dialog-name") : nullptr;
    check(field && field->text() == "greet", "F2 asks for a new name");
    if (d.dialog_window) d.dialog_window->close();
  }

  // Alt+Enter on a declaration creates the definition in the .cpp
  {
    const std::string orig_h = d.project.find_file("mainwindow.hpp")->text, orig_c = d.project.find_file("mainwindow.cpp")->text;
    d.project.active_file = "mainwindow.hpp";
    const int l = line_of(d.editor->lines(), "void greet();");
    const std::size_t end = d.editor->lines()[static_cast<std::size_t>(l - 1)].size();
    d.editor->replace_range(static_cast<std::size_t>(l - 1), end, static_cast<std::size_t>(l - 1), end,
                            "\n    void extra(int a = 1) const;");
    d.editor->go_to(l + 1, 10);
    key(win, Key::Enter, Mod::Alt);
    check(d.project.active_file.get() == "mainwindow.cpp" &&
              d.editor->text().find("void MainWindow::extra(int a) const {\n    \n}") != std::string::npos &&
              d.editor->caret_line() == line_of(d.editor->lines(), "MainWindow::extra") + 1,
          "Alt+Enter adds the definition and puts the caret in its body");
    d.project.find_file("mainwindow.hpp")->text = orig_h;
    d.project.find_file("mainwindow.cpp")->text = orig_c;
    d.open_file("mainwindow.cpp");
  }

  // Ctrl+/ comments the caret line
  d.editor->go_to(1, 1);
  key(win, Key::Slash, Mod::Primary);
  check(d.editor->lines()[0].rfind("// ", 0) == 0, "Ctrl+/ comments the line");
  key(win, Key::Slash, Mod::Primary);
  check(d.editor->lines()[0].rfind("//", 0) != 0, "Ctrl+/ again uncomments it");

  // right click in the editor: the caret goes there and the code menu opens
  {
    layout_now(win);
    d.editor->go_to(1, 1);
    const float lh = d.editor->bounds().h / 40;
    const Point at = d.editor->to_window({d.editor->bounds().w / 2, 8 + lh * 2.5f});
    InputEvent down = mouse(InputEvent::MouseDown, at), up = mouse(InputEvent::MouseUp, at);
    down.button = up.button = MouseButton::Right;
    win.send(mouse(InputEvent::MouseMove, at));
    win.send(down);
    win.send(up);
    check(!win.popups().empty(), "a right click in the code opens the context menu");
    check(d.editor->caret_line() > 1, "the right click moves the caret to the clicked line");
    if (const char* shots = std::getenv("PULSELAYER_SHOTS")) win.screenshot(std::string(shots) + "/editor_menu.png");
    key(win, Key::Escape);
    std::vector<std::string> labels;
    for (auto& e : d.editor_menu()) labels.push_back(e.display_label());
    auto has = [&](const char* k) { return std::find(labels.begin(), labels.end(), i18n::text(k)) != labels.end(); };
    check(has("menu.go_to_definition") && has("menu.rename") && has("menu.switch_header") && has("menu.toggle_breakpoint") &&
              has("menu.copy") && has("menu.paste"),
          "the menu has Go to Definition, Rename, Switch Header, Breakpoint and the clipboard");
    check(win.popups().empty(), "Escape closes the menu");
  }

  // the toolbar: one configuration selector instead of separate Debug / Release buttons
  layout_now(win);
  check(win.find<Select>("config") && win.find<Select>("config")->options().size() == 2 && !win.find("debug") && !win.find("release"),
        "Code mode has a Debug / Release selector");
  auto shown = [](View* v) {
    for (View* x = v; x; x = x->parent())
      if (!x->is_visible()) return false;
    return v != nullptr;
  };
  check(win.find("debug-over") && !shown(win.find("debug-over")) && win.find("stop") && !win.find("stop")->is_enabled(),
        "step buttons appear only while debugging");

  // a click on a line number toggles a breakpoint (the real mouse path)
  {
    layout_now(win);
    const Point p = d.editor->to_window({12, 6 + 18.f * 2 + 5});
    click(win, p);
    layout_now(win);
    const auto& all = d.breakpoints.get();
    const auto it = all.find(d.project.active_file.get());
    std::printf("[INFO] breakpoints in %s: %zu\n", d.project.active_file.get().c_str(), it == all.end() ? 0 : it->second.size());
    check(it != all.end() && it->second.size() == 1 && d.editor->breakpoints_value().size() == 1,
          "a click on a line number sets a breakpoint");
    if (const char* shots = std::getenv("PULSELAYER_SHOTS")) win.screenshot(std::string(shots) + "/breakpoint.png");
    click(win, p);
    const auto after = d.breakpoints.get();
    check(after.find(d.project.active_file.get()) == after.end() && d.editor->breakpoints_value().empty(), "a second click removes it");
  }
  d.open_file("main.cpp");
}

// clang-format (when installed), then `next`
void format_test(Designer& d, std::function<void()> next) {
  const std::string exe = find_clang_format(d.toolchains.get());
  std::printf("[INFO] clang-format: %s\n", exe.empty() ? "not found" : exe.c_str());
  if (exe.empty()) return next();
  d.mode = 1;
  d.project.active_file = "main.cpp";
  const std::string orig = d.editor_text.get();
  const std::string messy = "int   main( ) {return 0;}\n";
  d.editor_text = messy;
  d.cmd_format();
  wait_until([&d, messy] { return d.editor->text() + "\n" != messy && d.editor->text() != messy; }, 20000,
             [&d, next, orig](bool ok) {
               check(ok && d.editor->text().find("int main() {\n    return 0;\n}") != std::string::npos,
                     "Ctrl+Shift+F formats the file with clang-format");
               d.editor_text = orig;
               next();
             });
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
  type_text(win, "\nvalue = std::cl");
  check(d.editor->completion_open(), "suggestions after std::");
  key(win, Key::Enter);
  check(d.editor->text().find("value = std::clamp()") != std::string::npos && d.editor->signature_hint().find("clamp(") == 0,
        "accepting a function inserts () and shows its parameters");
  if (const char* shots = std::getenv("PULSELAYER_SHOTS")) win.screenshot(std::string(shots) + "/signature.png");
  key(win, Key::Escape);
  check(d.editor->signature_hint().empty(), "Escape closes the parameter hint");
  d.open_file("main.cpp");

  editor_tools_tests(d, win);

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
    format_test(d, [&d, &win] {
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

// Release: an optimized build packaged into a folder with everything it needs.
void release_tests(Designer& d) {
  const auto t0 = std::chrono::steady_clock::now();
  d.build_config = 1;
  d.cmd_build_selected(); // Build in the Release configuration
  check(d.builder.state.get() != Builder::Idle, "Build in Release starts an optimized build");
  wait_until([&d] { return d.builder.state.get() == Builder::Idle; }, 15 * 60 * 1000, [&d, t0](bool) {
    std::printf("[INFO] release build took %.0f s\n", std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
    const std::string dir = d.builder.release_result();
    check(!dir.empty(), "the release is packaged");
#ifdef _WIN32
    std::string exe;
    std::error_code ec;
    if (!dir.empty())
      for (auto& e : std::filesystem::directory_iterator(dir, ec))
        if (e.path().extension() == ".exe") exe = e.path().string();
    check(!exe.empty() && std::filesystem::path(exe).stem() == "Demo", "the release has <project>.exe");
    // everything it imports is either Windows' own or shipped next to it
    bool complete = !exe.empty();
    for (auto& dll : pe_imports(exe)) {
      const char* root = std::getenv("SystemRoot");
      const bool system = root && std::filesystem::exists(std::filesystem::path(root) / "System32" / dll, ec);
      const bool shipped = std::filesystem::exists(std::filesystem::path(dir) / dll, ec);
      std::printf("[INFO]   imports %s%s\n", dll.c_str(), shipped ? " (shipped)" : system ? "" : " (MISSING)");
      if (!system && !shipped && dll.rfind("api-ms-", 0) != 0) complete = false;
    }
    check(complete, "the release has every DLL it needs");
#endif
    finish();
  });
}

// Debugging the built program: a breakpoint in MainWindow's constructor, the stop,
// the call stack and locals, one step, then stopping the session.
void debug_tests(Designer& d) {
  const DebuggerTool tool = find_debugger(d.toolchains.get().kits[static_cast<std::size_t>(d.kit_index.get())]);
  std::printf("[INFO] debugger: %s %s\n", tool.name().c_str(), tool.path.c_str());
  if (!tool.found()) {
    check(true, "no debugger for this kit (skipped)");
    return finish();
  }
  d.project.active_file = "mainwindow.cpp";
  int line = 0;
  if (const SourceFile* f = d.project.find_file("mainwindow.cpp")) {
    const auto at = f->text.find("ui_.setup(");
    if (at != std::string::npos) line = 1 + static_cast<int>(std::count(f->text.begin(), f->text.begin() + static_cast<std::ptrdiff_t>(at), '\n'));
  }
  d.cmd_toggle_breakpoint(line);
  check(line > 0 && d.breakpoints.get().count("mainwindow.cpp") && d.breakpoints.get().at("mainwindow.cpp") == std::vector<int>{line},
        "F9 sets a breakpoint on the caret line");
  check(d.editor && d.editor->breakpoints_value() == std::vector<int>{line}, "the editor shows the breakpoint");
  const auto t0 = std::chrono::steady_clock::now();
  d.build_config = 0;
  d.cmd_run(); // Run in the Debug configuration = under the debugger
  wait_until([&d] { return d.debug_state.get() == Debugger::Paused || d.debug_state.get() == Debugger::Exited ||
                           (!d.debug_after_build && !d.debugger && d.builder.state.get() == Builder::Idle && !d.builder.last_build_ok()); },
             240000, [&d, line, t0](bool) {
               std::printf("[INFO] debugger stopped after %.1f s\n", std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
               check(d.debug_state.get() == Debugger::Paused, "the debugger stops at the breakpoint");
               wait_until([&d] { return d.exec_line.get() > 0 && !d.debug_locals.get().empty(); }, 20000, [&d, line](bool) {
                 check(d.exec_file.get() == "mainwindow.cpp" && d.exec_line.get() == line, "the editor marks the line where it stopped");
                 bool ctor = false;
                 for (auto& f : d.debug_stack.get()) ctor = ctor || f.function.find("MainWindow") != std::string::npos;
                 check(ctor, "the call stack shows MainWindow's constructor");
                 check(!d.debug_locals.get().empty(), "locals are listed");
                 check(app_output_has(d, "hello from the app"), "under the debugger std::cout also reaches Application output");
                 if (d.app_output)
                   for (auto& l : d.app_output->lines()) std::printf("[INFO]   app output: %s\n", l.c_str());
                 for (auto& f : d.debug_stack.get()) std::printf("[INFO]   frame %s %s:%d\n", f.function.c_str(), f.file.c_str(), f.line);
                 if (const char* shots = std::getenv("PULSELAYER_SHOTS"); shots && d.window) d.window->screenshot(std::string(shots) + "/debugger.png");
                 d.cmd_step_over();
                 wait_until([&d, line] { return d.debug_state.get() == Debugger::Paused && d.exec_line.get() > 0 && d.exec_line.get() != line; }, 30000,
                            [&d](bool stepped) {
                              check(stepped, "step over moves to the next line");
                              d.cmd_debug_stop();
                              wait_until([&d] { return d.debug_state.get() == Debugger::Exited || d.debug_state.get() == Debugger::Idle; }, 20000,
                                         [&d](bool stopped) {
                                           check(stopped && d.exec_line.get() == 0, "Stop ends the debugging session");
                                           release_tests(d);
                                         });
                            });
               });
             });
}

// main.cpp that prints a line without flushing: Application Output must still
// show it at once (the generated pulselayer_console.cpp turns buffering off)
std::string printing_main() {
  std::string m = Project::main_template("MainWindow");
  m = "#include <iostream>\n" + m;
  const std::string at = "int main() {\n";
  m.insert(m.find(at) + at.size(), "    std::cout << \"hello from the app\\n\";\n");
  return m;
}

bool app_output_has(Designer& d, const std::string& s) {
  if (!d.app_output) return false;
  for (auto& l : d.app_output->lines())
    if (l.find(s) != std::string::npos) return true;
  return false;
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

    // a list in the form (row template bound to a std::vector, an event taking the item):
    // the generated ForEach code must compile with the rest
    if (SourceFile* h = d.project.find_file("mainwindow.hpp")) {
      const std::string anchor = "    pulse::property<std::string> name;\n";
      const auto at = h->text.find(anchor);
      if (at != std::string::npos)
        h->text.insert(at + anchor.size(), "    struct Todo { std::string title; bool done = false; };\n"
                                           "    pulse::property<std::vector<Todo>> todos{std::vector<Todo>{{\"Write tests\", true}}};\n"
                                           "    void remove(const Todo& t);\n");
    }
    if (SourceFile* c = d.project.find_file("mainwindow.cpp")) c->text += "\nvoid MainWindow::remove(const Todo& t) { (void)t; }\n";
    d.load_form(0);
    if (View* list = d.doc.insert("ForEach", d.doc.root(), -1)) {
      d.doc.set_binding(list, "bind.items", "todos");
      View* row = list->children().front().get();
      for (auto& v : row->children()) {
        if (std::string(v->type_name()) == "Text") d.doc.set_binding(v.get(), "bind.text", "item.title");
        if (std::string(v->type_name()) == "Checkbox") d.doc.set_binding(v.get(), "bind.checked", "item.done");
      }
      View* btn = d.doc.insert("Button", row, -1);
      if (btn) d.doc.set_binding(btn, "on.click", "remove");
    }
    // components: the LoginDialog form inside MainWindow (its bindings name/greet exist
    // in MainWindow too), and a view class of the project from rating.hpp
    d.project.add_file("rating.hpp");
    if (SourceFile* r = d.project.find_file("rating.hpp"))
      r->text = "#pragma once\n#include <pulseui/pulseui.hpp>\n\n"
                "class Rating : public pulseui::ViewT<Rating> {\npublic:\n  Rating() { height(24); }\n"
                "  const char* type_name() const override { return \"Rating\"; }\n};\n";
    d.mode = 0;
    layout_now(win);
    check(win.find("palette-Component:LoginDialog") && win.find("palette-Custom:Rating|rating.hpp"),
          "the palette lists the other forms and the project's view classes");
    d.doc.insert("Component:LoginDialog", d.doc.root(), -1);
    if (View* custom = d.doc.insert("Custom:Rating|rating.hpp", d.doc.root(), -1))
      if (const meta::Property* p = meta::property(*custom, "modifiers")) d.doc.set_prop(custom, p, std::string(".tooltip(\"stars\")"));
    d.sync_form();
    d.mode = 1;
    check(d.project.forms[0].json.find("\"ForEach\"") != std::string::npos &&
              d.project.forms[0].json.find("\"Component\"") != std::string::npos,
          "the form has a list and components for the build");
    {
      const std::string h = Project::ui_header(d.project.forms[0]);
      check(h.find("#include \"ui_logindialog.hpp\"") != std::string::npos && h.find("#include \"rating.hpp\"") != std::string::npos &&
                h.find("VStack{Ui::LoginDialog::part(m)}") != std::string::npos && h.find("Rating()") != std::string::npos && h.find(".tooltip(\"stars\")") != std::string::npos,
            "the header includes and builds the components");
    }

    // fix it and build + run: two forms, three .cpp files
    d.editor_text = printing_main();
    const auto t1 = std::chrono::steady_clock::now();
    d.cmd_run_plain();
    wait_until([&d] { return d.builder.state.get() == Builder::Running || (d.builder.state.get() == Builder::Idle && !d.builder.last_build_ok()); },
               10 * 60 * 1000, [&d, &win, t1](bool) {
                 std::printf("[INFO] incremental build took %.1f s\n", std::chrono::duration<double>(std::chrono::steady_clock::now() - t1).count());
                 check(d.builder.last_build_ok(), "fixed multi-form program builds");
                 if (!d.builder.last_build_ok())
                   for (auto& i : d.builder.issues.get())
                     std::printf("[INFO]   %s:%d: %s\n", i.file.c_str(), i.line, i.message.c_str());
                 check(d.editor->diagnostics_value().empty(), "errors are cleared");
                 check(std::filesystem::exists(d.builder.executable()), "executable exists");
                 check(d.builder.state.get() == Builder::Running, "Run starts the program");
                 if (const char* shots = std::getenv("PULSELAYER_SHOTS")) win.screenshot(std::string(shots) + "/code_ok.png");
                 check(d.bottom_tab.get() == Designer::kTabAppOutput, "Run opens Application output");
                 App::after(std::chrono::milliseconds(1500), [&d] {
                   check(app_output_has(d, "hello from the app"), "std::cout of the running program appears in Application output");
                   check(d.output && std::none_of(d.output->lines().begin(), d.output->lines().end(),
                                                  [](const std::string& l) { return l.find("hello from the app") != std::string::npos; }),
                         "the program's output is not mixed into Build output");
                   d.cmd_stop();
                   wait_until([&d] { return d.builder.state.get() == Builder::Idle; }, 10000, [&d](bool stopped) {
                     check(stopped, "Stop terminates the program");
                     // the error check (saving in Code mode): no build, same errors
                     d.project.active_file = "main.cpp";
                     d.editor_text = kBrokenCode;
                     check(d.check_syntax(), "the error check starts");
                     wait_until([&d] { return !d.checking; }, 120000, [&d](bool done) {
                       bool found = false;
                       for (auto& i : d.builder.issues.get())
                         if (i.user_code && i.file == "main.cpp" && i.line == 8 && i.severity == Diagnostic::Error) found = true;
                       check(done && found, "the error check finds the error without building");
                       d.editor_text = printing_main();
                       debug_tests(d);
                     });
                   });
                 }).release();
               });
  });
}

} // namespace

} // namespace layer
