#include "panels.hpp"
#include "settings.hpp"

#include <pulse/version.hpp>

#include <cstdlib>
#include <algorithm>
#include <fstream>
#include <regex>

namespace layer {

using i18n::tr;

namespace {

// Theme-reactive colors: views bound to these repaint when the theme changes.
pulse::observable<Color> surface() { return theme().map([](const Theme& t) { return t.surface; }); }

Node section_title(Bind<std::string> s) {
  return Text(std::move(s)).font_size(11).weight(FontWeight::SemiBold).dim().padding(Insets(12, 10, 12, 4));
}

// ── palette ──────────────────────────────────────────────────────────────────

class PaletteItem : public ViewT<PaletteItem> {
public:
  PaletteItem(Designer& d, const meta::TypeInfo& t) : d_(&d), type_(t.name), label_(t.name), glyph_(t.glyph) {
    hover_repaint_ = true;
    cursor_ = Cursor::Hand;
    id_ = "palette-" + type_;
    bind(Bind<std::string>(tr("palette.item.tip")), [](View& v, const std::string& t) { v.set_tooltip(t); });
  }
  // a project part: "Component:<Form>" or "Custom:<Class>|<header>"
  PaletteItem(Designer& d, std::string spec, std::string label, std::string glyph, std::string tip)
      : d_(&d), type_(std::move(spec)), label_(std::move(label)), glyph_(std::move(glyph)) {
    hover_repaint_ = true;
    cursor_ = Cursor::Hand;
    id_ = "palette-" + type_;
    tooltip_ = std::move(tip);
  }
  const char* type_name() const override { return "PaletteItem"; }
  Size measure_content(Size) override { return {120, 28}; }

  void paint(Painter& p) override {
    const Theme& t = th();
    const Rect r = bounds().inset(Insets(6, 1));
    if (pressed_) p.fill_rect(r, t.control_pressed, t.radius_small);
    else if (hovered_) p.fill_rect(r, t.control_hover, t.radius_small);
    p.fill_rect({r.x + 6, r.y + 4, 20, r.h - 8}, t.accent.with_alpha(0.18f), 4);
    p.text({r.x + 6, r.y, 20, r.h}, glyph_, t.font_small, t.accent, Align::Center);
    p.text({r.x + 34, r.y, r.w - 38, r.h}, label_, t.font, t.text, Align::LeftCenter);
  }

