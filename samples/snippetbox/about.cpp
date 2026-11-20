#include "about.hpp"

using namespace pulseui;

About::About(Store& store) {
    // A stream works as a binding target as well as a property does.
    stats = store.snippets.map([](const std::vector<Snippet>& all) {
        std::size_t chars = 0;
        for (const Snippet& s : all) chars += s.code.size();
        return std::to_string(all.size()) + " snippets, " + std::to_string(chars) + " characters of code";
    });

    ui_.setup(window_, *this);
    ui_.logo->color(th().accent);
    ui_.blurb->dim();

    window_.on_close = [this] {
        close();
        return false;
    };
    window_.shortcut("Escape", [this] { close(); });
}
