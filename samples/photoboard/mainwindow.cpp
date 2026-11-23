#include "mainwindow.hpp"
#include "icons.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>

using namespace pulseui;
using namespace std::chrono_literals;

namespace {

const Svg& decoration(int i) {
    static const Svg arts[] = {Svg::parse(icons::mountain), Svg::parse(icons::wave), Svg::parse(icons::leaf), Svg::parse(icons::star)};
    return arts[static_cast<std::size_t>(i) % 4];
}

float hue_of(Color c) {
    float h, s, v;
    c.to_hsv(h, s, v);
    return h;
}

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

} // namespace

MainWindow::MainWindow()
    : sorted(
          [this] {
              auto list = photos.get();
              if (sort.get() == 1)
                  std::stable_sort(list.begin(), list.end(), [](const Photo& a, const Photo& b) { return lower(a.name) < lower(b.name); });
              else if (sort.get() == 2)
                  std::stable_sort(list.begin(), list.end(), [](const Photo& a, const Photo& b) { return hue_of(a.a) < hue_of(b.a); });
              return list;
          },
          photos, sort),
      count_text(
          [this] {
              const std::size_t n = photos.get().size();
              return n == 0 ? std::string("An empty board") : std::to_string(n) + (n == 1 ? " card" : " cards");
          },
          photos),
      has_photos([this] { return !photos.get().empty(); }, photos) {
    ui_.setup(window_, *this);
    window_.set_min_size(720, 480);

    // icons: SVG markup, drawn and tinted by PulseUI
    ui_.logo->set_source(icons::logo);
    ui_.logo->original_colors();
    ui_.open_btn->icon(icons::open);
    ui_.theme_btn->icon(icons::moon);
    ui_.add_samples->icon(icons::sparkle);
    ui_.clear->icon(icons::trash);
    ui_.intro->color(theme().map([](const Theme& t) { return t.text_dim; }));

    // the board: its color glides to each new value; files dropped on it are added
    ui_.board_scroll->background(tween(board_color, Transition(500ms, Easing::EaseInOut)));
    ui_.board_scroll->on_drop_files([this](const std::vector<std::string>& files) { add_files(files); },
                                    {".png", ".jpg", ".jpeg", ".bmp", ".gif"});

    // an empty-state hint that fades away, then the cards: a keyed ForEach that wraps
    ui_.board->add(Show(has_photos.map([](bool b) { return !b; }),
                        VStack{
                            Icon(icons::drop).size(64).color(theme().map([](const Theme& t) { return t.text_dim; })),
                            Text("Drop images here").font_size(18).weight(FontWeight::SemiBold),
                            Text("or press \xE2\x80\x9C" "Add sample cards\xE2\x80\x9D").dim(),
                        }
                            .spacing(10)
                            .align_items(Alignment::Center)
                            .padding(80))
                       .transition(350ms, Easing::EaseInOut));
    ui_.board->add(ForEach(sorted, [](const Photo& p) { return p.id; }, [this](const Photo& p) { return card(p); })
                       .wrap()
                       .spacing(18));

    window_.menu({
        Menu("&File", {
            Action("&Open images...", [this] { open_images(); }).shortcut("Ctrl+O").icon(icons::open),
            Action("Add &sample cards", [this] { add_samples(); }).shortcut("Ctrl+N").icon(icons::sparkle),
            Separator(),
            Action("&Clear board", [this] { clear_board(); }).enabled(has_photos).icon(icons::trash),
            Separator(),
            Action("&Quit", [] { App::quit(); }).shortcut("Ctrl+Q"),
        }),
        Menu("&View", {
            Action("&Bigger cards", [this] { card_size = std::min(320.0, card_size.get() + 30); }).shortcut("Ctrl+Up"),
            Action("&Smaller cards", [this] { card_size = std::max(140.0, card_size.get() - 30); }).shortcut("Ctrl+Down"),
            Separator(),
            Action("Sort by &added", [this] { sort = 0; }).checked(sort.map([](int s) { return s == 0; })).radio(),
            Action("Sort by &name", [this] { sort = 1; }).checked(sort.map([](int s) { return s == 1; })).radio(),
            Action("Sort by &color", [this] { sort = 2; }).checked(sort.map([](int s) { return s == 2; })).radio(),
            Separator(),
            Action("&Dark theme", [this] { toggle_theme(); }).checked(theme().map([](const Theme& t) { return t.name == "dark"; })).shortcut("Ctrl+T"),
        }),
    });

    add_samples();
}

