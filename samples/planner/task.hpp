#pragma once
#include <pulseui/pulseui.hpp>

#include <string>

// One task. The list row in mainwindow.pform binds to these fields:
// item.title, item.subtitle, item.done and item.dot (the priority color).
struct Task {
    int id = 0;
    std::string title;
    std::string tag;             // "#work", "#home"... (may be empty)
    pulseui::Date due;
    int priority = 1;            // 0 low, 1 normal, 2 high
    bool done = false;

    // shown by the row
    std::string subtitle;        // "Due Tue 24 Nov · #work"
    pulseui::Color dot;          // priority color

    bool operator==(const Task&) const = default;
};