  bool on_event(InputEvent& e) override {
    switch (e.type) {
      case InputEvent::MouseDown:
        if (e.button != MouseButton::Left) return false;
        start_ = e.window_pos;
        dragging_ = false;
        return true;
      case InputEvent::MouseMove:
        if (!pressed_) return false;
        if (!dragging_ && std::abs(e.window_pos.x - start_.x) + std::abs(e.window_pos.y - start_.y) > 6) dragging_ = true;
        if (dragging_ && d_->canvas) d_->canvas->drag_new_hover(e.window_pos, type_);
        return true;
      case InputEvent::MouseUp:
        if (dragging_) {
          if (d_->canvas && !d_->canvas->drop_new(e.window_pos, type_)) d_->canvas->drag_new_cancel();
        } else if (bounds().contains(e.pos)) {
          d_->doc.insert_near_selection(type_);
        }
        dragging_ = false;
        return true;
      default:
        return false;
    }
  }

private:
  Designer* d_;
  std::string type_, label_, glyph_;
  Point start_{};
  bool dragging_ = false;
};

// ── hierarchy ────────────────────────────────────────────────────────────────

// Two interlocked links: the "bound to code" icon (inspector and hierarchy).
void draw_chain(Painter& p, Point c, Color col, float scale = 1) {
  const float w = 11 * scale, h = 8 * scale, step = 7 * scale;
  p.stroke_rect({c.x - (w + step) / 2, c.y - h / 2, w, h}, col, 1.6f * scale, h / 2);
  p.stroke_rect({c.x - (w + step) / 2 + step, c.y - h / 2, w, h}, col, 1.6f * scale, h / 2);
}

bool has_bindings(const View* v) {
  for (auto& [key, value] : v->annotations())
    if (key.rfind("bind.", 0) == 0 || key.rfind("on.", 0) == 0) return true;
  return false;
}

struct TreeRow {
  View* view = nullptr;
  int depth = 0;
  bool operator==(const TreeRow&) const = default;
};

void flatten(View* v, int depth, std::vector<TreeRow>& out) {
  out.push_back({v, depth});
  if (!Document::is_container(v)) return; // inner parts of a view are not designed separately
  for (auto& c : v->children()) flatten(c.get(), depth + 1, out);
}

// Palette entries of the project: the forms other than the edited one, and view
// classes declared in its headers (class X : public pulseui::ViewT<X> ...).
std::vector<std::string> project_parts(Designer& d) {
  std::vector<std::string> out;
  const int active = d.project.active_form.get();
  for (std::size_t i = 0; i < d.project.forms.size(); ++i)
    if (static_cast<int>(i) != active) out.push_back("Component:" + d.project.forms[i].cls);
  static const std::regex view_class(
      R"(class\s+(\w+)\s*(?:final\s*)?:\s*public\s+(?:pulseui::)?(?:ViewT\s*<\s*\w+|View\b|VStack\b|HStack\b|Panel\b|Canvas\b|Layer\b))");
  for (auto& f : d.project.files) {
    const std::string ext = f.name.substr(f.name.find_last_of('.') == std::string::npos ? f.name.size() : f.name.find_last_of('.'));
    if (ext != ".hpp" && ext != ".h") continue;
    for (std::sregex_iterator it(f.text.begin(), f.text.end(), view_class), end; it != end; ++it)
      out.push_back("Custom:" + (*it)[1].str() + "|" + f.name);
  }
  return out;
}

// ── inspector ────────────────────────────────────────────────────────────────

struct PropRow {
  enum Kind { Header, Prop, Event, FormTitle, FormWidth, FormHeight } kind = Prop;
  View* view = nullptr;
  const meta::Property* prop = nullptr;
  std::string label;    // translation key (headers), property or event name
  std::string fallback; // shown when the key has no translation
  bool operator==(const PropRow&) const = default;
};

std::string trim(std::string s) {
  const auto b = s.find_first_not_of(" \t");
  const auto e = s.find_last_not_of(" \t");
  return b == std::string::npos ? std::string() : s.substr(b, e - b + 1);
}

// A first suggestion when a property gets bound: the id for the main value of a
// view ("email"), otherwise id_property ("ok_enabled").
std::string suggest_member(View* v, const std::string& prop) {
  if (!is_cpp_identifier(v->id())) return prop;
  if (prop == "text" || prop == "checked" || prop == "value" || prop == "index" || prop == "label") return v->id();
  return v->id() + "_" + prop;
}

std::string suggest_handler(View* v, const std::string& event) {
  const std::string id = is_cpp_identifier(v->id()) ? v->id() : std::string();
  if (id.empty()) return "on_" + event;
  return event == "click" ? "on_" + id : "on_" + id + "_" + event;
}

// Commits a member / method name typed into the inspector.
void commit_binding(Document& doc, View* v, const std::string& key, const std::string& text) {
  const std::string s = trim(text);
  if (!s.empty() && !is_binding_path(s)) {
    doc.status = i18n::Message{"status.bad_binding", s};
    doc.revision = doc.revision.get() + 1; // show the stored value again
    return;
  }
  doc.set_binding(v, key, s);
}

double parse_num(const std::string& s, double fallback) {
  char* end = nullptr;
  const double v = std::strtod(s.c_str(), &end);
  return end && end != s.c_str() ? v : fallback;
}

Node labeled(Bind<std::string> label, Node editor, std::string api_name = {}) {
  return HStack{Text(std::move(label)).dim().width(100).tooltip(std::move(api_name)), std::move(editor)}.spacing(6).align_items(Alignment::Center).padding(Insets(12, 2));
}

Node editor_for(Designer& d, View* v, const meta::Property* p) {
  Document& doc = d.doc;
  // The displayed value follows the document revision (undo, canvas drags...).
  auto value = [&doc, v, p] { return doc.revision.map([v, p](int) { return p->get(*v); }); };
  switch (p->type) {
    case meta::PropType::Bool:
      return Checkbox("", value() | pulse::map([](const meta::Value& x) { return std::holds_alternative<bool>(x) && std::get<bool>(x); }))
          .on_change([&doc, v, p](bool b) { doc.set_prop(v, p, b); });
    case meta::PropType::Enum:
      return Select(p->options, value() | pulse::map([](const meta::Value& x) {
                                  return std::holds_alternative<double>(x) ? static_cast<int>(std::get<double>(x)) : 0;
                                }))
          .on_change([&doc, v, p](int i) { doc.set_prop(v, p, static_cast<double>(i)); })
          .flex();
    case meta::PropType::Number:
      return TextField(value() | pulse::map([](const meta::Value& x) { return meta::to_string(x); }))
          .on_commit([&doc, v, p](const std::string& s) {
            const double cur = std::holds_alternative<double>(p->get(*v)) ? std::get<double>(p->get(*v)) : 0;
            doc.set_prop(v, p, std::clamp(parse_num(s, cur), p->min, p->max));
          })
          .flex();
    case meta::PropType::Color:
      return HStack{
          Spacer(22).radius(5).border(theme().map([](const Theme& t) { return t.border; }))
              .background(value() | pulse::map([](const meta::Value& x) {
                            return std::holds_alternative<Color>(x) ? std::get<Color>(x) : Color::transparent();
                          })),
          TextField(value() | pulse::map([](const meta::Value& x) {
                      return std::holds_alternative<Color>(x) && std::get<Color>(x).a > 0 ? std::get<Color>(x).to_hex() : std::string();
                    }))
              .placeholder("#RRGGBB")
              .on_commit([&doc, v, p](const std::string& s) {
                doc.set_prop(v, p, s.empty() ? Color::transparent() : Color::parse(s).value_or(Color::transparent()));
              })
              .flex(),
      }
          .spacing(6)
          .align_items(Alignment::Center)
          .flex();
    default:
      return TextField(value() | pulse::map([](const meta::Value& x) { return meta::to_string(x); }))
          .on_change([&doc, v, p](const std::string& s) { doc.set_prop(v, p, s); })
          .flex();
  }
}

// The value type a binding expects, from the property's hint:
// "pulse::property<std::string> (two-way)" -> "std::string", "double 0..1" -> "double".
std::string hint_type(const std::string& hint) {
  const auto lt = hint.find("property<");
  if (lt != std::string::npos) {
    const auto start = lt + 9;
    int depth = 1;
    for (std::size_t i = start; i < hint.size(); ++i) {
      if (hint[i] == '<') ++depth;
      else if (hint[i] == '>' && --depth == 0) return hint.substr(start, i - start);
    }
  }
  if (hint.rfind("std::vector", 0) == 0) return "vector<";
  return hint.substr(0, hint.find(' '));
}

// The element type of a container type: "property<std::vector<Todo>>" -> "Todo".
std::string element_type(const std::string& type) {
  const auto v = type.find("vector<");
  if (v == std::string::npos) return {};
  const auto start = v + 7;
  int depth = 1;
  for (std::size_t i = start; i < type.size(); ++i) {
    if (type[i] == '<') ++depth;
    else if (type[i] == '>' && --depth == 0) return trim(type.substr(start, i - start));
  }
  return {};
}

// Fields of a type, as the code analyzer sees them (for item.<field> bindings in list rows).
std::vector<CompletionItem> fields_of(const std::string& type, const CompletionContext& ctx) {
  if (type.empty()) return {};
  const std::vector<std::string> lines{"void pulse_layer_probe() {", "    " + type + " probe_value;", "    probe_value.", "}"};
  return complete_cpp(lines, 2, lines[2].size(), ctx).items;
}

// Members of the form's class for the binding field: what the code analyzer
// finds after `this->` inside a method of the class. Properties get fields
// (those of the expected type first), events get methods.
std::vector<Suggestion> member_suggestions(Designer& d, bool methods, const std::string& typed, const meta::Property* p,
                                           View* view = nullptr) {
  const int fi = d.project.active_form.get();
  if (fi < 0 || fi >= static_cast<int>(d.project.forms.size())) return {};
  const FormEntry& form = d.project.forms[static_cast<std::size_t>(fi)];
  CompletionContext ctx;
  ctx.current_file = form.name + "_binding_probe.cpp";
  for (auto& f : d.project.files) ctx.other_files.emplace_back(f.name, f.text);
  for (auto& f : d.project.forms) ctx.form_classes.push_back(f.cls);
  const std::vector<std::string> lines{"#include \"" + form.name + ".hpp\"", "void " + form.cls + "::pulse_layer_probe() {",
                                       "    this->", "}"};
  const CompletionResult res = complete_cpp(lines, 2, lines[2].size(), ctx);
  const std::string want = p ? hint_type(p->bind_hint) : std::string();
  auto lower = [](std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
  };
  std::string q = lower(trim(typed));
  // the field already holds a member name: offer all of them (that one first)
  std::string current;
  for (const CompletionItem& it : res.items)
    if (lower(it.label) == q) current = it.label;
  if (!current.empty()) q.clear();
  std::vector<Suggestion> typed_match, others;
  for (const CompletionItem& it : res.items) {
    if (it.kind != CompletionItem::Member && it.kind != CompletionItem::Variable && it.kind != CompletionItem::Function) continue;
    if (it.label == form.cls || it.label == "ui_" || it.label == "window_" || it.label == "pulse_layer_probe") continue;
    const bool is_method = !it.detail.empty() && it.detail[0] == '(';
    if (is_method != methods) continue;
    if (methods && (it.label == "show" || it.label == "window")) continue; // the form's own plumbing
    const std::string l = lower(it.label);
    if (!q.empty() && l.find(q) == std::string::npos) continue;
    Suggestion s{methods ? it.label + "()" : it.label, it.detail, it.label};
    const bool prefix = q.empty() || l.rfind(q, 0) == 0;
    const bool type_ok = !want.empty() && it.detail.find(want) != std::string::npos;
    if (it.label == current) typed_match.insert(typed_match.begin(), std::move(s));
    else (prefix && (methods || want.empty() || type_ok) ? typed_match : others).push_back(std::move(s));
  }
  // inside a list row: the fields of the row's item ("item.title")
  if (!methods && view) {
    for (View* a = view->parent(); a; a = a->parent()) {
      auto* list = dynamic_cast<ForEachTemplate*>(a);
      if (!list || (p && p->name == "items" && a == view)) continue;
      const std::string* items = list->annotation("bind.items");
      if (!items) break;
      std::string container_type;
      for (const CompletionItem& it : res.items)
        if (it.label == *items) container_type = it.detail;
      std::vector<Suggestion> fields, other_fields;
      for (const CompletionItem& f : fields_of(element_type(container_type), ctx)) {
        if (!f.detail.empty() && f.detail[0] == '(') continue; // fields only
        const std::string path = list->item_name + "." + f.label;
        if (!q.empty() && lower(path).find(q) == std::string::npos && lower(f.label).find(q) == std::string::npos) continue;
        // fields of the expected type first ("string" for a text)
        const bool fits = want.empty() || f.detail.find(want) != std::string::npos ||
                          (want == "std::string" && f.detail.find("string") != std::string::npos);
        (fits ? fields : other_fields).push_back({path, f.detail, path});
      }
      fields.insert(fields.end(), other_fields.begin(), other_fields.end());
      typed_match.insert(typed_match.begin(), fields.begin(), fields.end());
      break;
    }
  }
  typed_match.insert(typed_match.end(), others.begin(), others.end());
  return typed_match;
}

// A bindable property: its normal editor, or - once linked - the member it is bound to.
Node bindable_editor(Designer& d, View* v, const meta::Property* p) {
  Document& doc = d.doc;
  const std::string key = "bind." + p->name;
  auto bound = [&doc, v, key] { return doc.revision.map([v, key](int) { return v->annotation(key) != nullptr; }); };
  auto target = doc.revision.map([v, key](int) {
    const std::string* a = v->annotation(key);
    return a ? *a : std::string();
  });
  return HStack{
      Show(bound(),
           HStack{
               // "MainWindow::" - reads as C++: the member of the form's class
               Text(d.project.active_form.map([&d](int i) {
                 const auto& forms = d.project.forms;
                 return (i >= 0 && i < static_cast<int>(forms.size()) ? forms[static_cast<std::size_t>(i)].cls : std::string("Form")) + "::";
               }))
                   .mono()
                   .font_size(11.5f)
                   .color(theme().map([](const Theme& t) { return t.accent; }))
                   .max_width(110)
                   .id("bind-class-" + p->name),
               TextField(target)
                   .placeholder(tr("inspector.member"))
                   .tooltip(tr("inspector.bound.tip", p->bind_hint))
                   .suggestions([&d, p, v](const std::string& t) { return member_suggestions(d, false, t, p, v); })
                   .on_commit([&doc, v, key](const std::string& s) { commit_binding(doc, v, key, s); })
                   .id("bind-" + p->name)
                   .flex(),
           }
               .spacing(6)
               .align_items(Alignment::Center),
           editor_for(d, v, p))
          .flex(),
      // a chain icon: accent when bound, dim when not; click toggles the binding
      Canvas([v, key](Painter& p, const Rect& r) {
        draw_chain(p, r.center(), v->annotation(key) ? th().accent : th().text_dim);
      })
          .redraw_on(bound())
          .on_click([&doc, v, key, p] {
            doc.set_binding(v, key, v->annotation(key) ? std::string() : suggest_member(v, p->name));
          })
          .tooltip(tr("inspector.bind.tip"))
          .id("link-" + p->name)
          .radius(5)
          .size(26, 26),
  }
      .spacing(4)
      .align_items(Alignment::Center)
      .flex();
}

Node event_editor(Designer& d, View* v, const std::string& event) {
  Document& doc = d.doc;
  const std::string key = "on." + event;
  auto target = doc.revision.map([v, key](int) {
    const std::string* a = v->annotation(key);
    return a ? *a : std::string();
  });
  return HStack{
      TextField(target)
          .placeholder(tr("inspector.method"))
          .suggestions([&d](const std::string& t) { return member_suggestions(d, true, t, nullptr); })
          .on_commit([&doc, v, key](const std::string& s) { commit_binding(doc, v, key, s); })
          .id("event-" + event)
          .flex(),
      Button("+")
          .flat()
          .tooltip(tr("inspector.handler.tip"))
          .on_click([&doc, v, key, event] {
            if (!v->annotation(key)) doc.set_binding(v, key, suggest_handler(v, event));
          })
          .width(30),
  }
      .spacing(4)
      .align_items(Alignment::Center)
      .flex();
}

void write_file(const std::string& path, const std::string& text) {
  std::ofstream out(path, std::ios::binary);
  out << text;
}

} // namespace

// ── commands ─────────────────────────────────────────────────────────────────

std::vector<dialogs::Filter> form_filter() {
  return {{i18n::text("filter.forms"), "*.pform"}, {i18n::text("filter.all"), "*.*"}};
}

std::string Designer::export_file_name() {
  const int i = project.active_form.get();
  if (i >= 0 && i < static_cast<int>(project.forms.size())) return project.forms[static_cast<std::size_t>(i)].name + "_form.hpp";
  return "form.hpp";
}

void Designer::cmd_export() {
  auto f = dialogs::save_file(window, i18n::text("dlg.export"), {{i18n::text("filter.cpp"), "*.hpp"}}, export_file_name());
  if (!f) return;
  write_file(*f, doc.export_cpp());
  doc.status = i18n::Message{"status.exported", *f};
}

void Designer::cmd_preview() {
  auto j = json::Value::parse(doc.to_json_text());
  if (!j) return;
  auto form = form_from_json(*j);
  if (!form) return;
  auto w = std::make_unique<Window>(i18n::text("preview.title", form->title), form->width, form->height);
  w->content(std::move(form->root));
  Window* raw = w.get();
  w->closed.connect([this, raw] {
    App::post([this, raw] {
      std::erase_if(previews, [raw](const std::unique_ptr<Window>& p) { return p.get() == raw; });
    });
  });
  w->show();
  previews.push_back(std::move(w));
  doc.status = i18n::Message{"status.preview", ""};
}

void Designer::cmd_settings(int tab) {
  if (settings_window && settings_window->is_open()) {
    if (auto* tabs = settings_window->find<Tabs>("settings-tabs")) tabs->set_current(tab);
    settings_window->show();
    return;
  }
  std::vector<std::string> names;
  for (auto& l : i18n::languages()) names.push_back(l.name);

  auto row = [](const char* key, Node editor) {
    return HStack{Text(tr(key)).width(110), std::move(editor)}.spacing(12).align_items(Alignment::Center);
  };
  // Option lists are streams too, so they re-translate with the rest.
  auto theme_names = i18n::current().map([](const std::string&) {
    return std::vector<std::string>{i18n::text("theme.dark"), i18n::text("theme.light")};
  });

  Tabs tabs{
      Tab(tr("settings.general"),
          VStack{
              row("settings.language", Select(names, language).id("settings-language").flex()),
              Text(tr("settings.language.hint")).dim().wrap().font_size(11.5f).padding(Insets(122, 0, 0, 0)),
              row("settings.theme", Select(theme_names, theme).id("settings-theme").flex()),
          }
              .spacing(12)
              .padding(Insets(4, 18))),
      Tab(tr("settings.kits"), kits_settings(*this)),
      Tab(tr("settings.about"),
          VStack{
              Text("Pulse Layer").font_size(26).weight(FontWeight::Bold).color(pulseui::theme().map([](const Theme& t) { return t.accent; })),
              Text(tr("about.tagline")).dim(),
              Text(tr("about.version", PULSELAYER_VERSION)).weight(FontWeight::SemiBold),
              Text(tr("about.built", std::string(pulse::version::string) + "" + PULSEUI_VERSION_STRING)).dim(),
              Text(tr("about.text")).wrap(),
              Divider(),
              Text(tr("about.license")).dim().font_size(11.5f),
              Text(tr("about.settings_file", Settings::file().string())).dim().wrap().font_size(11.5f),
          }
              .spacing(10)
              .padding(Insets(4, 18))),
  };
  tabs.id("settings-tabs").flex();
  tabs.set_current(tab, false);

  settings_window = std::make_unique<Window>(tr("settings.title"), 700, 580);
  settings_window->set_min_size(520, 420);
  settings_window->content(VStack{
      std::move(tabs),
      HStack{Spacer(), Button(tr("settings.close"), [this] { settings_window->close(); }).primary()},
  }
                               .spacing(12)
                               .padding(16));
  Window* raw = settings_window.get();
  settings_window->shortcut("Escape", [raw] { raw->close(); });
  settings_window->closed.connect([this, raw] {
    App::post([this, raw] {
      if (settings_window.get() == raw) settings_window.reset();
    });
  });
  settings_window->show();
}

void Designer::cmd_delete() { doc.remove(doc.selection.get()); }
void Designer::cmd_duplicate() { doc.duplicate(doc.selection.get()); }

// ── panels ───────────────────────────────────────────────────────────────────

Node toolbar(Designer& d) {
  // Labels and tooltips are translation streams: they follow the language.
  // compact icon buttons; the tooltip carries the translated name + shortcut
  auto icon = [](const char* glyph, const char* key, std::function<void()> fn) {
    return Button(glyph, std::move(fn)).flat().tooltip(tr(std::string(key) + ".tip")).padding(Insets(10, 0));
  };
  auto tool = [](const char* key, std::function<void()> fn) {
    return Button(tr(key), std::move(fn)).flat().tooltip(tr(std::string(key) + ".tip"));
  };
  return HStack{
      Text("Pulse Layer").weight(FontWeight::Bold).color(theme().map([](const Theme& t) { return t.accent; })).padding(Insets(6, 0)),
      Segmented(i18n::current().map([](const std::string&) {
                  return std::vector<std::string>{i18n::text("mode.design"), i18n::text("mode.code")};
                }),
                d.mode)
          .id("mode"),
      Divider(Orientation::Vertical).height(22),
      tool("tb.new", [&d] { d.cmd_new(); }),
      tool("tb.open", [&d] { d.cmd_open(); }),
      tool("tb.save", [&d] { d.cmd_save(false); }),
      HStack{
          Divider(Orientation::Vertical).height(22),
          Select(d.project.revision.map([&d](int) {
                   std::vector<std::string> names;
                   for (auto& f : d.project.forms) names.push_back(f.cls);
                   return names;
                 }),
                 d.project.active_form)
              .id("form-select")
              .width(150)
              .tooltip(tr("tb.form.tip")),
          icon("+", "tree.add_form", [&d] { d.cmd_add_form(); }),
          Divider(Orientation::Vertical).height(22),
          icon("↶", "tb.undo", [&d] { d.doc.undo(); }),
          icon("↷", "tb.redo", [&d] { d.doc.redo(); }),
          Divider(Orientation::Vertical).height(22),
          icon("⧉", "tb.duplicate", [&d] { d.cmd_duplicate(); }),
          icon("✕", "tb.delete", [&d] { d.cmd_delete(); }),
      }
          .spacing(4)
          .align_items(Alignment::Center)
          .visible(d.mode.map([](int m) { return m == 0; })),
      Spacer(),
      Button(tr("tb.settings"), [&d] { d.cmd_settings(); }).flat().id("settings").tooltip(tr("tb.settings.tip")),
      Button(tr("tb.export"), [&d] { d.cmd_export(); }).id("export").tooltip(tr("tb.export.tip"))
          .visible(d.mode.map([](int m) { return m == 0; })),
      Button(tr("tb.board"), [&d] { d.cmd_preview_board(); }).id("board").tooltip(tr("tb.board.tip"))
          .visible(d.mode.map([](int m) { return m == 0; })),
      Button(tr("tb.preview"), [&d] { d.cmd_preview(); }).primary().tooltip(tr("tb.preview.tip"))
          .visible(d.mode.map([](int m) { return m == 0; })),
  }
      .spacing(4)
      .align_items(Alignment::Center)
      .padding(Insets(10, 6))
      .background(surface());
}

Node palette(Designer& d) {
  VStack list;
  list.spacing(0);
  // one section per category, in the order the categories first appear
  std::vector<std::string> categories;
  for (auto& t : meta::types())
    if (std::find(categories.begin(), categories.end(), t.category) == categories.end()) categories.push_back(t.category);
  for (const std::string& cat : categories) {
    list.add(section_title(tr("palette.cat." + cat, {}, cat)));
    for (auto& t : meta::types())
      if (t.category == cat) list.add(PaletteItem(d, t));
    if (cat == "Project") {
      // the other forms of the project (as components) and its own view classes
      auto parts = std::make_shared<pulse::computed<std::vector<std::string>>>([&d] { return project_parts(d); },
                                                                               d.project.revision, d.project.active_form, d.mode);
      d.palette_parts = parts;
      list.add(ForEach(*parts, [](const std::string& s) { return s; }, [&d](const std::string& spec) {
                 const bool form = spec.rfind("Component:", 0) == 0;
                 const std::string name = spec.substr(spec.find(':') + 1, spec.find('|') == std::string::npos ? std::string::npos
                                                                                                         : spec.find('|') - spec.find(':') - 1);
                 return Node(PaletteItem(d, spec, name, form ? "\xE2\x96\xA3" : "\xE2\x97\x87",
                                         form ? i18n::text("palette.form.tip", name) : i18n::text("palette.view.tip", name)));
               }).spacing(0));
    }
  }
  list.add(Text(tr("palette.tip")).font_size(11).dim().wrap().padding(Insets(12, 14)));
  return VStack{section_title(tr("panel.palette")), Scroll(std::move(list)).flex()}.spacing(0).background(surface());
}

Node hierarchy(Designer& d) {
  Document& doc = d.doc;
  auto rows = std::make_shared<pulse::computed<std::vector<TreeRow>>>(
      [&doc] {
        std::vector<TreeRow> out;
        if (doc.root()) flatten(doc.root(), 0, out);
        return out;
      },
      doc.revision);
  auto list = ForEach(rows->as_observable(), [&doc](const TreeRow& r) {
    View* v = r.view;
    const meta::TypeInfo* t = meta::find(v->type_name());
    return HStack{
        Spacer().flex(0).width(static_cast<float>(r.depth) * 14),
        Text(t ? t->glyph : "·").color(theme().map([](const Theme& th) { return th.accent; })).width(16),
        Text(v->type_name()).weight(FontWeight::Medium),
        // the id changes without restructuring the tree: follow the document
        Text(doc.revision.map([v](int) { return v->id().empty() ? std::string() : "#" + v->id(); })).dim().id("tree-id"),
        // bound to code: a small chain after the id
        Canvas([](Painter& p, const Rect& r) { draw_chain(p, r.center(), th().accent, 0.8f); })
            .size(18, 14)
            .visible(doc.revision.map([v](int) { return has_bindings(v); }))
            .tooltip(tr("tree.bound.tip"))
            .id("tree-bound"),
    }
        .spacing(6)
        .align_items(Alignment::Center)
        .padding(Insets(10, 3))
        .radius(4)
        .background(doc.selection.map([v](View* s) { return s == v ? th().selection : Color::transparent(); }))
        .on_click([&doc, v] { doc.selection = v; });
  });
  list.spacing(1);
  // keep the computed alive as long as the panel
  return VStack{section_title(tr("panel.hierarchy")), Scroll(std::move(list).padding(Insets(4, 0))).flex()}
      .spacing(0)
      .on_mount_fn([rows](VStack&) {});
}

Node inspector(Designer& d) {
  Document& doc = d.doc;
  auto rows = std::make_shared<pulse::computed<std::vector<PropRow>>>(
      [&doc] {
        std::vector<PropRow> out;
        View* v = doc.selection.get();
        if (!v) return out;
        if (v == doc.root()) {
          out.push_back({PropRow::Header, nullptr, nullptr, "inspector.form", "FORM"});
          out.push_back({PropRow::FormTitle, nullptr, nullptr, "form.title", "title"});
          out.push_back({PropRow::FormWidth, nullptr, nullptr, "form.width", "width"});
          out.push_back({PropRow::FormHeight, nullptr, nullptr, "form.height", "height"});
        }
        std::string group;
        for (const meta::Property* p : meta::properties(*v)) {
          if (p->group != group) {
            group = p->group;
            std::string up = group;
            for (auto& c : up) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            out.push_back({PropRow::Header, nullptr, nullptr, "group." + group, up});
          }
          out.push_back({PropRow::Prop, v, p, p->name, p->name});
        }
        if (!meta::events(*v).empty()) {
          out.push_back({PropRow::Header, nullptr, nullptr, "group.Events", "EVENTS"});
          for (const meta::Event& e : meta::events(*v)) out.push_back({PropRow::Event, v, nullptr, e.name, e.name});
        }
        return out;
      },
      doc.selection);

  auto list = ForEach(rows->as_observable(), [&d](const PropRow& r) -> Node {
    Document& doc = d.doc;
    switch (r.kind) {
      case PropRow::Header: return section_title(tr(r.label, {}, r.fallback));
      case PropRow::FormTitle:
        return labeled(tr(r.label, {}, r.fallback), TextField(doc.title).on_change([&doc](const std::string& s) { doc.set_form_title(s); }).flex());
      case PropRow::FormWidth:
        return labeled(tr(r.label, {}, r.fallback), TextField(doc.width.map([](double w) { return std::to_string(static_cast<int>(w)); }))
                                    .on_commit([&doc](const std::string& s) { doc.set_form_size(parse_num(s, doc.width.get()), doc.height.get()); })
                                    .flex());
      case PropRow::FormHeight:
        return labeled(tr(r.label, {}, r.fallback), TextField(doc.height.map([](double h) { return std::to_string(static_cast<int>(h)); }))
                                     .on_commit([&doc](const std::string& s) { doc.set_form_size(doc.width.get(), parse_num(s, doc.height.get())); })
                                     .flex());
      case PropRow::Event:
        return labeled(tr("event." + r.label, {}, r.fallback), event_editor(d, r.view, r.label), "on " + r.label);
      default:
        return labeled(tr("prop." + r.label, {}, r.fallback),
                       r.prop->bind ? bindable_editor(d, r.view, r.prop) : editor_for(d, r.view, r.prop), r.label);
    }
  });
  list.spacing(2);
  return VStack{
      HStack{section_title(tr("panel.properties")), Spacer(),
             Text(pulse::combine_latest(i18n::current().as_observable(), doc.selection.as_observable(),
                                        [](const std::string&, View* v) {
                                          return v ? std::string(v->type_name()) : i18n::text("inspector.nothing");
                                        }))
                 .dim()
                 .font_size(11)
                 .padding(Insets(12, 10, 12, 4))},
      Scroll(std::move(list).padding(Insets(0, 0, 0, 12))).flex(),
  }
      .spacing(0)
      .background(surface())
      .on_mount_fn([rows](VStack&) {});
}

Node status_bar(Designer& d) {
  Document& doc = d.doc;
  return HStack{
      Text(tr(doc.status)).dim().font_size(11.5f).flex(),
      Text(doc.revision.map([&doc](int) {
        View* v = doc.selection.get();
        if (!v) return std::string();
        return std::string(v->type_name()) + (v->id().empty() ? "" : " #" + v->id()) + "  ·  " +
               std::to_string(static_cast<int>(v->frame().w)) + " × " + std::to_string(static_cast<int>(v->frame().h));
      }))
          .dim()
          .font_size(11.5f),
  }
      .padding(Insets(12, 5))
      .background(surface());
}

} // namespace layer
