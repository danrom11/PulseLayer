#include "mainwindow.hpp"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>

using namespace pulseui;
using namespace std::chrono_literals;

namespace {

std::string percent(double v) { return std::to_string(static_cast<int>(std::lround(v * 100))) + " %"; }

std::string fixed(double v, int decimals = 1) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.*f", decimals, v);
    return buf;
}

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

const char* const kServiceGroups[][5] = {
    {"Network", "DNS Client", "DHCP Client", "Firewall", nullptr},
    {"System", "Print Spooler", "Updates", "Search Indexer", "Time Sync"},
    {"Media", "Audio", "Bluetooth", nullptr, nullptr},
};

} // namespace

MainWindow::MainWindow()
    : shown_processes(
          [this] {
              const std::string q = lower(query.get());
              std::vector<Process> out;
              for (auto& p : processes.get())
                  if (q.empty() || lower(p.name).find(q) != std::string::npos) out.push_back(p);
              return out;
          },
          processes, query),
      line_color_(Color::hex(0x5B8CFF)) {
    // the numbers on the cards count up/down smoothly, the bars glide
    cpu_bar = tween(cpu, Transition(600ms, Easing::EaseInOut));
    memory_bar = tween(memory, Transition(600ms, Easing::EaseInOut));
    disk_bar = tween(disk, Transition(600ms, Easing::EaseInOut));
    subs_.push_back(cpu_bar.subscribe([this](double v) { cpu_text = percent(v); }));
    subs_.push_back(memory_bar.subscribe([this](double v) { memory_text = percent(v); }));
    subs_.push_back(disk_bar.subscribe([this](double v) { disk_text = percent(v); }));

    std::vector<std::pair<std::string, bool>> list;
    for (auto& g : kServiceGroups)
        for (int i = 1; i < 5 && g[i]; ++i) list.push_back({g[i], i != 3});
    services = list;

    ui_.setup(window_, *this);
    window_.set_min_size(720, 520);
    ui_.heading->color(theme().map([](const Theme& t) { return t.accent; }));

    // ── what forms don't have yet: a sortable table and a tree, added in code ──
    ui_.procs->add(Table(shown_processes,
                         {
                             Column<Process>{"PID", 80, [](const Process& p) { return std::to_string(p.pid); }, {}, Align::RightCenter},
                             Column<Process>{"Name", 220, [](const Process& p) { return p.name; }},
                             Column<Process>{"CPU %", 90, [](const Process& p) { return fixed(p.cpu); }, {}, Align::RightCenter},
                             Column<Process>{"Memory, MB", 120, [](const Process& p) { return fixed(p.memory, 0); }, {}, Align::RightCenter},
                             Column<Process>{"Threads", 90, [](const Process& p) { return std::to_string(p.threads); }, {}, Align::RightCenter},
                         })
                       .sorted(2, false)
                       .on_activate([this](int item) {
                           const auto list = shown_processes.get();
                           if (item < 0 || item >= static_cast<int>(list.size())) return;
                           const Process p = list[static_cast<std::size_t>(item)];
                           dialogs::confirm_async(&window_, "End \xE2\x80\x9C" + p.name + "\xE2\x80\x9D?",
                                                  "Unsaved data in the program may be lost (in this sample nothing really ends).",
                                                  [this, p](bool yes) {
                                                      if (yes) end_process(p.pid);
                                                  },
                                                  "End process", "Cancel");
                       })
                       .a11y_label("Processes")
                       .flex());
    ui_.services->add(TreeView(services.map([this](const auto&) { return service_tree(); }))
                          .expanded()
                          .on_activate([this](const TreeItem& item) {
                              if (item.children.size()) return;
                              auto list = services.get();
                              for (auto& [name, running] : list)
                                  if (name == item.label) {
                                      running = !running;
                                      toast(window_, name + (running ? " started" : " stopped"), running ? ToastKind::Success : ToastKind::Info);
                                  }
                              services = list;
                          })
                          .a11y_label("Services")
                          .flex());

    // ── the chart: color and style changes animate ──
    color_tween_ = tween(chart_color, Transition(450ms)).subscribe([this](Color c) {
        line_color_ = c;
        frame = frame.get() + 1;
    });
    subs_.push_back(chart_style.as_observable().subscribe([this](int s) {
        if (s == shown_style_) return;
        prev_style_ = shown_style_;
        shown_style_ = s;
        style_mix_ = 0;
        style_anim_ = anim::run(Transition(400ms, Easing::EaseInOut), [this](float t) {
            style_mix_ = t;
            frame = frame.get() + 1;
        });
    }));

    // ── updates: a timer that follows the settings ──
    subs_.push_back(refresh_ms.as_observable().subscribe([this](int) { restart_timer(); }));
    subs_.push_back(live.as_observable().subscribe([this](bool) { restart_timer(); }));

    window_.menu({
        Menu("&File", {
            Action("&Export processes...", [this] { export_csv(); }).shortcut("Ctrl+E"),
            Separator(),
            Action("&Quit", [] { App::quit(); }).shortcut("Ctrl+Q"),
        }),
        Menu("&View", {
            Action("&Overview", [this] { tab = 0; }).checked(tab.map([](int t) { return t == 0; })).radio().shortcut("Ctrl+1"),
            Action("&Processes", [this] { tab = 1; }).checked(tab.map([](int t) { return t == 1; })).radio().shortcut("Ctrl+2"),
            Action("&Services", [this] { tab = 2; }).checked(tab.map([](int t) { return t == 2; })).radio().shortcut("Ctrl+3"),
            Action("S&ettings", [this] { tab = 3; }).checked(tab.map([](int t) { return t == 3; })).radio().shortcut("Ctrl+4"),
            Separator(),
            Action("&Live updates").checked(live).shortcut("Ctrl+P"),
            Action("&Dark theme", [] { theme() = theme().get().name == "dark" ? Theme::light() : Theme::dark(); })
                .checked(theme().map([](const Theme& t) { return t.name == "dark"; }))
                .shortcut("Ctrl+T"),
        }),
        Menu("&Help", {
            Action("&About", [this] {
                dialogs::alert(&window_, "System Monitor",
                               "A Pulse Layer sample with simulated data: tabs, a Canvas chart, a Table, a TreeView, "
                               "tweened values and toasts.");
            }),
        }),
    });

    for (int i = 0; i < 90; ++i) tick(); // a full chart to start with
}

