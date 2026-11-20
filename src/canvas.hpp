#pragma once
#include <optional>
#include <string>

#include "document.hpp"

namespace layer {

// The design surface: shows the edited form inside a mock window and turns
// mouse input into document edits (select, move, resize, drop).
class DesignCanvas : public ViewT<DesignCanvas> {
public:
  DesignCanvas();
  const char* type_name() const override { return "DesignCanvas"; }

  void attach(Document* doc);

  // Palette drag & drop (window coordinates).
  void drag_new_hover(Point window_pos, const std::string& type);
  bool drop_new(Point window_pos, const std::string& type);
  void drag_new_cancel();

  Size measure_content(Size avail) override { return avail; }
  void layout() override;
  void paint(Painter& p) override;
  void paint_overlay(Painter& p) override;
  bool on_event(InputEvent& e) override;
  bool hit_test_children(Point) const override { return false; }

  Rect form_rect() const;

private:
  struct Drop {
    View* container = nullptr;
    int index = -1;
    Point pos{};   // for Layer containers (container-local)
    Rect marker{}; // canvas-local indicator
    bool line = false;
  };

  Rect rect_of(const View* v) const; // canvas-local
  View* view_at(Point p) const;
  View* view_at_in(View* v, Point p) const;
  std::optional<Drop> drop_at(Point p, const View* dragged) const;
  int handle_at(Point p) const;
  Rect handle_rect(const Rect& r, int i) const;
  void apply_resize(Point p);

  Document* doc_ = nullptr;
  View* hover_ = nullptr;
  enum class Mode { None, Pressed, MoveFree, MoveDrop, Resize, Pan } mode_ = Mode::None;
  Point press_{};
  Point pan_{0, 0};
  Point pan_start_{};
  Rect start_rect_{};
  Point start_pos_{};
  int handle_ = -1;
  std::optional<Drop> drop_;
  std::string new_type_;
  Point ghost_{};
  bool edit_started_ = false;
  void start_edit();
  void pan_by(Point d);
};

} // namespace layer
