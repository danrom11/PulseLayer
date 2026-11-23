// Editor tools of Code mode: tabs, find / replace, go to definition, rename,
// create definition, header <-> source, clang-format and the error check on save.
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

#include <pulseui/core/json.hpp>

#include "panels.hpp"
#include "process.hpp"

namespace fs = std::filesystem;

namespace layer {

namespace {

std::string join_lines(const std::vector<std::string>& lines) {
  std::string out;
  for (std::size_t i = 0; i < lines.size(); ++i) out += (i ? "\n" : "") + lines[i];
  return out;
}

std::vector<std::string> split_lines(const std::string& text) {
  std::vector<std::string> out(1);
  for (char c : text) {
    if (c == '\r') continue;
    if (c == '\n') out.emplace_back();
    else out.back() += c;
  }
  return out;
}

// Replaces whole-identifier uses of `from` (code only, see find_occurrences).
std::size_t rename_in(std::vector<std::string>& lines, const std::string& from, const std::string& to) {
  const auto occ = find_occurrences(lines, from);
  for (auto it = occ.rbegin(); it != occ.rend(); ++it) lines[it->first].replace(it->second, from.size(), to);
  return occ.size();
}

// Form bindings ("bind": {"text": "name"}, "on": {"click": "greet"}) that use `from`.
pulseui::json::Value rename_bindings(const pulseui::json::Value& v, const std::string& from, const std::string& to, bool& changed) {
  using pulseui::json::Value;
  if (v.is_array()) {
    Value out = Value::array();
    for (auto& x : v.items()) out.push_back(rename_bindings(x, from, to, changed));
    return out;
  }
  if (!v.is_object()) return v;
  Value out = Value::object();
  for (auto& [k, x] : v.fields()) {
    if ((k == "bind" || k == "on") && x.is_object()) {
      Value m = Value::object();
      for (auto& [prop, member] : x.fields()) {
        std::string s = member.as_string();
        if (s == from || s.rfind(from + ".", 0) == 0) { // "settings.dark" when renaming "settings"
          s = to + s.substr(from.size());
          changed = true;
        }
        m[prop] = Value(s);
      }
      out[k] = std::move(m);
    } else {
      out[k] = rename_bindings(x, from, to, changed);
    }
  }
  return out;
}

int count_newlines(const std::string& s) { return static_cast<int>(std::count(s.begin(), s.end(), '\n')); }

} // namespace

// ── tabs ─────────────────────────────────────────────────────────────────────

void Designer::wire_editor_tools() {
  project.active_file.changed.connect([this](const std::string& name) {
    auto tabs = open_files.get();
    if (!name.empty() && std::find(tabs.begin(), tabs.end(), name) == tabs.end()) {
      tabs.push_back(name);
      open_files = tabs;
    }
    if (find_open.get()) find_refresh(false);
  });
  find_query.changed.connect([this](const std::string&) { find_refresh(true); });
  find_case.changed.connect([this](bool) { find_refresh(true); });
  find_word.changed.connect([this](bool) { find_refresh(true); });
}

void Designer::close_tab(const std::string& name) {
  auto tabs = open_files.get();
  auto it = std::find(tabs.begin(), tabs.end(), name);
  if (it == tabs.end() || tabs.size() <= 1) return;
  const std::size_t i = static_cast<std::size_t>(it - tabs.begin());
  tabs.erase(it);
  open_files = tabs;
  if (project.active_file.get() == name) project.active_file = tabs[std::min(i, tabs.size() - 1)];
}

// ── find / replace ───────────────────────────────────────────────────────────

void Designer::cmd_find(bool replace) {
  if (mode.get() != 1 || !editor) return;
  std::string seed = editor->selected_text();
  if (seed.empty() || seed.find('\n') != std::string::npos)
    seed = editor->word_at(static_cast<std::size_t>(editor->caret_line() - 1), static_cast<std::size_t>(editor->caret_column() - 1));
  find_open = true;
  replace_open = replace;
  if (!seed.empty() && seed != find_query.get()) find_query = seed;
  else find_refresh(true);
  if (window && find_field) window->set_focus(find_field);
}

void Designer::find_close() {
  find_open = false;
  replace_open = false;
  if (editor) {
    editor->set_search("");
    editor->focus();
  }
}

void Designer::find_refresh(bool select) {
  if (!editor) return;
  editor->set_search(find_open.get() ? find_query.get() : std::string(), find_case.get(), find_word.get());
  if (select && find_open.get() && !find_query.get().empty()) editor->find_next(false, true);
  const std::size_t n = editor->search_count();
  const int cur = editor->search_current();
  if (find_query.get().empty() || !find_open.get()) find_status = "";
  else if (n == 0) find_status = i18n::text("find.none");
  else find_status = (cur >= 0 ? std::to_string(cur + 1) : std::string("–")) + " / " + std::to_string(n);
}

void Designer::find_step(bool backwards) {
  if (!editor || mode.get() != 1) return;
  if (!find_open.get()) return cmd_find(false);
  editor->find_next(backwards);
  find_refresh(false);
}

void Designer::cmd_replace_one() {
  if (!editor) return;
  editor->replace_current(replace_query.get());
  find_refresh(false);
}

void Designer::cmd_replace_all() {
  if (!editor) return;
  const std::size_t n = editor->replace_all(replace_query.get());
  doc.status = i18n::Message{"status.replaced", std::to_string(n)};
  find_refresh(false);
}

// ── navigation ───────────────────────────────────────────────────────────────

bool Designer::go_to(const SourceLocation& loc) {
  if (!loc.form.empty()) { // a designed form, maybe a view in it
    for (std::size_t i = 0; i < project.forms.size(); ++i) {
      if (project.forms[i].cls != loc.form) continue;
      sync_form();
      project.active_form = static_cast<int>(i);
      mode = 0;
      if (!loc.view.empty())
        if (View* root = doc.root())
          if (View* v = root->find(loc.view)) doc.selection = v;
      doc.status = i18n::Message{"status.form_opened", loc.form + (loc.view.empty() ? "" : " · " + loc.view)};
      return true;
    }
    return false;
  }
  if (!project.find_file(loc.file)) return false;
  mode = 1;
  if (project.active_file.get() != loc.file) project.active_file = loc.file;
  if (editor) editor->go_to(loc.line, loc.column);
  return true;
}

void Designer::go_to_definition(std::size_t line, std::size_t col) {
  if (!editor) return;
  const auto& lines = editor->lines();
  const auto loc = find_definition(lines, line, col, completion_context());
  if (!loc || !go_to(*loc)) doc.status = i18n::Message{"status.no_definition", editor->word_at(line, col)};
}

void Designer::cmd_go_to_definition() {
  if (!editor || mode.get() != 1) return;
  go_to_definition(static_cast<std::size_t>(editor->caret_line() - 1), static_cast<std::size_t>(editor->caret_column() - 1));
}

void Designer::cmd_switch_header() {
  if (mode.get() != 1) return;
  const std::string name = project.active_file.get();
  const auto dot = name.rfind('.');
  if (dot == std::string::npos) return;
  const std::string stem = name.substr(0, dot), ext = name.substr(dot);
  const bool header = ext == ".hpp" || ext == ".h" || ext == ".hh";
  for (const char* e : header ? std::vector<const char*>{".cpp", ".cc", ".cxx"} : std::vector<const char*>{".hpp", ".h", ".hh"})
    if (project.find_file(stem + e)) {
      project.active_file = stem + e;
      return;
    }
  doc.status = i18n::Message{"status.no_counterpart", name};
}

// ── refactoring ──────────────────────────────────────────────────────────────

bool Designer::rename_symbol(const std::string& from, const std::string& to, bool whole_project) {
  if (!editor || from == to) return false;
  std::size_t total = 0;
  // the open file goes through the editor (one undo step)
  auto lines = editor->lines();
  const std::size_t here = rename_in(lines, from, to);
  if (here) editor->replace_all_text(join_lines(lines));
  total += here;
  if (whole_project) {
    for (auto& f : project.files) {
      if (f.name == project.active_file.get()) continue;
      auto other = split_lines(f.text);
      if (const std::size_t n = rename_in(other, from, to)) {
        f.text = join_lines(other);
        total += n;
        project.dirty = true;
      }
    }
    // members bound in the designed forms
    sync_form();
    for (std::size_t i = 0; i < project.forms.size(); ++i) {
      auto j = pulseui::json::Value::parse(project.forms[i].json);
      if (!j) continue;
      bool changed = false;
      auto renamed = rename_bindings(*j, from, to, changed);
      if (!changed) continue;
      project.forms[i].json = renamed.dump(2);
      project.dirty = true;
      if (static_cast<int>(i) == loaded_form) {
        doc.load_json(project.forms[i].json);
        doc.dirty = true;
      }
    }
  }
  doc.status = i18n::Message{"status.renamed", std::to_string(total)};
  return total > 0;
}

void Designer::cmd_rename() {
  if (!editor || mode.get() != 1) return;
  const std::size_t line = static_cast<std::size_t>(editor->caret_line() - 1);
  const std::size_t col = static_cast<std::size_t>(editor->caret_column() - 1);
  const std::string old_name = editor->word_at(line, col);
  if (old_name.empty() || !pulseui::is_cpp_identifier(old_name)) {
    doc.status = i18n::Message{"status.rename_none", ""};
    return;
  }
  const bool shared = symbol_is_shared(editor->lines(), line, col, completion_context());
  ask_name("dialog.rename", shared ? "dialog.rename.hint_project" : "dialog.rename.hint_file", old_name,
           [this, old_name, shared](const std::string& name, std::string* err) {
             if (!pulseui::is_cpp_identifier(name)) {
               *err = "invalid";
               return false;
             }
             rename_symbol(old_name, name, shared);
             return true;
           });
}

void Designer::cmd_create_definition() {
  if (!editor || mode.get() != 1) return;
  const auto stub = definition_stub(editor->lines(), static_cast<std::size_t>(editor->caret_line() - 1), completion_context());
  if (!stub) {
    doc.status = i18n::Message{"status.no_declaration", ""};
    return;
  }
  if (stub->existing) {
    go_to(*stub->existing);
    doc.status = i18n::Message{"status.definition_exists", stub->name};
    return;
  }
  int body_line = 0; // 1-based line of the empty body
  if (stub->file == project.active_file.get()) {
    const auto& lines = editor->lines();
    const std::size_t last = lines.size() - 1;
    const std::string add = std::string(lines[last].empty() ? "\n" : "\n\n") + stub->text;
    body_line = static_cast<int>(last) + count_newlines(add.substr(0, add.size() - stub->text.size())) + 2;
    editor->replace_range(last, lines[last].size(), last, lines[last].size(), add);
  } else {
    SourceFile* f = project.find_file(stub->file);
    if (!f) return;
    std::string& text = f->text;
    if (!text.empty() && text.back() != '\n') text += '\n';
    text += "\n";
    body_line = count_newlines(text) + 2;
    text += stub->text;
    project.dirty = true;
    project.active_file = stub->file;
  }
  editor->go_to(body_line, 5);
  doc.status = i18n::Message{"status.definition_added", stub->name};
}

// ── clang-format ─────────────────────────────────────────────────────────────

void Designer::cmd_format() {
  if (!editor || mode.get() != 1) return;
  const std::string exe = find_clang_format(toolchains.get());
  if (exe.empty()) {
    doc.status = i18n::Message{"status.no_clang_format", ""};
    return;
  }
  const std::string file = project.active_file.get();
  const std::string text = editor->text();
  std::error_code ec;
  const fs::path dir = fs::path(project_dir()) / "format";
  fs::create_directories(dir, ec);
  const fs::path tmp = dir / file;
  {
    std::ofstream out(tmp, std::ios::binary);
    out << text;
  }
  // the project's own .clang-format wins; otherwise a style that matches the templates
  std::string style = "{BasedOnStyle: LLVM, IndentWidth: 4, ColumnLimit: 110, AccessModifierOffset: -4, "
                      "AllowShortFunctionsOnASingleLine: Inline, AllowShortLambdasOnASingleLine: All}";
  if (const SourceFile* cf = project.find_file(".clang-format")) {
    std::ofstream(dir / ".clang-format", std::ios::binary) << cf->text;
    style = "file";
  }
  const std::string cmd = Process::quote(exe) + " -i --style=" + Process::quote(style) + " " + Process::quote(tmp.string());
  std::weak_ptr<bool> weak = alive;
  std::thread([this, weak, cmd, tmp, file, text] {
    std::string out;
    const int code = Process::run(cmd, out, 20000);
    std::ifstream in(tmp, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    const std::string formatted = ss.str();
    App::post([this, weak, code, out, file, text, formatted] {
      if (weak.expired()) return;
      if (code != 0 || formatted.empty()) {
        doc.status = i18n::Message{"status.format_failed", out.substr(0, out.find('\n'))};
        return;
      }
      if (!editor || project.active_file.get() != file || editor->text() != text) return; // edited meanwhile
      editor->replace_all_text(formatted);
      doc.status = i18n::Message{"status.formatted", file};
    });
  }).detach();
}

// ── error check on save ──────────────────────────────────────────────────────

bool Designer::check_syntax() {
  if (checking || builder.state.get() != Builder::Idle) return false;
  Builder::Request r = build_request();
  if (r.kit.cxx.empty()) return false;
  std::weak_ptr<bool> weak = alive;
  const bool started = builder.check(r, [this, weak](std::vector<Issue> found) {
    if (weak.expired()) return;
    checking = false;
    if (builder.state.get() != Builder::Idle) return; // a build started meanwhile: it reports its own issues
    builder.issues = found;
    int errors = 0;
    for (auto& i : found) errors += i.severity == Diagnostic::Error;
    doc.status = errors ? i18n::Message{"status.check_errors", std::to_string(errors)} : i18n::Message{"status.check_ok", ""};
  });
  if (!started) {
    doc.status = i18n::Message{"status.check_needs_build", ""};
    return false;
  }
  checking = true;
  doc.status = i18n::Message{"status.checking", ""};
  return true;
}

} // namespace layer
