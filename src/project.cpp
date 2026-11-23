#include "project.hpp"

#include <algorithm>
#include <cstdio>
#include <cctype>
#include <filesystem>
#include <functional>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace layer {

using namespace pulseui;

namespace {

std::string read_file(const fs::path& p, bool* ok = nullptr) {
  std::ifstream in(p, std::ios::binary);
  if (ok) *ok = static_cast<bool>(in);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

bool write_file(const fs::path& p, const std::string& text) {
  std::ofstream out(p, std::ios::binary);
  out << text;
  return static_cast<bool>(out);
}

std::string escape(const std::string& s) {
  std::string out;
  for (char c : s) {
    if (c == '"' || c == '\\') out += '\\';
    if (c == '\n') { out += "\\n"; continue; }
    out += c;
  }
  return out;
}

std::string class_from_stem(const std::string& stem) {
  std::string out;
  bool up = true;
  for (char c : stem) {
    if (!std::isalnum(static_cast<unsigned char>(c))) { up = true; continue; }
    out += up ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c;
    up = false;
  }
  if (out.empty() || std::isdigit(static_cast<unsigned char>(out[0]))) out = "Form" + out;
  return out;
}

std::string json_without_code(const std::string& form_json) {
  auto j = json::Value::parse(form_json);
  if (!j) return form_json;
  json::Value out = json::Value::object();
  for (auto& [k, v] : j->fields())
    if (k != "code") out[k] = v;
  return out.dump(2);
}

} // namespace

// ── naming / templates ───────────────────────────────────────────────────────

bool Project::valid_class_name(const std::string& cls) {
  if (cls.empty() || !(std::isalpha(static_cast<unsigned char>(cls[0])) || cls[0] == '_')) return false;
  return std::all_of(cls.begin(), cls.end(), [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; });
}

std::string Project::stem_of(const std::string& cls) {
  std::string s;
  for (char c : cls) s += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

std::string Project::header_template(const FormEntry& f, bool example) {
  const std::string state = example
      ? "    // State and actions the form is bound to. In Design mode select a view and\n"
        "    // link its properties (text, enabled...) and events (click...) to these members.\n"
        "    pulse::property<std::string> name;\n"
        "    void greet();\n"
      : "    // State and actions the form is bound to, for example:\n"
        "    //   pulse::property<std::string> text;   (link a TextField's text to `text` in Design mode)\n"
        "    //   void save();                         (link a Button's click to `save`)\n";
  return "#pragma once\n#include \"ui_" + f.name + ".hpp\"\n\n"
         "// Logic of the \"" + f.cls + "\" form. Its layout and bindings are in " + f.name + ".pform (Design mode).\n"
         "class " + f.cls + " {\npublic:\n    " + f.cls + "();\n"
         "    void show() { window_.show(); }\n"
         "    pulseui::Window& window() { return window_; }\n\n"
         "private:\n"
         "    friend struct Ui::" + f.cls + "; // the generated setup() binds the members below\n\n" +
         state + "\n"
         "    Ui::" + f.cls + " ui_; // the views with an id: ui_.ok, ui_.name, ...\n"
         "    pulseui::Window window_{Ui::" + f.cls + "::form_title, Ui::" + f.cls + "::form_width, Ui::" + f.cls +
         "::form_height};\n"
         "};\n";
}

std::string Project::source_template(const FormEntry& f, bool example) {
  std::string out = "#include \"" + f.name + ".hpp\"\n\nusing namespace pulseui;\n\n" + f.cls + "::" + f.cls + "() {\n"
                    "    ui_.setup(window_, *this); // builds the form and connects its bindings to this object\n\n"
                    "    // The views are also available directly, e.g.:\n"
                    "    // ui_.ok->tooltip(\"Save and close\");\n"
                    "}\n";
  if (example)
    out += "\nvoid " + f.cls + "::greet() {\n"
           "    window_.set_title(\"Hello, \" + name.get() + \"!\");\n"
           "}\n";
  return out;
}

std::string Project::main_template(const std::string& cls) {
  return "#include \"" + stem_of(cls) + ".hpp\"\n\n"
         "int main() {\n"
         "    pulseui::App app;\n\n"
         "    " + cls + " window;\n"
         "    window.show();\n\n"
         "    return app.run();\n"
         "}\n";
}

std::string Project::ui_header(const FormEntry& f) {
  // names a view id cannot take: members of the generated struct, and the class name itself
  const std::vector<std::string> reserved{"setup", "build", "form_title", "form_width", "form_height", f.cls};
  std::string title = f.cls;
  float w = 480, h = 360;
  std::string bound_expr = "pulseui::VStack{}", plain_expr = "pulseui::VStack{}";
  std::vector<FormRef> refs;
  std::vector<std::string> includes; // forms used as components, headers of project views
  bool bindings = false;
  if (auto j = json::Value::parse(f.json)) {
    if (auto form = form_from_json(*j)) {
      title = form->title;
      w = form->width;
      h = form->height;
      View& root = *form->root.get();
      std::function<void(View&)> scan = [&](View& v) {
        std::string inc;
        if (auto* c = dynamic_cast<FormComponent*>(&v); c && !c->form().empty() && c->form() != f.cls)
          inc = "ui_" + stem_of(c->form()) + ".hpp";
        if (auto* c = dynamic_cast<CustomView*>(&v); c && !c->header.empty()) inc = c->header;
        if (!inc.empty() && std::find(includes.begin(), includes.end(), inc) == includes.end()) includes.push_back(inc);
        for (auto& ch : v.children()) scan(*ch);
      };
      scan(root);
      bindings = form_has_bindings(root);
      refs = form_refs(root, reserved);
      CodegenOptions opt;
      opt.refs = true;
      opt.reserved = reserved;
      bound_expr = generate_cpp_expression(root, 3, opt);
      plain_expr = generate_cpp_expression(root, 3);
      for (std::string* e : {&bound_expr, &plain_expr})
        while (!e->empty() && e->front() == ' ') e->erase(0, 1);
    }
  }
  const bool legacy_names = std::none_of(refs.begin(), refs.end(), [](const FormRef& r) {
    return r.id == "title" || r.id == "width" || r.id == "height";
  });
  char size[64];
  std::snprintf(size, sizeof size, "%g, form_height = %g", w, h);

  std::string out = "// Generated by Pulse Layer from " + f.name + ".pform - do not edit, change the form instead.\n"
                    "#pragma once\n#include <pulseui/pulseui.hpp>\n";
  for (auto& inc : includes) out += "#include \"" + inc + "\"\n";
  out += std::string("\n") +
                    "namespace Ui {\n\n"
                    "// The \"" + f.cls + "\" form. Typical use in the class of the form:\n"
                    "//   Ui::" + f.cls + " ui_;    pulseui::Window window_{...};\n"
                    "//   ui_.setup(window_, *this);   // build it; bindings refer to members of *this\n"
                    "struct " + f.cls + " {\n"
                    "  static constexpr const char* form_title = \"" + escape(title) + "\";\n"
                    "  static constexpr float form_width = " + size + ";\n";
  if (legacy_names)
    out += "  static constexpr const char* title = form_title; // older name\n"
           "  static constexpr float width = form_width, height = form_height;\n";
  if (!refs.empty()) {
    out += "\n  // Views with an id (valid after setup).\n";
    for (auto& r : refs) out += "  pulseui::" + r.cpp_class + "* " + r.id + " = nullptr;\n";
  }
  out += "\n  // Builds the form into `window`. Bindings refer to members of `m`.\n"
         "  template <class Model>\n"
         "  void setup(pulseui::Window& window, Model& m) { window.content(build(m)); }\n\n"
         "  // The form as a view tree, to embed it somewhere else.\n"
         "  template <class Model>\n"
         "  pulseui::Node build([[maybe_unused]] Model& m) {\n"
         "    using namespace pulseui;\n"
         "    return " + bound_expr + ";\n"
         "  }\n";
  if (!bindings) {
    out += "\n  // Without bindings the form needs no model.\n"
           "  void setup(pulseui::Window& window) {\n"
           "    using namespace pulseui;\n"
           "    window.content(" + bound_expr + ");\n"
           "  }\n\n"
           "  // Layout only, without the view pointers (older API).\n"
           "  static pulseui::Node build() {\n"
           "    using namespace pulseui;\n"
           "    return " + plain_expr + ";\n"
           "  }\n";
  }
  out += "\n  // The form as a part of another form (a Component there). Its bindings refer\n"
         "  // to the members of `m`: the other form's class, or a member object of it.\n"
         "  template <class Model>\n"
         "  static pulseui::Node part([[maybe_unused]] Model& m) {\n"
         "    using namespace pulseui;\n"
         "    return " + plain_expr + ";\n"
         "  }\n";
  out += "};\n\n} // namespace Ui\n";
  return out;
}

// ── structure ────────────────────────────────────────────────────────────────

void Project::touch() {
  dirty = true;
  revision = revision.get() + 1;
}

void Project::create_new(const std::string& first_form_json) {
  forms.clear();
  files.clear();
  legacy_form_header = false;
  FormEntry main{"mainwindow", "MainWindow", json_without_code(first_form_json)};
  forms.push_back(main);
  files.push_back({"main.cpp", main_template(main.cls)});
  files.push_back({"mainwindow.hpp", header_template(main, true)});
  files.push_back({"mainwindow.cpp", source_template(main, true)});
  name = "Untitled";
  file = "";
  active_form = 0;
  active_file = "main.cpp";
  revision = revision.get() + 1;
  dirty = false;
}

bool Project::add_form(const std::string& cls, const std::string& form_json, std::string* error) {
  if (!valid_class_name(cls)) {
    if (error) *error = "invalid";
    return false;
  }
  const std::string stem = stem_of(cls);
  for (auto& f : forms)
    if (f.name == stem || f.cls == cls) {
      if (error) *error = "exists";
      return false;
    }
  if (find_file(stem + ".hpp") || find_file(stem + ".cpp")) {
    if (error) *error = "exists";
    return false;
  }
  FormEntry f{stem, cls, json_without_code(form_json)};
  forms.push_back(f);
  files.push_back({stem + ".hpp", header_template(f, false)});
  files.push_back({stem + ".cpp", source_template(f, false)});
  touch();
  return true;
}

bool Project::add_file(const std::string& file_name, std::string* error) {
  const fs::path p(file_name);
  if (file_name.empty() || p.has_parent_path() || p.extension().empty() ||
      file_name.find_first_of("\\/:*?\"<>|") != std::string::npos) {
    if (error) *error = "invalid";
    return false;
  }
  if (find_file(file_name)) {
    if (error) *error = "exists";
    return false;
  }
  const std::string ext = p.extension().string();
  std::string text;
  if (ext == ".hpp" || ext == ".h") text = "#pragma once\n\n";
  else if (ext == ".cpp" || ext == ".cc") {
    const fs::path header = p.stem().string() + ".hpp";
    text = find_file(header.string()) ? "#include \"" + header.string() + "\"\n\n" : "#include <pulseui/pulseui.hpp>\n\n";
  }
  files.push_back({file_name, text});
  touch();
  return true;
}

bool Project::remove_file(const std::string& file_name) {
  if (file_name == "main.cpp") return false;
  auto it = std::find_if(files.begin(), files.end(), [&](const SourceFile& f) { return f.name == file_name; });
  if (it == files.end()) return false;
  files.erase(it);
  if (active_file.get() == file_name) active_file = "main.cpp";
  touch();
  return true;
}

bool Project::remove_form(int index) {
  if (forms.size() <= 1 || index < 0 || index >= static_cast<int>(forms.size())) return false;
  const std::string stem = forms[static_cast<std::size_t>(index)].name;
  forms.erase(forms.begin() + index);
  std::erase_if(files, [&](const SourceFile& f) { return f.name == stem + ".hpp" || f.name == stem + ".cpp"; });
  if (active_file.get() == stem + ".hpp" || active_file.get() == stem + ".cpp") active_file = "main.cpp";
  if (active_form.get() >= static_cast<int>(forms.size())) active_form = static_cast<int>(forms.size()) - 1;
  touch();
  return true;
}

SourceFile* Project::find_file(const std::string& file_name) {
  for (auto& f : files)
    if (f.name == file_name) return &f;
  return nullptr;
}

const SourceFile* Project::find_file(const std::string& file_name) const {
  for (auto& f : files)
    if (f.name == file_name) return &f;
  return nullptr;
}

// ── files ────────────────────────────────────────────────────────────────────

std::string Project::dir() const { return file.get().empty() ? std::string() : fs::path(file.get()).parent_path().string(); }

bool Project::save(const std::string& pulse_path, std::string* error) {
  const fs::path p(pulse_path);
  const fs::path d = p.parent_path();
  std::error_code ec;
  fs::create_directories(d, ec);
  json::Value j = json::Value::object();
  j["format"] = "pulselayer-project";
  j["version"] = 1;
  j["name"] = p.stem().string();
  json::Value fl = json::Value::array();
  for (auto& f : forms) {
    json::Value e = json::Value::object();
    e["name"] = f.name;
    e["class"] = f.cls;
    fl.push_back(e);
    if (!write_file(d / (f.name + ".pform"), f.json + "\n")) {
      if (error) *error = (d / (f.name + ".pform")).string();
      return false;
    }
  }
  j["forms"] = std::move(fl);
  json::Value src = json::Value::array();
  for (auto& f : files) {
    src.push_back(f.name);
    if (!write_file(d / f.name, f.text)) {
      if (error) *error = (d / f.name).string();
      return false;
    }
  }
  j["files"] = std::move(src);
  if (legacy_form_header) j["legacy_form_header"] = true;
  if (!write_file(p, j.dump(2) + "\n")) {
    if (error) *error = p.string();
    return false;
  }
  name = p.stem().string();
  file = p.string();
  dirty = false;
  return true;
}

bool Project::load(const std::string& path, std::string* error) {
  const fs::path p(path);
  bool ok = false;
  const std::string text = read_file(p, &ok);
  if (!ok) {
    if (error) *error = "cannot open " + path;
    return false;
  }
  auto j = json::Value::parse(text, error);
  if (!j) return false;

  std::vector<FormEntry> new_forms;
  std::vector<SourceFile> new_files;
  bool legacy = false;
  if ((*j)["format"].as_string() == "pulseui-form") {
    // a single form (older Pulse Layer): one form, its embedded code becomes main.cpp
    if (!form_from_json(*j, error)) return false;
    FormEntry f{p.stem().string(), class_from_stem(p.stem().string()), json_without_code(text)};
    new_forms.push_back(f);
    if (auto* code = j->find("code"); code && code->is_string()) {
      new_files.push_back({"main.cpp", code->as_string()});
      legacy = true;
    } else {
      new_files.push_back({"main.cpp", main_template(f.cls)});
      new_files.push_back({f.name + ".hpp", header_template(f, false)});
      new_files.push_back({f.name + ".cpp", source_template(f, false)});
    }
    name = p.stem().string();
    file = "";
  } else if ((*j)["format"].as_string() == "pulselayer-project") {
    const fs::path d = p.parent_path();
    for (auto& e : (*j)["forms"].items()) {
      FormEntry f{e["name"].as_string(), e["class"].as_string(), {}};
      f.json = read_file(d / (f.name + ".pform"), &ok);
      if (!ok) {
        if (error) *error = "missing " + f.name + ".pform";
        return false;
      }
      if (f.cls.empty()) f.cls = class_from_stem(f.name);
      new_forms.push_back(std::move(f));
    }
    for (auto& e : (*j)["files"].items()) new_files.push_back({e.as_string(), read_file(d / e.as_string())});
    legacy = (*j)["legacy_form_header"].as_bool();
    name = (*j)["name"].as_string(p.stem().string());
    file = p.string();
  } else {
    if (error) *error = "not a Pulse Layer project";
    return false;
  }
  if (new_forms.empty()) {
    if (error) *error = "the project has no forms";
    return false;
  }
  forms = std::move(new_forms);
  files = std::move(new_files);
  legacy_form_header = legacy;
  active_form = 0;
  active_file = find_file("main.cpp") ? std::string("main.cpp") : files.empty() ? std::string() : files[0].name;
  revision = revision.get() + 1;
  dirty = false;
  return true;
}

std::vector<std::pair<std::string, std::string>> Project::build_files() const {
  std::vector<std::pair<std::string, std::string>> out;
  for (auto& f : forms) out.emplace_back("ui_" + f.name + ".hpp", ui_header(f));
  if (legacy_form_header && !forms.empty()) {
    // older single-form projects use make_form() / kFormTitle from form.hpp
    const FormEntry& f = forms[0];
    out.emplace_back("form.hpp", "#pragma once\n#include \"ui_" + f.name + ".hpp\"\n"
                                 "inline pulseui::Node make_form() { return Ui::" + f.cls + "::build(); }\n"
                                 "inline constexpr const char* kFormTitle = Ui::" + f.cls + "::form_title;\n"
                                 "inline constexpr float kFormWidth = Ui::" + f.cls + "::form_width;\n"
                                 "inline constexpr float kFormHeight = Ui::" + f.cls + "::form_height;\n");
  }
  for (auto& f : files) {
    std::string text = f.text;
    if (!text.empty() && text.back() != '\n') text += '\n';
    out.emplace_back(f.name, text);
  }
  return out;
}

} // namespace layer
