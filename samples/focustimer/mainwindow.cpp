#include "mainwindow.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace pulseui;
using namespace std::chrono_literals;

int MainWindow::duration(int p) {
    switch (p) {
        case ShortBreak: return 5 * 60;
        case LongBreak: return 15 * 60;
        default: return 25 * 60;
    }
}

Color MainWindow::phase_color(int p) {
    switch (p) {
        case ShortBreak: return Color::hex(0x2EB67D);
        case LongBreak: return Color::hex(0x3D7EFF);
        default: return Color::hex(0xF05A3C);
    }
}

MainWindow::MainWindow()
    : clock(
          [this] {
              char buf[16];
              std::snprintf(buf, sizeof buf, "%02d:%02d", remaining.get() / 60, remaining.get() % 60);
              return std::string(buf);
          },
          remaining),
      clock_color([this] { return phase_color(phase.get()); }, phase),
      elapsed([this] { return 1.0 - static_cast<double>(remaining.get()) / duration(phase.get()); }, remaining, phase),
      start_label(
          [this] {
              if (running.get()) return std::string("Pause");
              return std::string(remaining.get() == duration(phase.get()) ? "Start" : "Resume");
          },
          running, remaining, phase),
      hint(
          [this] {
              if (!running.get() && remaining.get() != duration(phase.get())) return std::string("Paused");
              return std::string(phase.get() == Focus ? "Stay focused - one thing at a time"
                                                      : "Stand up, stretch, drink some water");
          },
          running, remaining, phase),
      completed_text([this] { return std::to_string(completed.get()); }, completed),
      session_text([this] { return "Session " + std::to_string(completed.get() + 1); }, completed) {
    // ── behaviour: a handful of reactions to state changes ──
    // A new phase stops the timer and refills it.
    phase.changed.connect([this](int p) {
        running = false;
        remaining = duration(p);
    });
    // Running starts / stops a UI-thread timer.
    running.changed.connect([this](bool on) {
        if (on) ticker_ = App::every(1s, [this] { tick(); });
        else ticker_.reset();
    });

    ui_.setup(window_, *this); // builds the form and connects its bindings

    // The window title shows the countdown while it runs (visible in the taskbar / Dock).
    title_ = clock.as_observable().subscribe([this](const std::string& c) {
        window_.set_title(running.get() ? c + " - Focus Timer" : std::string(Ui::MainWindow::form_title));
    });

    ui_.dial->insert(0, ring()); // custom drawing behind the clock text
    ui_.hint->dim();
    ui_.session->dim();
}

void MainWindow::reset() {
    running = false;
    remaining = duration(phase.get());
}

void MainWindow::tick() {
    if (remaining.get() > 1) remaining = remaining.get() - 1;
    else next_phase(true);
}

void MainWindow::next_phase(bool count_it) {
    const bool was_running = running.get();
    if (phase.get() == Focus) {
        if (count_it) completed = completed.get() + 1;
        phase = (completed.get() > 0 && completed.get() % 4 == 0) ? LongBreak : ShortBreak;
    } else {
        phase = Focus;
    }
    if (count_it ? auto_start.get() : was_running) running = true;
}

// The countdown ring: a track and an arc for the time that is left.
Node MainWindow::ring() {
    return Canvas([this](Painter& p, const Rect& r) {
               constexpr float kPi = 3.14159265f;
               const float radius = std::min(r.w, r.h) / 2 - 12;
               const Point c = r.center();
               auto point = [&](float t) {
                   const float a = -kPi / 2 + 2 * kPi * t;
                   return Point{c.x + radius * std::cos(a), c.y + radius * std::sin(a)};
               };
               std::vector<Point> track;
               for (int i = 0; i <= 120; ++i) track.push_back(point(i / 120.f));
               p.polyline(track, th().control, 10, true);

               const float left = static_cast<float>(remaining.get()) / static_cast<float>(duration(phase.get()));
               std::vector<Point> arc;
               const int steps = std::max(2, static_cast<int>(120 * left));
               for (int i = 0; i <= steps; ++i) arc.push_back(point(left * static_cast<float>(i) / static_cast<float>(steps)));
               const Color color = phase_color(phase.get());
               if (left > 0) p.polyline(arc, color, 10);
               const Point head = point(left);
               p.fill_ellipse({head.x - 9, head.y - 9, 18, 18}, color);
           })
        .redraw_on(remaining)
        .redraw_on(phase);
}
