#pragma once
#include "ui_mainwindow.hpp"
#include "sensors.hpp"

#include <vector>

// A live dashboard driven by Pulse streams.
//
//   sensors.samples ──┬─ throttle ── map ──────────────────► card texts and bars
//                     ├─ map(cpu) ── scan(last 60) ────────► history ──► chart
//                     └─ map(cpu) ─┐
//   threshold (slider) ────────────┴─ combine_latest ─ distinct ─► alerting
//
// The form binds the cards straight to the streams below (cpu_text, mem_fraction...):
// a binding target can be a property, a computed value or any pulse::observable.
class MainWindow {
public:
    MainWindow();
    void show() { window_.show(); }

private:
    friend struct Ui::MainWindow; // the form binds to the members below

    static constexpr std::size_t kHistory = 60;
    void restart_timer();
    pulseui::Node chart();

    Sensors sensors_;

    // ── state ──
    pulse::property<bool> live{true};
    pulse::property<int> rate{1};                 // index in the Select
    pulse::property<double> threshold{80};
    pulse::property<std::vector<double>> history; // the last kHistory CPU values
    pulse::property<bool> alerting{false};
    pulse::property<int> alerts{0};
    pulse::property<double> network_peak{0};

    // ── streams and derived values the form displays ──
    pulse::observable<std::string> cpu_text, mem_text, net_text;
    pulse::observable<double> cpu_fraction, mem_fraction;
    pulse::observable<pulseui::Color> cpu_color;
    pulse::computed<std::string> peak_text, stats_text, threshold_text, alert_text;
    pulse::computed<pulseui::Color> alert_color;

    pulse::subscription ticker_;
    std::vector<pulse::subscription> subs_;

    Ui::MainWindow ui_;
    pulseui::Window window_{Ui::MainWindow::form_title, Ui::MainWindow::form_width, Ui::MainWindow::form_height};
};
