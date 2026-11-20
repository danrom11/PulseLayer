#include "mainwindow.hpp"

int main() {
    pulseui::App app;

    // One store for the whole app; every form gets a reference to it.
    Store store;

    // The theme follows a preference: flip the toggle and every window repaints.
    store.dark.changed.connect([](bool dark) {
        pulseui::theme() = dark ? pulseui::Theme::dark() : pulseui::Theme::light();
    });

    MainWindow window(store);
    window.show();

    return app.run();
}
