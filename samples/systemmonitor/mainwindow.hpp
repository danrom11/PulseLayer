#pragma once
#include "simulator.hpp"
#include "ui_mainwindow.hpp"

#include <chrono>
#include <deque>
#include <vector>

// System Monitor - tabs, live data and smooth motion.
//
//   * the form (Design mode) has four Tabs pages: stat cards in a Grid with
//     ProgressBars, a Canvas chart bound to draw_chart(), a filter field,
//     NumberFields, a RadioGroup, a Toggle and a ColorPicker for the settings;
//   * the numbers and bars glide (tweened streams), the chart scrolls smoothly
//     between samples and morphs when its style or color changes;
//   * code adds what forms don't have: a sortable Table of processes (double
//     click asks before ending one), a TreeView of services, a menu bar and
//     warning toasts when the CPU stays busy.
class MainWindow {
public:
    MainWindow();
    void show() { window_.show(); }

private:
    friend struct Ui::MainWindow;

    void draw_chart(pulseui::Painter& p, const pulseui::Rect& r);
    void tick();
    void restart_timer();
    void end_process(int pid);
    void export_csv();
    std::vector<pulseui::TreeItem> service_tree() const;

    Simulator sim_;

    // ── state bound by the form ──
    pulse::property<bool> live{true}, alerts{true};
    pulse::property<int> refresh_ms{1000}, threshold{80}, chart_style{0}, tab{0};
    pulse::property<pulseui::Color> chart_color{pulseui::Color::hex(0x5B8CFF)};
    pulse::property<std::string> query;
    pulse::property<double> cpu{0}, memory{0}, disk{0}; // 0..1
    pulse::property<std::string> cpu_text, memory_text, disk_text, peak_text, status_text, proc_count;
    pulse::observable<double> cpu_bar, memory_bar, disk_bar; // tweened
    pulse::property<int> frame{0};                          // the chart repaints when it changes

    pulse::property<std::vector<Process>> processes;
    pulse::computed<std::vector<Process>> shown_processes;
    pulse::property<std::vector<std::pair<std::string, bool>>> services; // name, running

    // ── the chart ──
    std::deque<double> history_;   // CPU samples 0..1
    float scroll_ = 1;             // 0..1: how far the newest sample has slid in
    float style_mix_ = 0;          // animates between styles
    int shown_style_ = 0, prev_style_ = 0;
    std::chrono::steady_clock::time_point last_alert_{};
    pulseui::Color line_color_;
    pulse::subscription scroll_anim_, style_anim_, timer_, color_tween_;
    std::vector<pulse::subscription> subs_;

    Ui::MainWindow ui_;
    pulseui::Window window_{Ui::MainWindow::form_title, Ui::MainWindow::form_width, Ui::MainWindow::form_height};
};
