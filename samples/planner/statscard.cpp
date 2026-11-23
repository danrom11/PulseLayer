#include "statscard.hpp"

#include <cmath>

using namespace pulseui;
using namespace std::chrono_literals;

namespace {
int count_done(const std::vector<Task>& all) {
    int n = 0;
    for (auto& t : all) n += t.done;
    return n;
}
} // namespace

StatsCard::StatsCard(pulse::property<std::vector<Task>>& tasks)
    : ratio(
          [&tasks] {
              const auto all = tasks.get();
              return all.empty() ? 0.0 : static_cast<double>(count_done(all)) / static_cast<double>(all.size());
          },
          tasks),
      headline(
          [&tasks] {
              const auto all = tasks.get();
              if (all.empty()) return std::string("Nothing planned");
              const int done = count_done(all);
              if (done == static_cast<int>(all.size())) return std::string("All done!");
              return std::to_string(done) + " of " + std::to_string(all.size()) + " done";
          },
          tasks),
      detail(
          [&tasks] {
              const Date today = Date::today();
              int overdue = 0, due_today = 0;
              for (auto& t : tasks.get()) {
                  if (t.done) continue;
                  if (t.due < today) ++overdue;
                  else if (t.due == today) ++due_today;
              }
              if (!overdue && !due_today) return std::string("Nothing urgent");
              std::string s;
              if (overdue) s += std::to_string(overdue) + " overdue";
              if (due_today) s += (s.empty() ? "" : "  \xC2\xB7  ") + std::to_string(due_today) + " due today";
              return s;
          },
          tasks) {
    // the ring glides from the old share to the new one
    tween_ = tween(ratio, Transition(700ms, Easing::EaseInOut)).subscribe([this](double v) { shown = v; });
}

void StatsCard::draw_ring(Painter& p, const Rect& r) {
    const Theme& t = th();
    const float size = std::min(r.w, r.h) - 8;
    const Point c = r.center();
    const float radius = size / 2 - 7;
    auto arc = [&](float from, float to) {
        std::vector<Point> pts;
        const int steps = std::max(2, static_cast<int>((to - from) * 64));
        for (int i = 0; i <= steps; ++i) {
            const float a = (from + (to - from) * static_cast<float>(i) / steps) * 6.2831853f - 1.5707963f;
            pts.push_back({c.x + radius * std::cos(a), c.y + radius * std::sin(a)});
        }
        return pts;
    };
    p.polyline(arc(0, 1), t.border, 12);
    const float v = static_cast<float>(shown.get());
    if (v > 0.001f) {
        const Color col = Color::mix(t.accent, t.success, v);
        p.polyline(arc(0, v), col, 12);
        // round caps
        const auto pts = arc(0, v);
        p.fill_ellipse({pts.front().x - 6, pts.front().y - 6, 12, 12}, col);
        p.fill_ellipse({pts.back().x - 6, pts.back().y - 6, 12, 12}, col);
    }
    p.text(r, std::to_string(static_cast<int>(std::lround(v * 100))) + "%", t.font.with_size(26).with_weight(FontWeight::Bold), t.text,
           Align::Center);
}
