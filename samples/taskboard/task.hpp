#pragma once
// The model of the board: plain data, no UI code.
#include <string>

enum class Priority { Low, Normal, High };

struct Task {
    int id = 0;
    std::string title;
    Priority priority = Priority::Normal;
    bool done = false;
    bool operator==(const Task&) const = default;
};

inline const char* priority_name(Priority p) {
    switch (p) {
        case Priority::Low: return "Low";
        case Priority::High: return "High";
        default: return "Normal";
    }
}
