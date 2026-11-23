#include "mainwindow.hpp"

#include <algorithm>
#include <cctype>
#include <set>

using namespace pulseui;
using namespace std::chrono_literals;

namespace {

const char* const kWeekdays[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
const char* const kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

std::string pretty(const Date& d) {
    const Date today = Date::today();
    if (d == today) return "Today";
    if (d == today.add_days(1)) return "Tomorrow";
    if (d == today.add_days(-1)) return "Yesterday";
    return std::string(kWeekdays[d.weekday()]) + " " + std::to_string(d.day) + " " + kMonths[d.month - 1];
}

std::string trimmed(const std::string& s) {
    const auto b = s.find_first_not_of(" \t");
    const auto e = s.find_last_not_of(" \t");
    return b == std::string::npos ? std::string() : s.substr(b, e - b + 1);
}

// "Buy milk #home" -> "#home"
std::string tag_of(const std::string& title) {
    const auto h = title.find('#');
    if (h == std::string::npos) return {};
    auto e = title.find(' ', h);
    return title.substr(h, e == std::string::npos ? std::string::npos : e - h);
}

Color priority_color(int p) {
    const Theme& t = th();
    return p == 2 ? t.danger : p == 1 ? t.accent : t.text_dim;
}

} // namespace

Task MainWindow::make(std::string title, Date d, int prio, bool done) {
    Task t;
    t.id = next_id_++;
    t.title = std::move(title);
    t.tag = tag_of(t.title);
    t.due = d;
    t.priority = prio;
    t.done = done;
    const bool overdue = !done && d < Date::today();
    t.subtitle = (overdue ? "Overdue \xC2\xB7 " : "") + pretty(d) + (t.tag.empty() ? "" : "  \xC2\xB7  " + t.tag);
    t.dot = priority_color(prio);
    return t;
}

MainWindow::MainWindow()
    : visible(
          [this] {
              std::vector<Task> out;
              for (const Task& t : tasks.get()) {
                  if (filter.get() == 1 && t.done) continue;
                  if (filter.get() == 2 && !t.done) continue;
                  if (!tag_filter.get().empty() && t.tag != tag_filter.get()) continue;
                  out.push_back(t);
              }
              // open first, then by date, the important ones on top
              std::stable_sort(out.begin(), out.end(), [](const Task& a, const Task& b) {
                  if (a.done != b.done) return !a.done;
                  if (a.due != b.due) return a.due < b.due;
                  return a.priority > b.priority;
              });
              return out;
          },
          tasks, filter, tag_filter),
      summary(
          [this] {
              int open = 0;
              for (auto& t : tasks.get()) open += !t.done;
              return std::to_string(open) + (open == 1 ? " task left" : " tasks left");
          },
          tasks),
      list_title(
          [this] {
              static const char* names[] = {"All tasks", "Open tasks", "Finished"};
              const std::string tag = tag_filter.get();
              return std::string(names[std::clamp(filter.get(), 0, 2)]) + (tag.empty() ? "" : "  \xC2\xB7  " + tag);
          },
          filter, tag_filter),
      today_text(
          [] {
              const Date d = Date::today();
              return std::string(kWeekdays[d.weekday()]) + ", " + std::to_string(d.day) + " " + kMonths[d.month - 1] + " " +
                     std::to_string(d.year);
          },
          dark),
      can_add([this] { return !trimmed(draft.get()).empty(); }, draft),
      ratio(
          [this] {
              const auto all = tasks.get();
              int done = 0;
              for (auto& t : all) done += t.done;
              return all.empty() ? 0.0 : static_cast<double>(done) / static_cast<double>(all.size());
          },
          tasks),
      progress(tween(ratio, Transition(600ms, Easing::EaseInOut))) {
    const Date today = Date::today();
    tasks = std::vector<Task>{
        make("Sketch the new onboarding #design", today, 2),
        make("Review pull requests #work", today.add_days(-1), 1),
        make("Book flights for the conference #travel", today.add_days(3), 2),
        make("Water the plants #home", today.add_days(1), 0),
        make("Write release notes #work", today.add_days(2), 1),
        make("Call the bank #home", today.add_days(-2), 1, true),
        make("Pick a color palette #design", today.add_days(5), 0, true),
    };

    ui_.setup(window_, *this);
    window_.set_min_size(640, 480);

    // ── animations: the list card follows its rows smoothly, the side column slides ──
    ui_.list_card->animate(260ms, Easing::EaseOut);
    ui_.side->animate(320ms, Easing::EaseInOut);
    ui_.side->style().clip = true;
    ui_.composer->animate(250ms);
    ui_.heading->color(theme().map([](const Theme& t) { return t.accent; }));
    ui_.today->color(theme().map([](const Theme& t) { return t.text_dim; }));
    ui_.summary->color(theme().map([](const Theme& t) { return t.text_dim; }));

    // ── suggestions while typing a #tag ──
    ui_.draft->suggestions([this](const std::string& text) {
        std::vector<Suggestion> out;
        const auto h = text.rfind('#');
        if (h == std::string::npos || text.find(' ', h) != std::string::npos) return out;
        const std::string typed = text.substr(h);
        std::set<std::string> tags{"#work", "#home", "#design", "#travel", "#health"};
        for (auto& t : tasks.get())
            if (!t.tag.empty()) tags.insert(t.tag);
        for (auto& tag : tags)
            if (tag.rfind(typed, 0) == 0 && tag != typed) out.push_back({tag, "tag", text.substr(0, h) + tag + " "});
        return out;
    });

    // ── the menu bar (native on macOS, drawn in the theme on Windows) ──
    window_.menu({
        Menu("&File", {
            Action("&New task", [this] { ui_.draft->focus(); }).shortcut("Ctrl+N").icon("+"),
            Separator(),
            Action("&Clear finished...", [this] { clear_done(); }).shortcut("Ctrl+Shift+Delete"),
            Separator(),
            Action("&Quit", [] { App::quit(); }).shortcut("Ctrl+Q"),
        }),
        Menu("&Edit", {
            Action("&Undo delete", [this] { undo_remove(); }).shortcut("Ctrl+Z"),
            Action("Mark all as &done", [this] {
                auto all = tasks.get();
                for (auto& t : all) {
                    const int id = t.id;
                    t = make(t.title, t.due, t.priority, true);
                    t.id = id;
                }
                tasks = all;
                toast(window_, "Everything is done. Nice!", ToastKind::Success);
            }),
        }),
        Menu("&View", {
            Action("&All tasks", [this] { filter = 0; }).checked(filter.map([](int f) { return f == 0; })).radio().shortcut("Ctrl+1"),
            Action("&Open", [this] { filter = 1; }).checked(filter.map([](int f) { return f == 1; })).radio().shortcut("Ctrl+2"),
            Action("&Finished", [this] { filter = 2; }).checked(filter.map([](int f) { return f == 2; })).radio().shortcut("Ctrl+3"),
            Separator(),
            Action("&Focus mode", [this] { set_focus_mode(focus_mode.get()); }).checked(focus_mode).shortcut("F11"),
            Action("&Dark theme", [this] { theme() = dark.get() ? Theme::dark() : Theme::light(); }).checked(dark).shortcut("Ctrl+T"),
        }),
        Menu("&Help", {
            Action("&About Planner", [this] {
                dialogs::alert(&window_, "Planner",
                               "A Pulse Layer sample: a ForEach row template, a component form, menus, dialogs, "
                               "toasts and animations - all reactive.");
            }),
        }),
    });

    // ── a context menu on the list ──
    ui_.list_card->context_menu([this] {
        return MenuItems{
            Action("New task", [this] { ui_.draft->focus(); }),
            Separator(),
            Action("Show all", [this] { filter = 0; tag_filter = std::string(); }),
            Action("Clear finished...", [this] { clear_done(); }).enabled(stats_ratio_nonzero()),
        };
    });

    // ── tag chips: rebuilt when the tags change ──
    subs_.push_back(tasks.as_observable().subscribe([this](const std::vector<Task>&) { rebuild_tags(); }));
    subs_.push_back(tag_filter.as_observable().subscribe([this](const std::string&) { rebuild_tags(); }));

    window_.shortcut("Ctrl+F", [this] { ui_.draft->focus(); });
}

bool MainWindow::stats_ratio_nonzero() {
    for (auto& t : tasks.get())
        if (t.done) return true;
    return false;
}

void MainWindow::rebuild_tags() {
    std::set<std::string> tags;
    for (auto& t : tasks.get())
        if (!t.tag.empty()) tags.insert(t.tag);
    if (tags == shown_tags_ && tag_filter.get() == shown_tag_filter_) return;
    shown_tags_ = tags;
    shown_tag_filter_ = tag_filter.get();
    ui_.tags->clear_children();
    auto chip = [this](const std::string& label, const std::string& value) {
        Button b(label, [this, value] { tag_filter = value; });
        b.flat();
        if (tag_filter.get() == value) b.primary();
        b.padding(Insets(10, 0)).height(26).animate();
        return b;
    };
    ui_.tags->add(chip("everything", ""));
    for (auto& t : tags) ui_.tags->add(chip(t, t));
}

void MainWindow::add_task() {
    const std::string title = trimmed(draft.get());
    if (title.empty()) return;
    auto all = tasks.get();
    all.push_back(make(title, due.get(), priority.get()));
    tasks = all;
    draft = std::string();
    ui_.draft->focus();
}

void MainWindow::toggle(const Task& t, bool done) {
    auto all = tasks.get();
    for (auto& x : all)
        if (x.id == t.id) {
            const int id = x.id;
            x = make(x.title, x.due, x.priority, done);
            x.id = id;
        }
    tasks = all;
    if (done && ratio.get() >= 1.0) toast(window_, "All tasks done!", ToastKind::Success);
}

void MainWindow::remove(const Task& t) {
    auto all = tasks.get();
    all.erase(std::remove_if(all.begin(), all.end(), [&](const Task& x) { return x.id == t.id; }), all.end());
    tasks = all;
    last_removed_ = t;
    ToastOptions o;
    o.action_label = "Undo";
    o.action = [this] { undo_remove(); };
    toast(window_, "Deleted \xE2\x80\x9C" + t.title + "\xE2\x80\x9D", o);
}

void MainWindow::undo_remove() {
    if (!last_removed_) return;
    auto all = tasks.get();
    all.push_back(*last_removed_);
    tasks = all;
    last_removed_.reset();
}

void MainWindow::clear_done() {
    int n = 0;
    for (auto& t : tasks.get()) n += t.done;
    if (!n) {
        toast(window_, "Nothing finished yet", ToastKind::Info);
        return;
    }
    dialogs::confirm_async(&window_, "Clear finished tasks?",
                           std::to_string(n) + (n == 1 ? " task" : " tasks") + " will be removed.",
                           [this, n](bool yes) {
                               if (!yes) return;
                               auto all = tasks.get();
                               all.erase(std::remove_if(all.begin(), all.end(), [](const Task& t) { return t.done; }), all.end());
                               tasks = all;
                               toast(window_, "Removed " + std::to_string(n) + " finished", ToastKind::Success);
                           },
                           "Clear", "Keep");
}

void MainWindow::set_focus_mode(bool on) {
    // the side column glides closed (its frame is animated), the composer fades
    ui_.side->style().width = on ? 0.f : 240.f;
    ui_.side->invalidate_layout();
    ui_.composer->set_opacity(on ? 0.55f : 1.f);
}
