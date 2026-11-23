#include "canvas.hpp"

#include <algorithm>
#include <cmath>

namespace layer {

namespace {
constexpr float kTitleBar = 30;
constexpr float kHandle = 7;
float snap(float v, float g = 4) { return std::round(v / g) * g; }

bool is_horizontal(const View* v) { return std::string(v->type_name()) == "HStack"; }
} // namespace

DesignCanvas::DesignCanvas() {
  style_.clip = true;
  focusable_ = true;
}

void DesignCanvas::attach(Document* doc) {
  doc_ = doc;
  // Repaint whenever the document or the selection changes; relayout on size.
  keep(doc->revision.changed.connect([this](int) {
    hover_ = nullptr; // the tree may have been rebuilt
    invalidate_layout();
  }).scoped());
  keep(doc->selection.changed.connect([this](View*) { invalidate(); }).scoped());
  keep(doc->width.changed.connect([this](double) { invalidate_layout(); }).scoped());
  keep(doc->height.changed.connect([this](double) { invalidate_layout(); }).scoped());
  keep(doc->title.changed.connect([this](const std::string&) { invalidate(); }).scoped());
}

Rect DesignCanvas::form_rect() const {
  if (!doc_) return {};
  const float fw = static_cast<float>(doc_->width.get()), fh = static_cast<float>(doc_->height.get());
  const Rect b = bounds();
  const float x = std::max(32.f, std::round((b.w - fw) / 2)) + pan_.x;
  const float y = std::max(32.f + kTitleBar, std::round((b.h - fh - kTitleBar) / 2) + kTitleBar) + pan_.y;
  return {x, y, fw, fh};
}

void DesignCanvas::layout() {
  if (!doc_ || !doc_->root()) return;
  doc_->root()->set_frame(form_rect());
  doc_->root()->layout();
}

Rect DesignCanvas::rect_of(const View* v) const {
  const Point o = v->to_window({0, 0}) - to_window({0, 0});
  return {o.x, o.y, v->frame().w, v->frame().h};
}

View* DesignCanvas::view_at_in(View* v, Point p) const {
  if (!v->is_visible() || !rect_of(v).contains(p)) return nullptr;
  if (!Document::is_container(v)) return v; // the inner parts of a view (a NumberField's text field) are not separate
  const auto& ch = v->children();
  for (auto it = ch.rbegin(); it != ch.rend(); ++it)
    if (View* h = view_at_in(it->get(), p)) return h;
  return v;
}

View* DesignCanvas::view_at(Point p) const {
  return doc_ && doc_->root() ? view_at_in(doc_->root(), p) : nullptr;
}

std::optional<DesignCanvas::Drop> DesignCanvas::drop_at(Point p, const View* dragged) const {
  View* v = view_at(p);
  // climb to the nearest container that may receive the view
  while (v && (!Document::is_container(v) || (dragged && doc_->is_descendant(v, dragged)) ||
               (std::string(v->type_name()) == "Scroll" && !v->children().empty() && v->children()[0].get() != dragged)))
    v = v->parent();
  if (!v || !doc_->is_descendant(v, doc_->root())) return std::nullopt;

  Drop d;
  d.container = v;
  const Rect cr = rect_of(v);
  if (Document::is_layer(v)) {
    d.pos = {snap(p.x - cr.x), snap(p.y - cr.y)};
    d.marker = {p.x - 1, p.y - 1, 2, 2};
    d.index = static_cast<int>(v->children().size());
    return d;
  }
  const bool horiz = is_horizontal(v);
  int index = 0;
  float marker_pos = horiz ? cr.x + v->style().padding.left : cr.y + v->style().padding.top;
  const auto& ch = v->children();
  for (std::size_t i = 0; i < ch.size(); ++i) {
    const View* c = ch[i].get();
    if (!c->is_visible()) continue;
    const Rect r = rect_of(c);
    const float center = horiz ? r.x + r.w / 2 : r.y + r.h / 2;
    if ((horiz ? p.x : p.y) > center) {
      index = static_cast<int>(i) + 1;
      marker_pos = horiz ? r.right() + 2 : r.bottom() + 2;
    }
  }
  d.index = index;
  d.line = true;
  const Rect inner = cr.inset(v->style().padding);
  d.marker = horiz ? Rect{marker_pos - 1.5f, inner.y, 3, std::max(16.f, inner.h)}
                   : Rect{inner.x, marker_pos - 1.5f, std::max(16.f, inner.w), 3};
  return d;
}

Rect DesignCanvas::handle_rect(const Rect& r, int i) const {
  const float xs[3] = {r.x, r.x + r.w / 2, r.right()};
  const float ys[3] = {r.y, r.y + r.h / 2, r.bottom()};
  // 0 TL, 1 T, 2 TR, 3 R, 4 BR, 5 B, 6 BL, 7 L
  static const int ix[8] = {0, 1, 2, 2, 2, 1, 0, 0};
  static const int iy[8] = {0, 0, 0, 1, 2, 2, 2, 1};
  return {xs[ix[i]] - kHandle / 2, ys[iy[i]] - kHandle / 2, kHandle, kHandle};
}

int DesignCanvas::handle_at(Point p) const {
  View* sel = doc_ ? doc_->selection.get() : nullptr;
  if (!sel || sel == doc_->root()) return -1;
  const Rect r = rect_of(sel);
  for (int i = 0; i < 8; ++i)
    if (handle_rect(r, i).outset(3).contains(p)) return i;
  return -1;
}

void DesignCanvas::apply_resize(Point p) {
  View* sel = doc_->selection.get();
  if (!sel) return;
  const float dx = p.x - press_.x, dy = p.y - press_.y;
  Rect r = start_rect_;
  const bool left = handle_ == 0 || handle_ == 6 || handle_ == 7;
  const bool right = handle_ == 2 || handle_ == 3 || handle_ == 4;
  const bool top = handle_ == 0 || handle_ == 1 || handle_ == 2;
  const bool bottom = handle_ == 4 || handle_ == 5 || handle_ == 6;
  if (right) r.w = std::max(8.f, snap(start_rect_.w + dx));
  if (bottom) r.h = std::max(8.f, snap(start_rect_.h + dy));
  if (left) {
    r.w = std::max(8.f, snap(start_rect_.w - dx));
    r.x = start_rect_.right() - r.w;
  }
  if (top) {
    r.h = std::max(8.f, snap(start_rect_.h - dy));
    r.y = start_rect_.bottom() - r.h;
  }
  start_edit();
  if (left || right) sel->style().width = r.w;
  if (top || bottom) sel->style().height = r.h;
  if (Document::is_layer(sel->parent()) && (left || top)) {
    sel->style().position = {start_pos_.x + (r.x - start_rect_.x), start_pos_.y + (r.y - start_rect_.y)};
  }
  doc_->changed();
}

void DesignCanvas::pan_by(Point d) {
  // Keep at least a strip of the form visible so it cannot get lost off-screen.
  const Point before = pan_;
  pan_ = pan_ + d;
  if (doc_) {
    const Rect b = bounds();
    pan_.x = std::round(pan_.x);
    pan_.y = std::round(pan_.y);
    const float keep = 80;
    const Rect f = form_rect();
    if (f.right() < keep) pan_.x += keep - f.right();
    if (f.x > b.w - keep) pan_.x -= f.x - (b.w - keep);
    if (f.bottom() < keep) pan_.y += keep - f.bottom();
    if (f.y - kTitleBar > b.h - keep) pan_.y -= f.y - kTitleBar - (b.h - keep);
  }
  if (pan_ == before) return;
  // Only the form moves inside the canvas: re-place it and repaint, without
  // asking the window for a full relayout on every mouse move.
  layout();
  invalidate();
}

void DesignCanvas::start_edit() {
  if (edit_started_) return;
  edit_started_ = true;
  doc_->begin_edit();
}

// ── palette drag & drop ──────────────────────────────────────────────────────

void DesignCanvas::drag_new_hover(Point window_pos, const std::string& type) {
  new_type_ = type;
  const Point p = from_window(window_pos);
  ghost_ = p;
  drop_ = bounds().contains(p) ? drop_at(p, nullptr) : std::nullopt;
  invalidate();
}

bool DesignCanvas::drop_new(Point window_pos, const std::string& type) {
  const Point p = from_window(window_pos);
  auto d = bounds().contains(p) ? drop_at(p, nullptr) : std::nullopt;
  drop_.reset();
  new_type_.clear();
  invalidate();
  if (!d) return false;
  doc_->insert(type, d->container, d->index, d->pos);
  return true;
}

void DesignCanvas::drag_new_cancel() {
  drop_.reset();
  new_type_.clear();
  invalidate();
}

// ── painting ─────────────────────────────────────────────────────────────────

void DesignCanvas::paint(Painter& p) {
  const Theme& t = th();
  const Rect b = bounds();
  p.fill_rect(b, t.window.darker(0.18f));
  // grid (moves with the form): ~100 hairlines, cheap to draw every frame
  const Color line = t.text.with_alpha(0.035f);
  constexpr float kGrid = 24;
  for (float x = std::fmod(pan_.x, kGrid) + (pan_.x < 0 ? kGrid : 0); x < b.w; x += kGrid) p.fill_rect({x, 0, 1, b.h}, line);
  for (float y = std::fmod(pan_.y, kGrid) + (pan_.y < 0 ? kGrid : 0); y < b.h; y += kGrid) p.fill_rect({0, y, b.w, 1}, line);
  if (!doc_) return;

  // mock window around the form
  const Rect f = form_rect();
  const Rect win{f.x, f.y - kTitleBar, f.w, f.h + kTitleBar};
  for (int i = 4; i >= 1; --i) p.fill_rect(win.outset(static_cast<float>(i) * 3).translated(0, 4), t.shadow.with_alpha(0.08f), 10.f + i * 3);
  p.fill_rect(win, t.surface_alt, 8);
  p.fill_rect({win.x, win.y + kTitleBar - 1, win.w, 1}, t.border);
  for (int i = 0; i < 3; ++i) {
    static const Color dots[3] = {Color::hex(0xFF5F57), Color::hex(0xFEBC2E), Color::hex(0x28C840)};
    p.fill_ellipse({win.x + 12 + i * 18.f, win.y + 10, 11, 11}, dots[i]);
  }
  p.text({win.x, win.y, win.w, kTitleBar}, doc_->title.get(), t.font_small.with_weight(FontWeight::Medium), t.text_dim, Align::Center);
  p.save();
  p.clip(f);
  p.fill_rect(f, t.window);
  p.restore();
  p.stroke_rect(win, t.border, 1, 8);
}

void DesignCanvas::paint_overlay(Painter& p) {
  if (!doc_) return;
  const Theme& t = th();
  if (hover_ && hover_ != doc_->selection.get() && mode_ == Mode::None) {
    p.stroke_rect(rect_of(hover_).outset(1), t.accent.with_alpha(0.5f), 1);
  }
  if (View* sel = doc_->selection.get()) {
    const Rect r = rect_of(sel);
    p.stroke_rect(r.outset(1), t.accent, 1.5f);
    if (sel != doc_->root())
      for (int i = 0; i < 8; ++i) {
        const Rect h = handle_rect(r, i);
        p.fill_rect(h, Color::white(), 1.5f);
        p.stroke_rect(h, t.accent, 1.2f, 1.5f);
      }
    // label chip: "Button  #ok  120 × 30"
    std::string label = sel->type_name();
    if (!sel->id().empty()) label += "  #" + sel->id();
    label += "  " + std::to_string(static_cast<int>(r.w)) + " × " + std::to_string(static_cast<int>(r.h));
    const Font f = t.font_small;
    const Size ts = measure_text(label, f, 0);
    Rect chip{r.x, r.y - ts.h - 10, ts.w + 12, ts.h + 6};
    if (chip.y < 2) chip.y = r.bottom() + 4;
    p.fill_rect(chip, t.accent, 3);
    p.text(chip, label, f, t.on_accent, Align::Center);
  }
  if (!new_type_.empty() && bounds().contains(ghost_)) {
    const Size gs = measure_text(new_type_, t.font_small, 0);
    const Rect g{ghost_.x + 12, ghost_.y + 10, gs.w + 16, gs.h + 8};
    p.fill_rect(g, t.accent.with_alpha(0.9f), 4);
    p.text(g, new_type_, t.font_small, t.on_accent, Align::Center);
  }
  if (drop_) {
    if (drop_->container) p.stroke_rect(rect_of(drop_->container), t.success.with_alpha(0.6f), 1.5f);
    p.fill_rect(drop_->marker.outset(drop_->line ? 0.f : 3.f), t.success, 2);
  }
}

// ── input ────────────────────────────────────────────────────────────────────

bool DesignCanvas::on_event(InputEvent& e) {
  if (!doc_) return false;
  const Point p = e.pos;
  switch (e.type) {
    case InputEvent::MouseMove: {
      if (mode_ == Mode::None) {
        View* h = view_at(p);
        if (h != hover_) {
          hover_ = h;
          invalidate();
        }
        const int hi = handle_at(p);
        static const Cursor cursors[8] = {Cursor::SizeNWSE, Cursor::SizeNS, Cursor::SizeNESW, Cursor::SizeWE,
                                          Cursor::SizeNWSE, Cursor::SizeNS, Cursor::SizeNESW, Cursor::SizeWE};
        cursor_ = hi >= 0 ? cursors[hi] : Cursor::Arrow;
        return false;
      }
      View* sel = doc_->selection.get();
      if (mode_ == Mode::Pan) {
        pan_by(pan_start_ + (p - press_) - pan_);
        return true;
      }
      if (mode_ == Mode::Resize) {
        apply_resize(p);
        return true;
      }
      if (mode_ == Mode::Pressed && sel && sel != doc_->root() && (p - press_).x * (p - press_).x + (p - press_).y * (p - press_).y > 25) {
        mode_ = Document::is_layer(sel->parent()) ? Mode::MoveFree : Mode::MoveDrop;
        cursor_ = Cursor::SizeAll;
      }
      if (mode_ == Mode::MoveFree && sel) {
        start_edit();
        sel->style().position = {snap(start_pos_.x + p.x - press_.x), snap(start_pos_.y + p.y - press_.y)};
        doc_->changed();
        return true;
      }
      if (mode_ == Mode::MoveDrop && sel) {
        drop_ = drop_at(p, sel);
        invalidate();
        return true;
      }
      return true;
    }
    case InputEvent::MouseDown: {
      focus();
      press_ = p;
      edit_started_ = false;
      if (e.button == MouseButton::Middle || (e.button == MouseButton::Left && (e.mods & Mod::Alt))) {
        mode_ = Mode::Pan;
        pan_start_ = pan_;
        return true;
      }
      if (e.button != MouseButton::Left) return true;
      handle_ = handle_at(p);
      if (handle_ >= 0) {
        View* sel = doc_->selection.get();
        mode_ = Mode::Resize;
        start_rect_ = rect_of(sel);
        start_pos_ = sel->style().position;
        return true;
      }
      View* v = view_at(p);
      doc_->selection = v ? v : doc_->root();
      if (v) start_pos_ = v->style().position;
      mode_ = Mode::Pressed;
      return true;
    }
    case InputEvent::MouseUp: {
      if (mode_ == Mode::MoveDrop && drop_) {
        View* sel = doc_->selection.get();
        auto d = *drop_;
        drop_.reset();
        if (Document::is_layer(d.container)) {
          doc_->move(sel, d.container, d.index);
          if (View* moved = doc_->selection.get()) {
            moved->style().position = d.pos;
            doc_->changed();
          }
        } else {
          doc_->move(sel, d.container, d.index);
        }
      }
      drop_.reset();
      mode_ = Mode::None;
      cursor_ = Cursor::Arrow;
      invalidate();
      return true;
    }
    case InputEvent::Wheel:
      if (e.mods & Mod::Shift) pan_by({e.wheel_dy * 40, 0});
      else pan_by({e.wheel_dx * 40, e.wheel_dy * 40});
      return true;
    case InputEvent::KeyDown: {
      View* sel = doc_->selection.get();
      if (!sel || sel == doc_->root()) return false;
      const float step = (e.mods & Mod::Shift) ? 8.f : 1.f;
      Point d{};
      if (e.key == Key::Left) d.x = -step;
      else if (e.key == Key::Right) d.x = step;
      else if (e.key == Key::Up) d.y = -step;
      else if (e.key == Key::Down) d.y = step;
      else return false;
      if (Document::is_layer(sel->parent())) {
        doc_->begin_edit("nudge");
        sel->style().position = sel->style().position + d;
        doc_->changed();
      } else if (View* parent = sel->parent()) {
        // in a stack, arrows reorder
        const int i = parent->index_of(sel);
        const int dir = (d.x + d.y) > 0 ? 2 : -1;
        doc_->move(sel, parent, std::clamp(i + dir, 0, static_cast<int>(parent->children().size())));
      }
      return true;
    }
    default:
      return false;
  }
}

} // namespace layer
