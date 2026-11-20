#pragma once
#include "ui_mainwindow.hpp"
#include "task.hpp"

#include <vector>

// A task board: add, prioritise, search, filter and complete tasks.
//
// The form (mainwindow.pform) binds its views to the members below: the text
// fields edit `draft` and `query`, the Add button is enabled by `can_add` and
// calls add_task(), the footer shows `summary` and `progress`... Select a view in
// Design mode to see its bindings. Nothing here updates the screen by hand:
// everything shown is derived from `tasks`, `query` and `filter`.
class MainWindow {
public:
    MainWindow();
    void show() { window_.show(); }

private:
    friend struct Ui::MainWindow; // the form binds to the state and actions below

    // ── actions (called by the form) ──
    void add_task();
    void clear_completed();

    void toggle(int id);
    void remove(int id);
    pulseui::Node row(const Task& task);

    // ── state ──
    pulse::property<std::vector<Task>> tasks;
    pulse::property<std::string> draft;  // text of the new task
    pulse::property<std::string> query;  // search box
    pulse::property<int> priority{1};    // Low / Normal / High for the new task
    pulse::property<int> filter{0};      // All / Active / Done
    int next_id_ = 1;

    // ── derived state (recomputed when their sources change) ──
    pulse::computed<std::vector<Task>> visible;
    pulse::computed<std::string> summary;
    pulse::computed<double> progress;
    pulse::computed<bool> has_completed;
    pulse::computed<bool> can_add;

    Ui::MainWindow ui_; // the views with an id: ui_.list, ui_.filters, ui_.search...
    pulseui::Window window_{Ui::MainWindow::form_title, Ui::MainWindow::form_width, Ui::MainWindow::form_height};
};
