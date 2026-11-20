#include "preferences.hpp"

using namespace pulseui;

Preferences::Preferences(Store& s) : store(s) {
    ui_.setup(window_, *this);
    ui_.note->dim();

    window_.on_close = [this] {
        close();
        return false;
    };
    window_.shortcut("Escape", [this] { close(); });
}
