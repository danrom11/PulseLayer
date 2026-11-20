#pragma once
// Code completion for the editor: names visible in the current scope, members
// after '.' / '->', 'Type::' scopes, ids of the designed views inside
// find<T>("..."), PulseUI / Pulse API and C++ keywords.
//
// It is a lightweight, heuristic C++ reader (tokens + brace scopes), not a
// compiler front-end: good enough for typical UI code, never blocks typing.
#include <functional>
#include <string>
#include <vector>

#include <pulseui/ui/code_editor.hpp>

namespace layer {

struct FormView {
  std::string id;   // "ok"
  std::string type; // "Button"
  std::string form; // "MainWindow"
};

struct CompletionContext {
  std::string current_file;                                     // e.g. "mainwindow.cpp"
  std::vector<std::pair<std::string, std::string>> other_files; // name -> text (classes / globals come from here)
  std::vector<std::string> form_classes;                        // Ui::<Class>
  std::vector<FormView> views;                                  // ids of all forms
};

pulseui::CompletionResult complete_cpp(const std::vector<std::string>& lines, std::size_t line, std::size_t col,
                                       const CompletionContext& ctx);

pulseui::CodeEditor::CompletionProvider make_cpp_completion(std::function<CompletionContext()> context);

// Ids (with types) of every view in a pulseui-form JSON text.
void collect_form_views(const std::string& form_json, const std::string& form_class, std::vector<FormView>& out);

} // namespace layer
