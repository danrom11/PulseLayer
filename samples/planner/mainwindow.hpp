#pragma once
#include "statscard.hpp"
#include "task.hpp"
#include "ui_mainwindow.hpp"

#include <optional>
#include <set>
#include <vector>

// Planner - a tour of the newer PulseUI features, built in Pulse Layer.
//
// In the form (Design mode):
//   * the task list is a ForEach row template bound to `visible`, keyed by `id`
//     (rows keep their state while the list changes); the row binds item.title,
//     item.subtitle, item.done, item.dot, and its events call toggle()/remove()
//     with the row's task;
//   * the card on the right is another form (StatsCard) used as a Component;
//   * DatePicker, Segmented, RadioGroup and Wrap come from the palette.
// In code (mainwindow.cpp): the menu bar, a context menu on the list, toasts
// with Undo, a confirmation dialog, suggestions in the text field, tag chips,
// animations (the list card and the side column glide; the progress tweens).
class MainWindow {
public:
    MainWindow();
    void show() { window_.show(); }

private:
    friend struct Ui::MainWindow;

    // ── actions (the form calls these) ──
    void add_task();
    void toggle(const Task& t, bool done);
    void remove(const Task& t);

    // ── more actions (menus, shortcuts) ──
    void clear_done();
    void undo_remove();
    void set_focus_mode(bool on);
    void rebuild_tags();
    bool stats_ratio_nonzero();
    Task make(std::string title, pulseui::Date due, int priority, bool done = false);

    // ── state ──
    pulse::property<std::vector<Task>> tasks;
    pulse::property<std::string> draft;
    pulse::property<pulseui::Date> due{pulseui::Date::today()};
    pulse::property<int> priority{1};
    pulse::property<int> filter{0};          // All / Open / Done
    pulse::property<std::string> tag_filter; // "" = every tag
    pulse::property<bool> dark{true}, focus_mode{false};
    std::optional<Task> last_removed_;
    std::set<std::string> shown_tags_;
    std::string shown_tag_filter_ = "-";
    int next_id_ = 1;

    // ── derived state ──
    pulse::computed<std::vector<Task>> visible;
    pulse::computed<std::string> summary, list_title, today_text;
    pulse::computed<bool> can_add;
    pulse::computed<double> ratio;
    pulse::observable<double> progress; // `ratio`, gliding (the progress bar binds to it)

    StatsCard stats{tasks}; // the model of the StatsCard component

    Ui::MainWindow ui_;
    pulseui::Window window_{Ui::MainWindow::form_title, Ui::MainWindow::form_width, Ui::MainWindow::form_height};
    std::vector<pulse::subscription> subs_;
};
