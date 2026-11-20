#include "mainwindow.hpp"
#include "colormath.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>

using namespace pulseui;
using namespace std::chrono_literals;

namespace {

int channel(double v) { return static_cast<int>(v + 0.5); }

std::string upper_hex(Color c) {
    std::string h = c.to_hex();
    for (char& ch : h) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    return h;
}

} // namespace

MainWindow::MainWindow()
    : color([this] { return Color::rgb8(channel(red.get()), channel(green.get()), channel(blue.get())); }, red, green, blue),
      text_color([this] { return colormath::readable_on(color.get()); }, color),
      contrast_label(
          [this] {
              const double ratio = colormath::contrast(color.get(), text_color.get());
              char buf[64];
              std::snprintf(buf, sizeof buf, "Contrast %.1f : 1  ·  %s", ratio, colormath::grade(ratio).c_str());
              return std::string(buf);
          },
          color, text_color),
      red_text([this] { return std::to_string(channel(red.get())); }, red),
      green_text([this] { return std::to_string(channel(green.get())); }, green),
      blue_text([this] { return std::to_string(channel(blue.get())); }, blue),
      shades([this] { return colormath::tints_and_shades(color.get()); }, color),
      has_palette([this] { return !palette.get().empty(); }, palette) {
    // The hex field shows the current color.
    hex_sync_ = color.as_observable().subscribe([this](const Color& c) { hex_text = upper_hex(c); });

    ui_.setup(window_, *this); // builds the form and connects its bindings
    window_.set_min_size(620, 460);

    // Details the designer does not cover: slider steps, dimmed texts, and the two
    // color lists, rendered with ForEach into placeholder containers.
    for (Slider* s : {ui_.red, ui_.green, ui_.blue}) s->step(1);
    ui_.intro->dim();
    ui_.shades_title->dim();
    ui_.shades->add(ForEach(shades, [this](const Color& c) { return swatch(c, 44, true); }).horizontal().spacing(8));
    ui_.palette->add(ForEach(palette, [this](const Color& c) { return swatch(c, 30); }).horizontal().spacing(6));
    ui_.palette->add(Text("Saved colors appear here").dim().visible(has_palette.map([](bool has) { return !has; })));

    window_.shortcut("Ctrl+R", [this] { randomize(); });
}

void MainWindow::apply_hex(const std::string& text) {
    std::string t = text;
    if (!t.empty() && t[0] != '#') t = "#" + t;
    if (auto c = Color::parse(t)) set_color(*c);
    else hex_text = upper_hex(color.get()); // invalid: put the current color back
}

void MainWindow::copy_hex() {
    App::set_clipboard(hex_text.get());
    copy_label = "Copied";
    copied_flash_ = App::after(1200ms, [this] { copy_label = "Copy"; });
}

void MainWindow::randomize() {
    std::uniform_int_distribution<int> d(0, 255);
    set_color(Color::rgb8(d(rng_), d(rng_), d(rng_)));
}

void MainWindow::save_color() {
    const Color c = color.get();
    palette.update([c](std::vector<Color>& p) {
        std::erase(p, c);
        p.insert(p.begin(), c);
        if (p.size() > 8) p.resize(8);
    });
}

void MainWindow::set_color(Color c) {
    red = c.r * 255.0;
    green = c.g * 255.0;
    blue = c.b * 255.0;
}

Node MainWindow::swatch(Color c, float size, bool stretch) {
    return Spacer()
        .flex(stretch ? 1.f : 0.f)
        .width(stretch ? 0.f : size)
        .height(size)
        .radius(size / 4)
        .background(c)
        .border(Color::hex(0x000000, 0.15f))
        .tooltip(c.to_hex())
        .on_click([this, c] { set_color(c); });
}
