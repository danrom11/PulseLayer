#pragma once
#include "ui_statscard.hpp"
#include "task.hpp"

#include <vector>

// A reusable piece of UI: the "StatsCard" form is placed inside MainWindow as a
// Component (palette → Project → StatsCard). Its bindings (headline, detail, the
// ring's draw and redraw_on) refer to the members of this class; MainWindow owns
// one StatsCard and binds the component's "model" to it.
class StatsCard {
public:
    explicit StatsCard(pulse::property<std::vector<Task>>& tasks);

private:
    friend struct Ui::StatsCard;

    void draw_ring(pulseui::Painter& p, const pulseui::Rect& r);

    pulse::computed<double> ratio;          // finished / all
    pulse::computed<std::string> headline;  // "3 of 7 done"
    pulse::computed<std::string> detail;    // "2 overdue · 1 due today"
    pulse::property<double> shown{0};       // the ratio as drawn: glides to each new value
    pulse::subscription tween_;
};
