#include "mainwindow.hpp"

int main() {
    pulseui::App app;

    MainWindow window;
    window.show();

    return app.run();
}
