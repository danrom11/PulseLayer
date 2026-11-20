#pragma once
// A Pulse Layer project: several forms and source files, like a Qt project.
//
//   MyApp/
//     MyApp.pulse          project file (list of forms and files)
//     mainwindow.pform     layout of the MainWindow form   (Design mode)
//     mainwindow.hpp/.cpp  class MainWindow - logic of the form (Code mode)
//     login.pform, login.hpp/.cpp ...
//     main.cpp
//
// For every form the build generates ui_<name>.hpp with
//   namespace Ui { struct MainWindow { title, width, height; static pulseui::Node build(); }; }
#include <string>
#include <utility>
#include <vector>

#include <pulseui/pulseui.hpp>

namespace layer {

struct SourceFile {
  std::string name; // file name inside the project folder, e.g. "mainwindow.cpp"
  std::string text;
  bool operator==(const SourceFile&) const = default;
};

struct FormEntry {
  std::string name;  // file stem: "mainwindow" -> mainwindow.pform, ui_mainwindow.hpp
  std::string cls;   // class name: "MainWindow"
  std::string json;  // the form (pulseui-form JSON)
  bool operator==(const FormEntry&) const = default;
};

class Project {
public:
  pulse::property<std::string> name{"Untitled"};
  pulse::property<std::string> file;               // path of the .pulse file ("" = not saved yet)
  pulse::property<int> revision{0};                // the list of forms / files changed
  pulse::property<bool> dirty{false};
  pulse::property<int> active_form{0};             // form shown in Design mode
  pulse::property<std::string> active_file{"main.cpp"}; // file shown in Code mode

  std::vector<FormEntry> forms;
  std::vector<SourceFile> files;
  bool legacy_form_header = false; // imported from a single .pform: also generate form.hpp

  // ── structure ──
  void create_new(const std::string& first_form_json);
  // Adds <stem>.pform + <stem>.hpp/.cpp for class `cls`. Returns false if the name is invalid / taken.
  bool add_form(const std::string& cls, const std::string& form_json, std::string* error = nullptr);
  bool add_file(const std::string& file_name, std::string* error = nullptr);
  bool remove_file(const std::string& file_name);
  bool remove_form(int index); // also removes the form's .hpp/.cpp
  SourceFile* find_file(const std::string& file_name);
  const SourceFile* find_file(const std::string& file_name) const;

  // ── files ──
  std::string dir() const;
  bool save(const std::string& pulse_path, std::string* error = nullptr);
  bool load(const std::string& path, std::string* error = nullptr); // .pulse, or a single .pform

  // Everything the compiler needs: generated ui headers + project files.
  std::vector<std::pair<std::string, std::string>> build_files() const;
  static std::string ui_header(const FormEntry& form);

  static bool valid_class_name(const std::string& cls);
  static std::string stem_of(const std::string& cls); // "LoginDialog" -> "logindialog"
  static std::string header_template(const FormEntry& f, bool example);
  static std::string source_template(const FormEntry& f, bool example);
  static std::string main_template(const std::string& cls);

private:
  void touch();
};

} // namespace layer
