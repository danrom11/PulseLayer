#pragma once
#include "ui_mainwindow.hpp"

#include <optional>
#include <string>
#include <vector>

// Photo Board - drag and drop, SVG icons and animation.
//
//   * drop image files on the board (from Explorer / Finder / another app);
//     drag a card out of the window to copy the file (or its name) elsewhere;
//   * the cards are a keyed ForEach that wraps: changing the sort order makes
//     the cards fly to their new places, the size slider resizes them smoothly,
//     the board color glides (a tweened binding), hovering lifts a card;
//   * double click opens a preview sheet; right click: rename (a prompt),
//     remove (with Undo in a toast), copy the name;
//   * the sidebar is designed in Pulse Layer (Slider, ColorPicker, Segmented).
struct Photo {
    int id = 0;
    std::string name;
    std::string path;            // an image file; empty for a generated card
    pulseui::Color a, b;         // generated cards: gradient colors
    int art = 0;                 // generated cards: which decoration
    bool operator==(const Photo&) const = default;
};

class MainWindow {
public:
    MainWindow();
    void show() { window_.show(); }

private:
    friend struct Ui::MainWindow;

    // ── actions (bound in the form) ──
    void add_samples();
    void clear_board();
    void open_images();
    void toggle_theme();

    void add_files(const std::vector<std::string>& files);
    void remove(int id);
    void rename(int id);
    void preview(int id);
    pulseui::Node card(const Photo& p);
    pulseui::Node art(const Photo& p, bool big);
    const Photo* find(int id) const;

    // ── state ──
    pulse::property<std::vector<Photo>> photos;
    pulse::property<double> card_size{210};
    pulse::property<pulseui::Color> board_color{pulseui::Color::hex(0x161A21)};
    pulse::property<int> sort{0};
    std::optional<Photo> last_removed_;
    int next_id_ = 1, sample_round_ = 0;
    pulseui::View* sheet_ = nullptr;

    // ── derived ──
    pulse::computed<std::vector<Photo>> sorted;
    pulse::computed<std::string> count_text;
    pulse::computed<bool> has_photos;

    Ui::MainWindow ui_;
    pulseui::Window window_{Ui::MainWindow::form_title, Ui::MainWindow::form_width, Ui::MainWindow::form_height};
};
