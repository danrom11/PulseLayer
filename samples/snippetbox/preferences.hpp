#pragma once
#include "ui_preferences.hpp"
#include "store.hpp"

// Preferences: the form binds its toggles straight to the store (store.dark,
// store.line_numbers...), so the main window changes while you click - there is
// no "Apply" step. The model of this form is the Preferences object itself; it
// exposes the store as a member so bindings can reach into it.
class Preferences {
public:
    explicit Preferences(Store& store);
    void show() { window_.show(); }

private:
    friend struct Ui::Preferences;

    void close() { window_.hide(); } // closing only hides, so the window keeps its state

    Store& store;

    Ui::Preferences ui_;
    pulseui::Window window_{Ui::Preferences::form_title, Ui::Preferences::form_width, Ui::Preferences::form_height};
};
