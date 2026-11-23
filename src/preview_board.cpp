// Preview board: the edited form side by side in several conditions - dark and
// light theme, smaller and larger window sizes, and a pseudo-translation (longer
// texts with accents) that shows where a translated UI would overflow.
#include "panels.hpp"

#include <algorithm>

namespace layer {

using i18n::tr;

namespace {

// "Save" -> "[Šàvé···]": each text ~40% longer, accented, bracketed (a classic pseudo-locale).
std::string pseudo(const std::string& s) {
  if (s.empty()) return s;
  static const char* const map[26] = {"\xC3\xA0", "\xC6\x80", "\xC3\xA7", "\xC4\x91", "\xC3\xA9", "\xC6\x92", "\xC4\x9F",
                                      "\xC4\xA5", "\xC3\xAE", "\xC4\xB5", "\xC4\xB7", "\xC4\xBC", "\xE1\xB8\xBF", "\xC3\xB1",
                                      "\xC3\xB6", "\xC6\xA5", "\xC9\x8B", "\xC5\x99", "\xC5\xA1", "\xC5\xA3", "\xC3\xBC",
                                      "\xE1\xB9\xBD", "\xC5\xB5", "\xC3\x97", "\xC3\xBD", "\xC5\xBE"};
  std::string out = "[";
  std::size_t letters = 0;
  for (char c : s) {
    if (c >= 'a' && c <= 'z') {
      out += map[c - 'a'];
      ++letters;
    } else if (c >= 'A' && c <= 'Z') {
      out += c;
      ++letters;
    } else {
      out += c;
    }
  }
  for (std::size_t i = 0; i < std::max<std::size_t>(2, letters * 2 / 5); ++i) out += "\xC2\xB7"; // ·
  return out + "]";
}

void pseudo_translate(View& v) {
  if (auto* t = dynamic_cast<Text*>(&v)) t->set_text(pseudo(t->text()));
  else if (auto* b = dynamic_cast<Button*>(&v)) b->set_label(pseudo(b->label()));
  else if (auto* c = dynamic_cast<Checkbox*>(&v)) c->set_label(pseudo(c->label()));
  else if (auto* g = dynamic_cast<Toggle*>(&v)) g->set_label(pseudo(g->label()));
  else if (auto* f = dynamic_cast<TextField*>(&v)) f->set_placeholder(pseudo(f->placeholder_value()));
  else if (auto* s = dynamic_cast<Select*>(&v)) {
    auto o = s->options();
    for (auto& x : o) x = pseudo(x);
    s->set_options(o);
  }
  for (auto& c : v.children()) pseudo_translate(*c);
}

struct Variant {
  std::string label;
  bool dark;
  float width_factor;
  bool pseudo_text;
};

struct Card {
  std::string label;
  Image image;
  float w = 0, h = 0; // logical size of the rendered form
  float k = 1;        // display scale (the same for all cards, so sizes compare)
  bool operator==(const Card& o) const {
    return label == o.label && image.cache_key() == o.image.cache_key() && w == o.w && h == o.h;
  }
};

} // namespace

// Renders the form JSON in one condition (theme, size, texts) to an image.
static Card render_card(const std::string& form_json, const Variant& v) {
  Card card;
  card.label = v.label;
  auto j = json::Value::parse(form_json);
  if (!j) return card;
  auto form = form_from_json(*j);
  if (!form) return card;
  if (v.pseudo_text) pseudo_translate(*form->root.get());
  card.w = std::round(form->width * v.width_factor);
  card.h = form->height;
  const Theme saved = theme().get();
  const Theme wanted = v.dark ? Theme::dark() : Theme::light();
  if (!(saved == wanted)) theme() = wanted;
  {
    Window w(form->title, card.w, card.h);
    w.content(std::move(form->root));
    w.handle_resize({card.w, card.h});
    w.update_layout();
    card.image = w.grab();
  }
  if (!(saved == wanted)) theme() = saved;
  return card;
}

void Designer::cmd_preview_board() {
  if (board_window && board_window->is_open()) {
    board_window->show();
    board_window->activate();
    return;
  }
  struct BoardState {
    pulse::property<bool> dark{true}, light{true}, compact{false}, wide{true}, pseudo_text{true};
    pulse::property<std::vector<Card>> cards;
    pulse::subscription refresh_timer;
    std::vector<pulse::subscription> subs;
  };
  auto st = std::make_shared<BoardState>();
  board_state = st;

  auto refresh = [this, st] {
    sync_form();
    const std::string json = doc.to_json_text();
    std::vector<Variant> variants;
    auto add = [&](bool dark, float wf, bool ps) {
      std::string label = dark ? i18n::text("board.dark") : i18n::text("board.light");
      if (wf < 1) label += " \xC2\xB7 " + i18n::text("board.compact");
      if (wf > 1) label += " \xC2\xB7 " + i18n::text("board.wide");
      if (ps) label += " \xC2\xB7 " + i18n::text("board.pseudo");
      variants.push_back({label, dark, wf, ps});
    };
    std::vector<bool> themes;
    if (st->dark.get()) themes.push_back(true);
    if (st->light.get()) themes.push_back(false);
    for (bool dark : themes) {
      add(dark, 1.f, false);
      if (st->compact.get()) add(dark, 0.75f, false);
      if (st->wide.get()) add(dark, 1.4f, false);
      if (st->pseudo_text.get()) add(dark, 1.f, true);
    }
    std::vector<Card> cards;
    float widest = 1;
    for (auto& v : variants) {
      cards.push_back(render_card(json, v));
      widest = std::max({widest, cards.back().w, cards.back().h});
    }
    for (auto& c : cards) c.k = std::min(1.f, 420.f / widest);
    st->cards = cards;
  };
  // re-render shortly after the form or the options change (edits come in bursts)
  auto schedule = [st, refresh] {
    st->refresh_timer = App::after(std::chrono::milliseconds(250), refresh);
  };
  st->subs.push_back(doc.revision.as_observable().subscribe([schedule](int) { schedule(); }));
  for (auto* p : {&st->dark, &st->light, &st->compact, &st->wide, &st->pseudo_text})
    st->subs.push_back(p->as_observable().subscribe([schedule](bool) { schedule(); }));

  auto w = std::make_unique<Window>(tr("board.title"), 1100, 760);
  w->content(VStack{
      HStack{
          Text(tr("board.title")).weight(FontWeight::SemiBold),
          Spacer(),
          Checkbox(tr("board.dark"), st->dark).id("board-dark"),
          Checkbox(tr("board.light"), st->light).id("board-light"),
          Checkbox(tr("board.compact"), st->compact).id("board-compact"),
          Checkbox(tr("board.wide"), st->wide).id("board-wide"),
          Checkbox(tr("board.pseudo"), st->pseudo_text).id("board-pseudo").tooltip(tr("board.pseudo.tip")),
      }
          .spacing(14)
          .align_items(Alignment::Center),
      Scroll(ForEach(st->cards, [](const Card& c) {
               return Node(Panel{
                   Text(c.label).font_size(12).dim(),
                   ImageView(c.image).size(c.w * c.k, c.h * c.k),
               }
                               .spacing(6)
                               .padding(10));
             })
                 .wrap()
                 .spacing(14)
                 .id("board-cards"))
          .flex(),
  }
                 .spacing(12)
                 .padding(16));
  Window* raw = w.get();
  w->closed.connect([this, raw] {
    App::post([this, raw] {
      if (board_window.get() == raw) {
        board_window.reset();
        board_state.reset();
      }
    });
  });
  if (!Window::headless()) w->show();
  board_window = std::move(w);
  refresh();
  doc.status = i18n::Message{"status.board", ""};
}

} // namespace layer
