// Project: commands (new / open / save, add / remove forms and files), switching
// the edited form and file, the project tree of Code mode and the name dialog.
#include <filesystem>

#include "panels.hpp"

namespace fs = std::filesystem;

namespace layer {

using i18n::tr;

// ── switching forms and files ────────────────────────────────────────────────

void Designer::sync_form() {
  if (loaded_form < 0 || loaded_form >= static_cast<int>(project.forms.size())) return;
  auto& f = project.forms[static_cast<std::size_t>(loaded_form)];
  const std::string json = doc.form_json();
  if (!json.empty() && json != f.json) {
    f.json = json;
    project.dirty = true;
  }
}

void Designer::load_form(int index) {
  if (index < 0 || index >= static_cast<int>(project.forms.size())) return;
  loaded_form = index;
  doc.load_json(project.forms[static_cast<std::size_t>(index)].json);
  if (project.active_form.get() != index) project.active_form = index;
}

void Designer::open_file(const std::string& name) {
  if (const SourceFile* f = project.find_file(name)) {
    loading_text = true;
    editor_text = f->text;
    loading_text = false;
    if (project.active_file.get() != name) project.active_file = name;
  }
}

void Designer::reload_project_views() {
  load_form(0);
  open_file(project.active_file.get());
  project.revision = project.revision.get() + 1;
}

void Designer::wire_project() {
  // the form shown in Design mode
  project.active_form.changed.connect([this](int i) {
    if (i == loaded_form) return;
    sync_form();
    load_form(i);
  });
  // the file shown in the editor
  project.active_file.changed.connect([this](const std::string& name) { open_file(name); });
  editor_text.changed.connect([this](const std::string& text) {
    if (loading_text) return;
    if (SourceFile* f = project.find_file(project.active_file.get()); f && f->text != text) {
      f->text = text;
      project.dirty = true;
    }
  });
  doc.revision.changed.connect([this](int) {
    if (doc.dirty.get()) project.dirty = true;
  });
}

// ── commands ─────────────────────────────────────────────────────────────────

namespace {
std::vector<dialogs::Filter> project_filter() {
  return {{i18n::text("filter.projects"), "*.pulse;*.pform"}, {i18n::text("filter.all"), "*.*"}};
}
} // namespace

void Designer::cmd_new() {
  doc.new_form();
  project.create_new(doc.form_json());
  loaded_form = 0;
  open_file("main.cpp");
  doc.status = i18n::Message{"status.new_project", ""};
}

void Designer::cmd_open() {
  if (auto f = dialogs::open_file(window, i18n::text("dlg.open"), project_filter())) open_project(*f);
}

bool Designer::open_project(const std::string& path) {
  std::string err;
  if (!project.load(path, &err)) {
    doc.status = i18n::Message{"status.open_failed", err};
    return false;
  }
  loaded_form = -1;
  reload_project_views();
  doc.status = i18n::Message{"status.opened", path};
  return true;
}

bool Designer::save_project(const std::string& path) {
  sync_form();
  std::string err;
  if (!project.save(path, &err)) {
    doc.status = i18n::Message{"status.save_failed", err};
    return false;
  }
  doc.dirty = false;
  doc.status = i18n::Message{"status.saved", path};
  return true;
}

void Designer::cmd_save(bool save_as) {
  std::string path = project.file.get();
  if (save_as || path.empty()) {
    auto f = dialogs::save_file(window, i18n::text("dlg.save_project"), {{i18n::text("filter.projects"), "*.pulse"}},
                                project.name.get() + ".pulse");
    if (!f) return;
    path = *f;
  }
  save_project(path);
}

bool Designer::add_form(const std::string& cls, std::string* error) {
  sync_form();
  // a new form starts as a small panel with a title
  VStack root{Text(cls).title().id("title")};
  root.spacing(12).padding(20);
  const std::string json = form_to_json(cls, 420, 300, root).dump(2);
  if (!project.add_form(cls, json, error)) return false;
  project.active_form = static_cast<int>(project.forms.size()) - 1;
  doc.status = i18n::Message{"status.form_added", cls};
  return true;
}

bool Designer::add_file(const std::string& name, std::string* error) {
  if (!project.add_file(name, error)) return false;
  project.active_file = name;
  mode = 1;
  doc.status = i18n::Message{"status.file_added", name};
  return true;
}

void Designer::remove_form(int index) {
  if (index < 0 || index >= static_cast<int>(project.forms.size()) || project.forms.size() <= 1) return;
  const std::string name = project.forms[static_cast<std::size_t>(index)].name;
  sync_form();
  if (!project.remove_form(index)) return;
  loaded_form = -1;
  load_form(std::min(project.active_form.get(), static_cast<int>(project.forms.size()) - 1));
  open_file(project.active_file.get());
  doc.status = i18n::Message{"status.removed", name + ".pform"};
}

void Designer::remove_file(const std::string& name) {
  if (!project.remove_file(name)) return;
  open_file(project.active_file.get());
  doc.status = i18n::Message{"status.removed", name};
}

// ── name dialog ──────────────────────────────────────────────────────────────

void Designer::ask_name(const std::string& title_key, const std::string& hint_key, const std::string& initial,
                        std::function<bool(const std::string&, std::string*)> accept) {
  if (dialog_window && dialog_window->is_open()) dialog_window->close();
  auto value = std::make_shared<pulse::property<std::string>>(initial);
  auto error = std::make_shared<pulse::property<std::string>>();
  dialog_window = std::make_unique<Window>(tr(title_key), 420, 180);
  Window* raw = dialog_window.get();
  auto ok = [this, raw, value, error, accept] {
    std::string err;
    if (accept(value->get(), &err)) {
      raw->close();
      return;
    }
    *error = i18n::text("dialog.error." + err, value->get(), i18n::text("dialog.error.invalid", value->get()));
  };
  dialog_window->content(VStack{
      Text(tr(hint_key)).dim().wrap(),
      TextField(*value).id("dialog-name").on_submit([ok](const std::string&) { ok(); }),
      Text(*error).color(pulseui::theme().map([](const Theme& t) { return t.danger; })).font_size(11.5f),
      Spacer(),
      HStack{Spacer(), Button(tr("dialog.cancel"), [raw] { raw->close(); }),
             Button(tr("dialog.ok"), ok).primary().id("dialog-ok")},
  }
                             .spacing(10)
                             .padding(18)
                             .on_mount_fn([value, error](VStack&) {}));
  dialog_window->shortcut("Escape", [raw] { raw->close(); });
  dialog_window->closed.connect([this, raw] {
    App::post([this, raw] {
      if (dialog_window.get() == raw) dialog_window.reset();
    });
  });
  dialog_window->show();
  if (auto* field = dialog_window->find<TextField>("dialog-name")) dialog_window->set_focus(field);
}

void Designer::cmd_add_form() {
  ask_name("dialog.new_form", "dialog.new_form.hint", "Dialog",
           [this](const std::string& cls, std::string* err) { return add_form(cls, err); });
}

void Designer::cmd_add_file() {
  ask_name("dialog.new_file", "dialog.new_file.hint", "utils.hpp",
           [this](const std::string& name, std::string* err) { return add_file(name, err); });
}

// ── completion context ───────────────────────────────────────────────────────

CompletionContext Designer::completion_context() {
  sync_form();
  CompletionContext ctx;
  ctx.current_file = project.active_file.get();
  for (auto& f : project.files) ctx.other_files.emplace_back(f.name, f.text);
  for (auto& f : project.forms) {
    ctx.form_classes.push_back(f.cls);
    collect_form_views(f.json, f.cls, ctx.views);
    // the class of a form also exposes Ui::<Class>
  }
  return ctx;
}

// ── project tree ─────────────────────────────────────────────────────────────

namespace {

struct TreeItem {
  enum Kind { Root, Group, Form, File } kind = File;
  std::string label, key; // key: form index / file name
  int depth = 0;
  bool operator==(const TreeItem&) const = default;
};

const char* file_glyph(const std::string& name) {
  const std::string ext = fs::path(name).extension().string();
  if (ext == ".hpp" || ext == ".h") return "h";
  if (ext == ".cpp" || ext == ".cc") return "c";
  return "·";
}

} // namespace

Node project_tree(Designer& d) {
  auto items = std::make_shared<pulse::computed<std::vector<TreeItem>>>(
      [&d] {
        std::vector<TreeItem> out;
        out.push_back({TreeItem::Root, d.project.name.get() + (d.project.file.get().empty() ? "" : ".pulse"), "", 0});
        out.push_back({TreeItem::Group, "tree.forms", "", 1});
        for (std::size_t i = 0; i < d.project.forms.size(); ++i)
          out.push_back({TreeItem::Form, d.project.forms[i].name + ".pform", std::to_string(i), 2});
        std::vector<std::string> sources, headers, other;
        for (auto& f : d.project.files) {
          const std::string ext = fs::path(f.name).extension().string();
          (ext == ".cpp" || ext == ".cc" ? sources : ext == ".hpp" || ext == ".h" ? headers : other).push_back(f.name);
        }
        for (auto [title, list] : {std::pair<const char*, std::vector<std::string>*>{"tree.sources", &sources},
                                   {"tree.headers", &headers}, {"tree.other", &other}}) {
          if (list->empty()) continue;
          std::sort(list->begin(), list->end());
          out.push_back({TreeItem::Group, title, "", 1});
          for (auto& n : *list) out.push_back({TreeItem::File, n, n, 2});
        }
        return out;
      },
      d.project.revision, d.project.name, d.project.file);

  auto rows = ForEach(items->as_observable(), [&d](const TreeItem& it) -> Node {
    if (it.kind == TreeItem::Group)
      return Text(tr(it.label)).font_size(11).weight(FontWeight::SemiBold).dim().padding(Insets(12, 8, 8, 2));
    if (it.kind == TreeItem::Root)
      return Text(it.label).weight(FontWeight::SemiBold).padding(Insets(10, 4));
    const bool is_form = it.kind == TreeItem::Form;
    const std::string key = it.key;
    // highlight: the active file in Code mode, the active form in Design mode
    auto active = pulse::combine_latest(d.mode.as_observable(),
                                        pulse::combine_latest(d.project.active_file.as_observable(),
                                                              d.project.active_form.as_observable(),
                                                              [](const std::string& f, int i) { return std::make_pair(f, i); }),
                                        [is_form, key](int mode, const std::pair<std::string, int>& a) {
                                          const bool on = is_form ? (mode == 0 && std::to_string(a.second) == key)
                                                                  : (mode == 1 && a.first == key);
                                          return on ? th().selection : Color::transparent();
                                        });
    const bool removable = is_form ? d.project.forms.size() > 1 : key != "main.cpp";
    return HStack{
        Spacer().flex(0).width(14),
        Text(is_form ? "◫" : file_glyph(it.label)).color(pulseui::theme().map([is_form](const Theme& t) {
          return is_form ? t.accent : t.text_dim;
        })).width(14),
        Text(it.label).flex(),
        Button("✕", [&d, is_form, key] {
          if (is_form) d.remove_form(std::stoi(key));
          else d.remove_file(key);
        })
            .flat()
            .tooltip(tr("tree.remove"))
            .visible(removable)
            .padding(Insets(6, 0))
            .height(22),
    }
        .spacing(6)
        .align_items(Alignment::Center)
        .padding(Insets(6, 1))
        .radius(4)
        .background(active)
        .id((is_form ? "tree-form-" : "tree-file-") + key)
        .on_click([&d, is_form, key] {
          if (is_form) {
            d.project.active_form = std::stoi(key);
            d.mode = 0;
          } else {
            d.project.active_file = key;
            d.mode = 1;
          }
        });
  });
  rows.spacing(1);

  return VStack{
      HStack{Text(tr("tree.project")).font_size(11).weight(FontWeight::SemiBold).dim(), Spacer(),
             Button(tr("tree.add_form"), [&d] { d.cmd_add_form(); }).flat().id("add-form").tooltip(tr("tree.add_form.tip")),
             Button(tr("tree.add_file"), [&d] { d.cmd_add_file(); }).flat().id("add-file").tooltip(tr("tree.add_file.tip"))}
          .spacing(2)
          .align_items(Alignment::Center)
          .padding(Insets(12, 6, 6, 4)),
      Scroll(std::move(rows).padding(Insets(4, 0, 4, 8))).flex(),
  }
      .spacing(0)
      .background(pulseui::theme().map([](const Theme& t) { return t.surface; }))
      .on_mount_fn([items](VStack&) {});
}

} // namespace layer
