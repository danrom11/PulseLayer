#pragma once
#include "ui_mainwindow.hpp"
#include "store.hpp"
#include "preferences.hpp"
#include "about.hpp"

#include <memory>

// The main form: a searchable list of snippets on the left, the editor on the right.
// Preferences and About are separate forms, opened on demand.
class MainWindow {
public:
    explicit MainWindow(Store& store);
    void show() { window_.show(); }

private:
    friend struct Ui::MainWindow; // the form binds to the state and actions below

    struct Row {
        Snippet snippet;
        bool selected = false;
        bool operator==(const Row&) const = default;
    };

    // ── actions (called by the form) ──
    void add_snippet() { store_.add(); }
    void delete_snippet() { store_.remove(store_.selected.get()); }
    void copy_code() { pulseui::App::set_clipboard(code.get()); }
    void open_preferences();
    void open_about();

    void open(int id); // load a snippet into the editor fields
    pulseui::Node row(const Row& r);

    Store& store_;
    std::unique_ptr<Preferences> preferences_;
    std::unique_ptr<About> about_;

    // ── editor state: the fields of the open snippet ──
    pulse::property<std::string> title, code, query;
    pulse::property<int> language{0};
    bool loading_ = false; // true while open() fills the fields

    // ── derived ──
    pulse::computed<std::vector<Row>> rows;
    pulse::computed<std::string> stats;
    pulse::computed<std::string> count_text;
    pulse::computed<bool> has_selection;

    std::vector<pulse::subscription> subs_;

    Ui::MainWindow ui_;
    pulseui::Window window_{Ui::MainWindow::form_title, Ui::MainWindow::form_width, Ui::MainWindow::form_height};
};