void MainWindow::restart_timer() {
    timer_.reset();
    status_text = live.get() ? "Updated every " + std::to_string(refresh_ms.get()) + " ms" : "Paused";
    if (!live.get()) return;
    timer_ = App::every(std::chrono::milliseconds(std::max(200, refresh_ms.get())), [this] { tick(); });
}

void MainWindow::tick() {
    sim_.step();
    cpu = sim_.cpu / 100;
    memory = sim_.memory / 100;
    disk = sim_.disk / 100;
    processes = sim_.processes;
    proc_count = std::to_string(shown_processes.get().size()) + " processes";

    history_.push_back(cpu.get());
    while (history_.size() > 90) history_.pop_front();
    double peak = 0, sum = 0;
    for (double v : history_) {
        peak = std::max(peak, v);
        sum += v;
    }
    peak_text = "peak " + percent(peak) + "  \xC2\xB7  average " + percent(sum / static_cast<double>(history_.size()));

    // the newest sample slides in during the interval: a smoothly scrolling chart
    scroll_ = 0;
    scroll_anim_ = anim::run(Transition(std::chrono::milliseconds(refresh_ms.get() * 9 / 10), Easing::Linear), [this](float t) {
        scroll_ = t;
        frame = frame.get() + 1;
    });

    const auto now = std::chrono::steady_clock::now();
    if (alerts.get() && cpu.get() * 100 > threshold.get() && now - last_alert_ > 15s && window_.is_open()) {
        last_alert_ = now;
        toast(window_, "The CPU is busy: " + percent(cpu.get()), ToastKind::Warning, 4000ms);
    }
}

