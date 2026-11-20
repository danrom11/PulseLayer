#pragma once
#include "ui_mainwindow.hpp"

#include <random>
#include <string>
#include <vector>

// Color Studio: three sliders mix a color; everything else is derived from it.
//
//   red, green, blue  ->  color  ->  preview background, readable text color,
//                                    contrast grade, hex field, tints & shades
//
// All of that is wired in the form (select a view in Design mode to see its
// bindings). The hex field works the other way round: committing a color calls
// apply_hex(), which sets the sliders, and the rest follows.
class MainWindow {
public:
    MainWindow();
    void show() { window_.show(); }

private:
    friend struct Ui::MainWindow; // the form binds to the state and actions below

    // ── actions (called by the form) ──
    void apply_hex(const std::string& text);
    void copy_hex();
    void randomize();
    void save_color();
    void clear_palette() { palette = std::vector<pulseui::Color>{}; }

    void set_color(pulseui::Color c);
    pulseui::Node swatch(pulseui::Color c, float size, bool stretch = false);

    // ── state ──
    pulse::property<double> red{61}, green{126}, blue{255};
    pulse::property<std::string> hex_text;          // what the hex field shows
    pulse::property<std::string> copy_label{"Copy"};
    pulse::property<std::vector<pulseui::Color>> palette;

    // ── derived ──
    pulse::computed<pulseui::Color> color;
    pulse::computed<pulseui::Color> text_color;
    pulse::computed<std::string> contrast_label;
    pulse::computed<std::string> red_text, green_text, blue_text;
    pulse::computed<std::vector<pulseui::Color>> shades;
    pulse::computed<bool> has_palette;

    pulse::subscription hex_sync_, copied_flash_;
    std::mt19937 rng_{std::random_device{}()};

    Ui::MainWindow ui_;
    pulseui::Window window_{Ui::MainWindow::form_title, Ui::MainWindow::form_width, Ui::MainWindow::form_height};
};
