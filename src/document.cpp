#include "document.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <functional>
#include <sstream>

namespace layer {

namespace {
constexpr std::size_t kMaxUndo = 200;

void collect_ids(const View* v, std::vector<std::string>& out) {
  if (!v->id().empty()) out.push_back(v->id());
  for (auto& c : v->children()) collect_ids(c.get(), out);
}
} // namespace

void Document::set_host(View& host) {
  host_ = &host;
  new_form();
}

// ── helpers ──────────────────────────────────────────────────────────────────

bool Document::is_container(const View* v) {
  const meta::TypeInfo* t = v ? meta::find(v->type_name()) : nullptr;
  return t && t->container;
}

bool Document::is_layer(const View* v) { return v && std::string(v->type_name()) == "Layer"; }

bool Document::is_descendant(const View* v, const View* ancestor) const {
  for (const View* x = v; x; x = x->parent())
    if (x == ancestor) return true;
  return false;
}

std::vector<int> Document::path_of(const View* v) const {
  std::vector<int> p;
  for (const View* x = v; x && x != root_; x = x->parent()) {
    if (!x->parent()) return {};
    p.push_back(x->parent()->index_of(x));
  }
  std::reverse(p.begin(), p.end());
  return p;
}

View* Document::at_path(const std::vector<int>& p) const {
  View* v = root_;
  for (int i : p) {
    if (!v || i < 0 || i >= static_cast<int>(v->children().size())) return root_;
    v = v->children()[static_cast<std::size_t>(i)].get();
  }
  return v;
}

std::string Document::unique_id(const std::string& type) const {
  std::vector<std::string> ids;
  if (root_) collect_ids(root_, ids);
  std::string base = type;
  for (auto& c : base) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  for (int n = 1;; ++n) {
    std::string id = base + std::to_string(n);
    if (std::find(ids.begin(), ids.end(), id) == ids.end()) return id;
  }
}

// ── undo ─────────────────────────────────────────────────────────────────────

Document::Snapshot Document::snapshot() const {
  return {root_ ? view_to_json(*root_).dump(0) : std::string(), path_of(selection.get()), title.get(), width.get(),
          height.get()};
}

void Document::restore(const Snapshot& s) {
  selection = nullptr;
  if (auto j = json::Value::parse(s.json)) set_root(view_from_json(*j));
  title = s.title;
  width = s.width;
  height = s.height;
  selection = at_path(s.selection);
  dirty = true;
  revision = revision.get() + 1;
}

void Document::begin_edit(const std::string& merge_key) {
  const auto now = std::chrono::steady_clock::now();
  if (!merge_key.empty() && merge_key == last_key_ && now - last_edit_ < std::chrono::milliseconds(1500)) {
    last_edit_ = now;
    return;
  }
  last_key_ = merge_key;
  last_edit_ = now;
  undo_.push_back(snapshot());
  if (undo_.size() > kMaxUndo) undo_.erase(undo_.begin());
  redo_.clear();
}

void Document::changed() {
  dirty = true;
  revision = revision.get() + 1;
}

void Document::undo() {
  if (undo_.empty()) return;
  redo_.push_back(snapshot());
  Snapshot s = std::move(undo_.back());
  undo_.pop_back();
  last_key_.clear();
  restore(s);
  status = i18n::Message{"status.undo", ""};
}

void Document::redo() {
  if (redo_.empty()) return;
  undo_.push_back(snapshot());
  Snapshot s = std::move(redo_.back());
  redo_.pop_back();
  last_key_.clear();
  restore(s);
  status = i18n::Message{"status.redo", ""};
}

// ── edits ────────────────────────────────────────────────────────────────────

void Document::set_root(Node n) {
  selection = nullptr;
  if (!host_) return;
  host_->clear_children();
  root_ = n ? &host_->add(std::move(n)) : nullptr;
}

void Document::set_binding(View* v, const std::string& key, const std::string& value) {
  if (!v) return;
  const std::string* cur = v->annotation(key);
  if ((cur ? *cur : std::string()) == value) return;
  begin_edit(key + "@" + std::to_string(reinterpret_cast<std::uintptr_t>(v)));
  v->set_annotation(key, value);
  changed();
}

void Document::set_prop(View* v, const meta::Property* p, const meta::Value& value) {
  if (!v || !p || !p->set) return;
  if (p->get && meta::equal(p->get(*v), value)) return;
  begin_edit(p->name + "@" + std::to_string(reinterpret_cast<std::uintptr_t>(v)));
  p->set(*v, value);
  changed();
}

View* Document::insert(const std::string& type, View* container, int index, std::optional<Point> pos) {
  if (!container || !is_container(container)) return nullptr;
  auto v = meta::create(type);
  if (!v) return nullptr;
  begin_edit();
  v->set_id(unique_id(type));
  if (is_layer(container)) {
    v->style().positioned = true;
    v->style().position = pos.value_or(Point{16.f + 12.f * container->children().size(), 16.f + 12.f * container->children().size()});
  }
  if (index < 0) index = static_cast<int>(container->children().size());
  View& added = container->insert(static_cast<std::size_t>(index), Node(std::move(v)));
  selection = &added;
  changed();
  status = i18n::Message{"status.added", type};
  return &added;
}

View* Document::insert_near_selection(const std::string& type) {
  View* sel = selection.get() ? selection.get() : root_;
  if (!sel) return nullptr;
  if (std::string(sel->type_name()) == "Scroll" && !sel->children().empty()) sel = sel->children()[0].get();
  if (is_container(sel) && !(std::string(sel->type_name()) == "Scroll" && !sel->children().empty()))
    return insert(type, sel, -1);
  View* parent = sel->parent();
  if (!parent || sel == root_) return nullptr;
  return insert(type, parent, parent->index_of(sel) + 1);
}

void Document::remove(View* v) {
  if (!v || v == root_ || !v->parent()) return;
  begin_edit();
  View* parent = v->parent();
  selection = parent; // drop bindings to `v` before it dies
  parent->remove(v);
  changed();
  status = i18n::Message{"status.deleted", ""};
}

View* Document::duplicate(View* v) {
  if (!v || v == root_ || !v->parent()) return nullptr;
  json::Value j = view_to_json(*v);
  Node n = view_from_json(j);
  if (!n) return nullptr;
  begin_edit();
  std::function<void(View*)> renumber = [&](View* x) {
    if (!x->id().empty()) x->set_id(unique_id(x->type_name()));
    for (auto& c : x->children()) renumber(c.get());
  };
  View* parent = v->parent();
  if (is_layer(parent)) n->style().position = n->style().position + Point{12, 12};
  View& added = parent->insert(static_cast<std::size_t>(parent->index_of(v) + 1), std::move(n));
  added.set_id(""); // ids must be unique: renumber the copy
  renumber(&added);
  if (added.id().empty()) added.set_id(unique_id(added.type_name()));
  selection = &added;
  changed();
  status = i18n::Message{"status.duplicated", ""};
  return &added;
}

void Document::move(View* v, View* new_parent, int index) {
  if (!v || v == root_ || !new_parent || is_descendant(new_parent, v) || !is_container(new_parent)) return;
  View* old_parent = v->parent();
  if (!old_parent) return;
  const int old_index = old_parent->index_of(v);
  if (old_parent == new_parent && (index == old_index || index == old_index + 1)) return;
  begin_edit();
  if (old_parent == new_parent && index > old_index) --index;
  auto owned = old_parent->take(v);
  if (!is_layer(new_parent)) owned->style().positioned = false;
  else if (!owned->style().positioned) {
    owned->style().positioned = true;
    owned->style().position = {16, 16};
  }
  View& placed = new_parent->insert(static_cast<std::size_t>(std::max(0, index)), Node(std::move(owned)));
  selection = &placed;
  changed();
  status = i18n::Message{"status.moved", ""};
}

void Document::copy(View* v) {
  if (!v) return;
  clipboard_ = view_to_json(*v).dump(2);
  App::set_clipboard(clipboard_);
  status = i18n::Message{"status.copied", v->type_name()};
}

void Document::paste() {
  std::string text = App::clipboard();
  auto j = json::Value::parse(text);
  if (!j || !(*j)["type"].is_string()) j = json::Value::parse(clipboard_);
  if (!j) return;
  Node n = view_from_json(*j);
  if (!n) return;
  View* sel = selection.get() ? selection.get() : root_;
  View* container = is_container(sel) ? sel : sel->parent();
  if (!container) return;
  begin_edit();
  std::function<void(View*)> renumber = [&](View* x) {
    x->set_id("");
    for (auto& c : x->children()) renumber(c.get());
  };
  renumber(n.get());
  n->set_id(unique_id(n->type_name()));
  const int index = container == sel ? static_cast<int>(container->children().size()) : container->index_of(sel) + 1;
  View& added = container->insert(static_cast<std::size_t>(index), std::move(n));
  if (!is_layer(container)) added.style().positioned = false;
  selection = &added;
  changed();
  status = i18n::Message{"status.pasted", ""};
}

void Document::set_form_size(double w, double h) {
  w = std::clamp(w, 120.0, 4000.0);
  h = std::clamp(h, 80.0, 4000.0);
  if (w == width.get() && h == height.get()) return;
  begin_edit("form-size");
  width = w;
  height = h;
  changed();
}

void Document::set_form_title(const std::string& t) {
  if (t == title.get()) return;
  begin_edit("form-title");
  title = t;
  changed();
}

// ── files ────────────────────────────────────────────────────────────────────

void Document::new_form() {
  undo_.clear();
  redo_.clear();
  set_root(VStack{
      Text(i18n::text("doc.hello")).title().id("title"),
      Text(i18n::text("doc.hint"))
          .wrap()
          .id("subtitle"),
      TextField().placeholder(i18n::text("doc.name")).id("name"),
      HStack{Spacer(), Button(i18n::text("doc.cancel")).id("cancel"), Button(i18n::text("doc.ok")).primary().id("ok")},
  }
               .spacing(12)
               .padding(20));
  // example bindings: the name field edits `name`, OK calls `greet()` (see the project templates)
  if (View* v = root_->find("name")) v->set_annotation("bind.text", "name");
  if (View* v = root_->find("ok")) v->set_annotation("on.click", "greet");
  title = i18n::text("doc.untitled");
  width = 480;
  height = 300;
  path = "";
  dirty = false;
  selection = root_;
  revision = revision.get() + 1;
  status = i18n::Message{"status.new", ""};
}

bool Document::open(const std::string& file) {
  std::string err;
  std::optional<Form> f = load_form(file, &err);
  if (!f) {
    status = i18n::Message{"status.open_failed", err};
    return false;
  }
  undo_.clear();
  redo_.clear();
  set_root(std::move(f->root));
  title = f->title;
  width = f->width;
  height = f->height;
  path = file;
  dirty = false;
  selection = root_;
  revision = revision.get() + 1;
  status = i18n::Message{"status.opened", file};
  return true;
}

bool Document::save(const std::string& file) {
  bool ok = false;
  if (root_) {
    std::ofstream out(file, std::ios::binary);
    out << to_json_text() << "\n";
    ok = static_cast<bool>(out);
  }
  if (!ok) {
    status = i18n::Message{"status.save_failed", file};
    return false;
  }
  path = file;
  dirty = false;
  status = i18n::Message{"status.saved", file};
  return true;
}

std::string Document::to_json_text() {
  if (!root_) return {};
  return form_to_json(title.get(), static_cast<float>(width.get()), static_cast<float>(height.get()), *root_).dump(2);
}

std::string Document::form_json() { return to_json_text(); }

bool Document::load_json(const std::string& text) {
  auto j = json::Value::parse(text);
  if (!j) return false;
  auto f = form_from_json(*j);
  if (!f) return false;
  undo_.clear();
  redo_.clear();
  last_key_.clear();
  set_root(std::move(f->root));
  title = f->title;
  width = f->width;
  height = f->height;
  dirty = false;
  selection = root_;
  revision = revision.get() + 1;
  return true;
}

std::string Document::export_cpp() {
  std::string fn = "make_";
  for (char c : title.get()) fn += std::isalnum(static_cast<unsigned char>(c)) ? static_cast<char>(std::tolower(static_cast<unsigned char>(c))) : '_';
  return root_ ? generate_cpp(*root_, fn) : std::string();
}

} // namespace layer