void MainWindow::draw_chart(Painter& p, const Rect& r) {
    const Theme& t = th();
    p.fill_rect(r, t.surface, t.radius);
    p.stroke_rect(r, t.border, 1, t.radius);
    const Rect a = r.inset(Insets(44, 14, 14, 24));
    for (int i = 0; i <= 4; ++i) { // grid
        const float y = a.y + a.h * static_cast<float>(i) / 4;
        p.line({a.x, y}, {a.right(), y}, t.border.with_alpha(0.6f), 1);
        p.text({r.x + 4, y - 9, 34, 18}, std::to_string(100 - i * 25) + "%", t.font_small, t.text_dim, Align::RightCenter);
    }
    if (history_.size() < 2) return;
    const int n = static_cast<int>(history_.size());
    const float step = a.w / 88.f;
    auto pt = [&](int i) {
        const float x = a.right() - (static_cast<float>(n - 1 - i) + 1 - scroll_) * step;
        return Point{x, a.bottom() - a.h * static_cast<float>(history_[static_cast<std::size_t>(i)])};
    };
    p.save();
    p.clip(a);
    auto draw_style = [&](int style, float alpha) {
        if (alpha <= 0.01f) return;
        const Color c = line_color_.with_alpha(line_color_.a * alpha);
        std::vector<Point> line;
        for (int i = 0; i < n; ++i) line.push_back(pt(i));
        if (style == 2) { // bars
            for (int i = 0; i < n; ++i) {
                const Point q = line[static_cast<std::size_t>(i)];
                p.fill_rect({q.x - step * 0.36f, q.y, step * 0.72f, a.bottom() - q.y}, c.with_alpha(c.a * 0.85f), 2);
            }
            return;
        }
        if (style == 0) { // area
            std::vector<Point> area = line;
            area.push_back({line.back().x, a.bottom()});
            area.push_back({line.front().x, a.bottom()});
            p.fill_polygon(area, c.with_alpha(c.a * 0.22f));
        }
        p.polyline(line, c, style == 1 ? 2.5f : 2.f);
        const Point last = line.back();
        p.fill_ellipse({last.x - 4, last.y - 4, 8, 8}, c);
    };
    if (prev_style_ == shown_style_ || style_mix_ >= 1) {
        draw_style(shown_style_, 1);
    } else { // cross-fade to the new style
        draw_style(prev_style_, 1 - style_mix_);
        draw_style(shown_style_, style_mix_);
    }
    p.restore();
    // the threshold line
    const float ty = a.bottom() - a.h * static_cast<float>(threshold.get()) / 100.f;
    p.line({a.x, ty}, {a.right(), ty}, t.warning.with_alpha(0.7f), 1);
    p.text({a.x + 4, ty - 16, 200, 14}, "warning above " + std::to_string(threshold.get()) + "%", t.font_small, t.warning, Align::LeftCenter);
}

std::vector<TreeItem> MainWindow::service_tree() const {
    std::vector<TreeItem> roots;
    const auto list = services.get();
    for (auto& g : kServiceGroups) {
        TreeItem group{g[0], {}, g[0], ""};
        int running = 0;
        for (int i = 1; i < 5 && g[i]; ++i)
            for (auto& [name, on] : list)
                if (name == g[i]) {
                    group.children.push_back({name, {}, name, on ? "Running" : "Stopped"});
                    running += on;
                }
        group.detail = std::to_string(running) + " of " + std::to_string(group.children.size()) + " running";
        roots.push_back(group);
    }
    return roots;
}

void MainWindow::end_process(int pid) {
    auto& list = sim_.processes;
    for (auto it = list.begin(); it != list.end(); ++it)
        if (it->pid == pid) {
            toast(window_, "Ended " + it->name, ToastKind::Info);
            list.erase(it);
            break;
        }
    processes = sim_.processes;
}

void MainWindow::export_csv() {
    auto path = dialogs::save_file(&window_, "Export processes", {{"CSV", "*.csv"}}, "processes.csv");
    if (!path) return;
    std::ofstream out(*path);
    out << "pid,name,cpu,memory_mb,threads\n";
    for (auto& p : processes.get()) out << p.pid << "," << p.name << "," << fixed(p.cpu) << "," << fixed(p.memory, 0) << "," << p.threads << "\n";
    ToastOptions o;
    o.kind = ToastKind::Success;
    o.action_label = "Copy path";
    o.action = [file = *path] { App::set_clipboard(file); };
    toast(window_, "Exported " + std::to_string(processes.get().size()) + " processes", o);
}
