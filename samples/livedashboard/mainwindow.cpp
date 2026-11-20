#include "mainwindow.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <numeric>

using namespace pulseui;
using namespace std::chrono_literals;

namespace {

std::string fmt(const char* pattern, double v) {
    char buf[64];
    std::snprintf(buf, sizeof buf, pattern, v);
    return buf;
}

} // namespace

MainWindow::MainWindow()
    : peak_text([this] { return fmt("peak %.1f MB/s", network_peak.get()); }, network_peak),
      stats_text(
          [this] {
              const std::vector<double> h = history.get();
              if (h.empty()) return std::string("waiting for data...");
              const auto [lo, hi] = std::minmax_element(h.begin(), h.end());
              const double avg = std::accumulate(h.begin(), h.end(), 0.0) / static_cast<double>(h.size());
              return fmt("min %.0f  ·  ", *lo) + fmt("avg %.0f  ·  ", avg) + fmt("max %.0f %%", *hi);
          },
          history),
      threshold_text([this] { return fmt("%.0f %%", threshold.get()); }, threshold),
      alert_text(
          [this] {
              if (alerting.get()) return std::string("⚠ CPU above the limit");
              const int n = alerts.get();
              return n == 0 ? std::string("No alerts yet") : std::to_string(n) + (n == 1 ? " alert so far" : " alerts so far");
          },
          alerts, alerting),
      alert_color([this] { return alerting.get() ? th().danger : th().text_dim; }, alerting) {
    // ── the streams ──
    const pulse::observable<Sample> samples = sensors_.samples.as_observable();
    const auto cpu = samples | pulse::map([](const Sample& s) { return s.cpu; });
    // Numbers that change 10 times a second are hard to read: show at most 4 updates per second.
    const auto display = samples | pulse::throttle(250ms);

    cpu_text = display | pulse::map([](const Sample& s) { return fmt("%.0f %%", s.cpu); });
    cpu_fraction = display | pulse::map([](const Sample& s) { return s.cpu / 100; });
    cpu_color = alerting.map([](bool on) { return on ? th().danger : th().text; });
    mem_text = display | pulse::map([](const Sample& s) { return fmt("%.1f GB", s.memory); });
    mem_fraction = display | pulse::map([](const Sample& s) { return s.memory / Sensors::kMemoryTotal; });
    net_text = display | pulse::map([](const Sample& s) { return fmt("%.1f MB/s", s.network); });

    // History for the chart: scan keeps a sliding window of the last values.
    subs_.push_back((cpu | pulse::scan(std::vector<double>{}, [](std::vector<double> acc, double v) {
                         acc.push_back(v);
                         if (acc.size() > kHistory) acc.erase(acc.begin());
                         return acc;
                     }))
                        .subscribe([this](const std::vector<double>& h) { history = h; }));
    subs_.push_back(samples.subscribe([this](const Sample& s) {
        if (s.network > network_peak.get()) network_peak = s.network;
    }));

    // Alert: CPU above the threshold, recalculated when either side changes.
    subs_.push_back((pulse::combine_latest(cpu, threshold.as_observable(), [](double c, double t) { return c > t; }) |
                     pulse::distinct_until_changed())
                        .subscribe([this](bool on) {
                            alerting = on;
                            if (on) alerts = alerts.get() + 1;
                        }));

    ui_.setup(window_, *this); // builds the form and connects its bindings
    window_.set_min_size(640, 480);

    ui_.chart->add(chart());
    ui_.threshold->step(1);
    for (Text* t : {ui_.cpu_label, ui_.mem_label, ui_.net_label, ui_.net_peak, ui_.stats}) t->dim();

    // The timer that feeds the sensors: restarted when Live or the rate changes.
    live.changed.connect([this](bool) { restart_timer(); });
    rate.changed.connect([this](int) { restart_timer(); });
    restart_timer();
    for (int i = 0; i < 25; ++i) sensors_.tick(); // start with some history on screen
}

void MainWindow::restart_timer() {
    ticker_.reset();
    if (!live.get()) return;
    static const std::chrono::milliseconds periods[] = {100ms, 250ms, 1000ms};
    ticker_ = App::every(periods[std::clamp(rate.get(), 0, 2)], [this] { sensors_.tick(); });
}

// Area chart of the history with a line for the alert threshold.
Node MainWindow::chart() {
    return Canvas([this](Painter& p, const Rect& r) {
               const Theme& t = th();
               auto y_of = [&](double v) { return r.y + r.h * (1.f - static_cast<float>(v / 100.0)); };
               for (int i = 0; i <= 4; ++i) {
                   const float y = r.y + r.h * static_cast<float>(i) / 4.f;
                   p.line({r.x, y}, {r.x + r.w, y}, t.border, 1);
               }
               const float ty = y_of(threshold.get());
               p.line({r.x, ty}, {r.x + r.w, ty}, t.danger.with_alpha(0.8f), 1.5f);

               const std::vector<double> h = history.get();
               if (h.size() < 2) return;
               const float step = r.w / static_cast<float>(kHistory - 1);
               const float x0 = r.x + r.w - step * static_cast<float>(h.size() - 1);
               std::vector<Point> line;
               for (std::size_t i = 0; i < h.size(); ++i) line.push_back({x0 + step * static_cast<float>(i), y_of(h[i])});
               std::vector<Point> area = line;
               area.push_back({line.back().x, r.y + r.h});
               area.push_back({line.front().x, r.y + r.h});
               const Color c = alerting.get() ? t.danger : t.accent;
               p.fill_polygon(area, c.with_alpha(0.16f));
               p.polyline(line, c, 2);
               p.fill_ellipse({line.back().x - 4, line.back().y - 4, 8, 8}, c);
           })
        .redraw_on(history)
        .redraw_on(threshold)
        .flex();
}
