#include "mainwindow.hpp"

#include <algorithm>
#include <cctype>

using namespace pulseui;

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trimmed(const std::string& s) {
    const auto b = s.find_first_not_of(" \t");
    const auto e = s.find_last_not_of(" \t");
    return b == std::string::npos ? std::string() : s.substr(b, e - b + 1);
}

std::size_t count_done(const std::vector<Task>& all) {
    return static_cast<std::size_t>(std::count_if(all.begin(), all.end(), [](const Task& t) { return t.done; }));
}

} // namespace

MainWindow::MainWindow()
    : visible(
          [this] {
              const std::string q = lower(query.get());
              std::vector<Task> out;
              for (const Task& t : tasks.get()) {
                  if (filter.get() == 1 && t.done) continue;
                  if (filter.get() == 2 && !t.done) continue;
                  if (!q.empty() && lower(t.title).find(q) == std::string::npos) continue;
                  out.push_back(t);
              }
              // open tasks first, the most important on top
              std::stable_sort(out.begin(), out.end(), [](const Task& a, const Task& b) {
                  if (a.done != b.done) return !a.done;
                  return a.priority > b.priority;
              });
              return out;
          },
          tasks, query, filter),
      summary(
          [this] {
              const std::vector<Task> all = tasks.get();
              const std::size_t done = count_done(all);
              return std::to_string(all.size() - done) + " open  ·  " + std::to_string(done) + " done";
          },
          tasks),
      progress(
          [this] {
              const std::vector<Task> all = tasks.get();
              return all.empty() ? 0.0 : static_cast<double>(count_done(all)) / static_cast<double>(all.size());
          },
          tasks),
      has_completed([this] { return count_done(tasks.get()) > 0; }, tasks),
      can_add([this] { return !trimmed(draft.get()).empty(); }, draft) {
    tasks = std::vector<Task>{
        {next_id_++, "Sketch the main window in Design mode", Priority::High, true},
        {next_id_++, "Bind the views to members of the class", Priority::High, false},
        {next_id_++, "Press Run and enjoy", Priority::Normal, false},
        {next_id_++, "Add a second form", Priority::Low, false},
    };

    ui_.setup(window_, *this); // builds the form; its bindings connect to the members above
    window_.set_min_size(480, 420);

    // What a form cannot express yet is added in code, into placeholder containers:
    // a segmented filter and the list itself (one row per visible task).
    ui_.filters->add(Segmented({"All", "Active", "Done"}, filter));
    ui_.list->add(ForEach(visible, [this](const Task& t) { return row(t); }));
    ui_.list->add(Text("Nothing to show").dim().text_align(Align::Center).padding(24)
                      .visible(visible.map([](const std::vector<Task>& v) { return v.empty(); })));
    ui_.summary->dim();

    window_.shortcut("Ctrl+F", [this] { ui_.search->focus(); });
    window_.shortcut("Ctrl+N", [this] { ui_.draft->focus(); });
}

Node MainWindow::row(const Task& task) {
    static const Color marks[] = {Color::hex(0x8A94A6), Color::hex(0x3D7EFF), Color::hex(0xF05A3C)};
    const int id = task.id;
    return HStack{
        Spacer().flex(0).size(4, 22).radius(2).background(marks[static_cast<int>(task.priority)]),
        Checkbox(task.title, task.done).on_change([this, id](bool) { toggle(id); }).flex(),
        Text(priority_name(task.priority)).dim(),
        Button("✕", [this, id] { remove(id); }).flat().tooltip("Delete"),
    }
        .spacing(10)
        .align_items(Alignment::Center)
        .padding(Insets(8, 4));
}

void MainWindow::add_task() {
    const std::string title = trimmed(draft.get());
    if (title.empty()) return;
    const auto p = static_cast<Priority>(priority.get());
    tasks.update([&](std::vector<Task>& v) { v.push_back({next_id_++, title, p, false}); });
    draft = "";
}

void MainWindow::toggle(int id) {
    tasks.update([id](std::vector<Task>& v) {
        for (Task& t : v)
            if (t.id == id) t.done = !t.done;
    });
}

void MainWindow::remove(int id) {
    tasks.update([id](std::vector<Task>& v) { std::erase_if(v, [id](const Task& t) { return t.id == id; }); });
}

void MainWindow::clear_completed() {
    tasks.update([](std::vector<Task>& v) { std::erase_if(v, [](const Task& t) { return t.done; }); });
}
