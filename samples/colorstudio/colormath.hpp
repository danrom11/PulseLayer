#pragma once
// Color helpers: WCAG contrast and tints / shades. No UI code here.
#include <pulseui/core/color.hpp>

#include <cmath>
#include <string>
#include <vector>

namespace colormath {

// Relative luminance of an sRGB color (WCAG 2.x).
inline double luminance(pulseui::Color c) {
    auto channel = [](float v) {
        const double x = v;
        return x <= 0.03928 ? x / 12.92 : std::pow((x + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * channel(c.r) + 0.7152 * channel(c.g) + 0.0722 * channel(c.b);
}

// Contrast ratio between two colors: 1 (none) .. 21 (black on white).
inline double contrast(pulseui::Color a, pulseui::Color b) {
    double la = luminance(a), lb = luminance(b);
    if (la < lb) std::swap(la, lb);
    return (la + 0.05) / (lb + 0.05);
}

// Black or white - whichever reads better on `background`.
inline pulseui::Color readable_on(pulseui::Color background) {
    return contrast(background, pulseui::Color::black()) >= contrast(background, pulseui::Color::white())
               ? pulseui::Color::hex(0x111318)
               : pulseui::Color::white();
}

inline std::string grade(double ratio) {
    if (ratio >= 7) return "AAA";
    if (ratio >= 4.5) return "AA";
    if (ratio >= 3) return "AA large text only";
    return "not readable";
}

// Three shades (towards black), the color itself, three tints (towards white).
inline std::vector<pulseui::Color> tints_and_shades(pulseui::Color c) {
    std::vector<pulseui::Color> out;
    for (float t : {0.6f, 0.4f, 0.2f}) out.push_back(pulseui::Color::mix(c, pulseui::Color::black(), t));
    out.push_back(c);
    for (float t : {0.2f, 0.4f, 0.6f}) out.push_back(pulseui::Color::mix(c, pulseui::Color::white(), t));
    return out;
}

} // namespace colormath
