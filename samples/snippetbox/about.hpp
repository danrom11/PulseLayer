#pragma once
#include "ui_about.hpp"
#include "store.hpp"

class About {
public:
    explicit About(Store& store);
    void show() { window_.show(); }

private:
    friend struct Ui::About;

    void close() { window_.hide(); }

    // Live statistics, shown by the form while the window is open.
    pulse::observable<std::string> stats;

    Ui::About ui_;
    pulseui::Window window_{Ui::About::form_title, Ui::About::form_width, Ui::About::form_height};
};