const Photo* MainWindow::find(int id) const {
    for (auto& p : photos.get())
        if (p.id == id) return &p;
    return nullptr;
}

// ── cards ────────────────────────────────────────────────────────────────────

Node MainWindow::art(const Photo& p, bool big) {
    if (!p.path.empty()) return ImageView(p.path).a11y_label(p.name).a11y_role(Role::Image);
    const Photo copy = p;
    return Canvas([copy, big](Painter& g, const Rect& r) {
               g.fill_gradient(r, copy.a, copy.b, big ? 10.f : 8.f);
               // soft bubbles
               for (int i = 0; i < 4; ++i) {
                   const float k = static_cast<float>((copy.id * 37 + i * 53) % 100) / 100.f;
                   const float rad = r.h * (0.18f + 0.12f * static_cast<float>(i % 2));
                   g.fill_ellipse({r.x + r.w * k - rad, r.y + r.h * (0.15f + 0.2f * static_cast<float>(i)) - rad, rad * 2, rad * 2},
                                  Color::white().with_alpha(0.07f));
               }
               const float side = std::min(r.w, r.h) * 0.42f;
               decoration(copy.art).draw(g, {r.x + (r.w - side) / 2, r.y + (r.h - side) / 2, side, side}, Color::white(), false);
           })
        .radius(8)
        .clip() // the bubbles stay inside the picture
        .a11y_label(p.name)
        .a11y_role(Role::Image);
}

Node MainWindow::card(const Photo& p) {
    const int id = p.id;
    const std::string ext = p.path.empty() ? std::string("sample") : lower(std::filesystem::path(p.path).extension().string().substr(1));
    VStack c{
        art(p, false),
        HStack{Text(p.name).font_size(13).weight(FontWeight::Medium).flex(), Text(ext).font_size(11).dim()}.spacing(6).align_items(Alignment::Center),
    };
    c.spacing(8)
        .padding(10)
        .radius(12)
        .background(theme().map([](const Theme& t) { return t.surface; }))
        .border(theme().map([](const Theme& t) { return t.border; }))
        .animate(320ms, Easing::EaseInOut)  // position, size and colors glide
        .tooltip(p.path.empty() ? p.name : p.path)
        .cursor(Cursor::Hand)
        .drag_source([this, id] {
            const Photo* ph = find(id);
            if (!ph) return DragData{};
            return ph->path.empty() ? DragData{{}, ph->name} : DragData{{ph->path}, ""};
        })
        .context_menu([this, id] {
            return MenuItems{
                Action("Open preview", [this, id] { preview(id); }),
                Action("Rename...", [this, id] { rename(id); }),
                Action("Copy name", [this, id] {
                    if (const Photo* ph = find(id)) App::set_clipboard(ph->name);
                    toast(window_, "Copied", ToastKind::Info, 1500ms);
                }),
                Separator(),
                Action("Remove", [this, id] { remove(id); }).icon(icons::trash),
            };
        })
        .on_event_fn([this, id](const InputEvent& e) {
            if (e.type == InputEvent::MouseDown && e.button == MouseButton::Left && e.clicks >= 2)
                App::post([this, id] { preview(id); });
        })
        .on_mount_fn([this](VStack& v) {
            // the card follows the size slider (its frame animation makes it smooth)
            v.keep(card_size.as_observable().subscribe([&v](double w) {
                v.style().width = static_cast<float>(w);
                v.children().front()->style().height = static_cast<float>(w * 0.66);
                v.invalidate_layout();
            }));
            // hover lifts the card: the border lights up, the background brightens
            v.on_hover([&v](bool h) {
                const Theme& t = th();
                v.set_border_color(h ? t.accent : t.border);
                v.set_background(h ? t.surface.lighter(0.06f) : t.surface);
            });
        });
    return c;
}

// ── actions ──────────────────────────────────────────────────────────────────

