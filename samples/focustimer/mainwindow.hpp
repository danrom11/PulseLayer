#pragma once
#include "ui_mainwindow.hpp"

#include <string>

// A Pomodoro timer: 25 minutes of focus, then a short break; every fourth
// break is a long one.
//
// The timer itself only decrements `remaining` once a second. The form binds
// the clock text and its color, the progress bar, the Start/Pause label and the
// counters to the derived values below; the buttons call the actions.
class MainWindow {
public:
    MainWindow();
    void show() { window_.show(); }

private:
    friend struct Ui::MainWindow; // the form binds to the state and actions below

    enum Phase { Focus, ShortBreak, LongBreak };
    static int duration(int phase); // seconds
    static pulseui::Color phase_color(int phase);

    // ── actions (called by the form) ──
    void toggle_running() { running = !running.get(); }
    void reset();
    void skip() { next_phase(false); }

    void tick();
    void next_phase(bool count_it);
    pulseui::Node ring();

    // ── state ──
    pulse::property<int> phase{Focus};
    pulse::property<int> remaining{duration(Focus)};
    pulse::property<bool> running{false};
    pulse::property<bool> auto_start{false};
    pulse::property<int> completed{0};

    // ── derived ──
    pulse::computed<std::string> clock;
    pulse::computed<pulseui::Color> clock_color;
    pulse::computed<double> elapsed; // 0 -> 1 within the phase
    pulse::computed<std::string> start_label;
    pulse::computed<std::string> hint;
    pulse::computed<std::string> completed_text;
    pulse::computed<std::string> session_text;

    pulse::subscription ticker_; // the 1 s timer while running
    pulse::subscription title_;  // window title follows the clock

    Ui::MainWindow ui_;
    pulseui::Window window_{Ui::MainWindow::form_title, Ui::MainWindow::form_width, Ui::MainWindow::form_height};
};
