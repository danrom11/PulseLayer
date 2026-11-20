#include "mainwindow.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>

using namespace pulseui;
using namespace std::chrono_literals;

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string first_line(const std::string& s) {
    const auto start = s.find_first_not_of(" \n");
    if (start == std::string::npos) return "empty";
    return s.substr(start, s.find('\n', start) - start);
}

} // namespace

MainWindow::MainWindow(Store& store)
    : store_(store),
      rows(
          [this] {
              const std::string q = lower(query.get());
              std::vector<Row> out;
              for (const Snippet& s : store_.snippets.get())
                  if (q.empty() || lower(s.title).find(q) != std::string::npos || lower(s.code).find(q) != std::string::npos)
                      out.push_back({s, s.id == store_.selected.get()});
              return out;
          },
          store_.snippets, store_.selected, query),
      stats(
          [this] {
              const std::string text = code.get(); // get() returns a copy: take it once
              const auto lines = std::count(text.begin(), text.end(), '\n') + (text.empty() ? 0 : 1);
              return std::to_string(lines) + " lines  ·  " + std::to_string(text.size()) + " characters";
          },
          code),
      count_text([this] { return std::to_string(store_.snippets.get().size()) + " snippets"; }, store_.snippets),
      has_selection([this] { return store_.selected.get() != 0; }, store_.selected) {
    // The editor fields edit the selected snippet in the store.
    title.changed.connect([this](const std::string& v) {
        if (!loading_) store_.edit(store_.selected.get(), [&](Snippet& s) { s.title = v; });
    });
    language.changed.connect([this](int v) {
        if (!loading_) store_.edit(store_.selected.get(), [&](Snippet& s) { s.language = v; });
    });
    code.changed.connect([this](const std::string& v) {
        if (!loading_) store_.edit(store_.selected.get(), [&](Snippet& s) { s.code = v; });
    });
    store_.selected.changed.connect([this](int id) { open(id); });
    open(store_.selected.get());

    ui_.setup(window_, *this); // builds the form and connects its bindings
    window_.set_min_size(640, 400);
    window_.closed.connect([] { App::quit(); }); // the dialogs may still exist (hidden)

    // Added in code: the list, the code editor and a debounced search.
    ui_.list->add(ForEach(rows, [this](const Row& r) { return row(r); }).spacing(2));
    ui_.count->dim();
    ui_.stats->dim();
    CodeEditor* editor = nullptr;
    ui_.code_host->add(CodeEditor(code).ref(editor).flex());
    subs_.push_back(store_.line_numbers.as_observable().subscribe([editor](bool on) {
        editor->line_numbers(on);
        editor->invalidate_layout();
    }));
    // Search filters as you type, but only after a short pause (a Pulse operator on the text stream).
    subs_.push_back((ui_.search->text_changes() | pulse::debounce(200ms, App::ui()))
                        .subscribe([this](const std::string& q) { query = q; }));

    window_.shortcut("Ctrl+N", [this] { add_snippet(); });
    window_.shortcut("Ctrl+F", [this] { ui_.search->focus(); });
    window_.shortcut("Ctrl+P", [this] { open_preferences(); });
}

void MainWindow::open_preferences() {
    if (!preferences_) preferences_ = std::make_unique<Preferences>(store_);
    preferences_->show();
}

void MainWindow::open_about() {
    if (!about_) about_ = std::make_unique<About>(store_);
    about_->show();
}

void MainWindow::open(int id) {
    loading_ = true;
    const std::optional<Snippet> s = store_.find(id);
    title = s ? s->title : std::string();
    language = s ? s->language : 0;
    code = s ? s->code : std::string();
    loading_ = false;
}

Node MainWindow::row(const Row& r) {
    const int id = r.snippet.id;
    const auto& names = Store::languages();
    const std::size_t lang = std::min(static_cast<std::size_t>(std::max(0, r.snippet.language)), names.size() - 1);
    const std::string subtitle = names[lang] + "  ·  " + first_line(r.snippet.code);
    return VStack{
        Text(r.snippet.title.empty() ? std::string("Untitled") : r.snippet.title).weight(FontWeight::Medium),
        Text(subtitle).dim(),
    }
        .spacing(2)
        .padding(Insets(10, 7))
        .radius(6)
        .background(r.selected ? th().selection : Color::transparent())
        .on_click([this, id] { store_.selected = id; });
}