void MainWindow::add_samples() {
    struct Sample {
        const char* name;
        std::uint32_t a, b;
        int art;
    };
    static const Sample samples[] = {
        {"Alpine morning", 0x7AA7FF, 0x2B3F9E, 0}, {"Ocean drift", 0x2EC4B6, 0x135D8C, 1}, {"Green study", 0x8BD17C, 0x2F7A3E, 2},
        {"Night stars", 0x6C4AB6, 0x1E1442, 3},     {"Desert dunes", 0xF6B26B, 0xB45309, 0}, {"Coral reef", 0xFF7A90, 0x9D2B54, 1},
        {"Fern valley", 0x4ADE80, 0x166534, 2},     {"Golden hour", 0xFCD34D, 0xD9480F, 3},
    };
    auto list = photos.get();
    const int start = (sample_round_++ * 4) % 8;
    for (int i = 0; i < 4; ++i) {
        const Sample& s = samples[(start + i) % 8];
        list.push_back({next_id_++, s.name, "", Color::hex(s.a), Color::hex(s.b), s.art});
    }
    photos = list;
    toast(window_, "Added 4 sample cards", ToastKind::Success, 2000ms);
}

void MainWindow::add_files(const std::vector<std::string>& files) {
    auto list = photos.get();
    for (auto& f : files) {
        Photo p;
        p.id = next_id_++;
        p.path = f;
        p.name = std::filesystem::path(f).stem().string();
        p.a = Color::hsv(static_cast<float>((p.id * 47) % 360), 0.5f, 0.8f); // for sorting by color
        list.push_back(p);
    }
    photos = list;
    toast(window_, "Added " + std::to_string(files.size()) + (files.size() == 1 ? " image" : " images"), ToastKind::Success);
}

void MainWindow::open_images() {
    if (auto f = dialogs::open_file(&window_, "Add an image", {{"Images", "*.png;*.jpg;*.jpeg;*.bmp;*.gif"}})) add_files({*f});
}

void MainWindow::remove(int id) {
    auto list = photos.get();
    auto it = std::find_if(list.begin(), list.end(), [id](const Photo& p) { return p.id == id; });
    if (it == list.end()) return;
    last_removed_ = *it;
    list.erase(it);
    photos = list;
    ToastOptions o;
    o.action_label = "Undo";
    o.action = [this] {
        if (!last_removed_) return;
        auto l = photos.get();
        l.push_back(*last_removed_);
        std::sort(l.begin(), l.end(), [](const Photo& a, const Photo& b) { return a.id < b.id; });
        photos = l;
        last_removed_.reset();
    };
    toast(window_, "Removed \xE2\x80\x9C" + last_removed_->name + "\xE2\x80\x9D", o);
}

void MainWindow::rename(int id) {
    const Photo* p = find(id);
    if (!p) return;
    dialogs::Message m;
    m.title = "Rename card";
    m.text = "New name:";
    m.icon = dialogs::Icon::None;
    m.buttons = {"Rename", "Cancel"};
    m.input = true;
    m.input_text = p->name;
    dialogs::message_async(&window_, m, [this, id](dialogs::Answer a) {
        if (a.button != 0 || a.text.empty()) return;
        auto list = photos.get();
        for (auto& ph : list)
            if (ph.id == id) ph.name = a.text;
        photos = list;
    });
}

void MainWindow::clear_board() {
    dialogs::confirm_async(&window_, "Clear the board?", "All cards will be removed (the image files stay where they are).",
                           [this](bool yes) {
                               if (yes) photos = std::vector<Photo>{};
                           },
                           "Clear", "Cancel");
}

void MainWindow::toggle_theme() {
    const bool dark = theme().get().name != "dark";
    theme() = dark ? Theme::dark() : Theme::light();
    board_color = dark ? Color::hex(0x161A21) : Color::hex(0xE9EDF3); // glides (tween)
}

void MainWindow::preview(int id) {
    const Photo* p = find(id);
    if (!p || sheet_) return;
    sheet_ = window_.present(
        VStack{
            Layer{art(*p, true)}.size(600, 400).radius(10).clip(),
            HStack{
                Text(p->name).font_size(16).weight(FontWeight::SemiBold),
                Spacer(),
                Button("Close", [this] {
                    if (sheet_) window_.close_popup(sheet_);
                }).primary(),
            }
                .align_items(Alignment::Center),
        }
            .spacing(14)
            .padding(18)
            .radius(14)
            .background(theme().map([](const Theme& t) { return t.surface; })),
        [this] { sheet_ = nullptr; });
}
