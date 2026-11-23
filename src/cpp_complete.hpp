#pragma once
// Code completion for the editor: names visible in the current scope, members
// after '.' / '->' (types of whole chains: a.b()->c[0].), 'Type::' scopes, ids
// of the designed views inside find<T>("..."), #include paths, snippets,
// PulseUI / Pulse / std API, and parameter hints for the call at the caret.
//
// It is a lightweight, heuristic C++ reader (tokens + brace scopes), not a
// compiler front-end: good enough for typical UI code, never blocks typing.
#include <functional>
#include <optional>
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
  std::vector<std::string> headers;                             // project headers for #include "..."
};

pulseui::CompletionResult complete_cpp(const std::vector<std::string>& lines, std::size_t line, std::size_t col,
                                       const CompletionContext& ctx);

pulseui::CodeEditor::CompletionProvider make_cpp_completion(std::function<CompletionContext()> context);

// ── navigation and refactoring (positions: 0-based line, byte column) ──

struct SourceLocation {
  std::string file;          // project file; empty for a designed view / form
  int line = 0, column = 0;  // 1-based
  std::string form, view;    // a designed form (and a view id in it): open it in Design mode
};

// Where the symbol at the position is defined: a function's body (or its declaration
// when the caret is on the body), a variable's or a class's declaration, a designed view.
std::optional<SourceLocation> find_definition(const std::vector<std::string>& lines, std::size_t line, std::size_t col,
                                              const CompletionContext& ctx);

// Tooltip text for the symbol at the position ("void MainWindow::greet()", "Button* ok ..."), or "".
std::string describe_symbol(const std::vector<std::string>& lines, std::size_t line, std::size_t col,
                            const CompletionContext& ctx);

// True when the symbol is a class member or a namespace-scope name (renaming it touches
// every file), false for parameters and local variables (only the current function).
bool symbol_is_shared(const std::vector<std::string>& lines, std::size_t line, std::size_t col, const CompletionContext& ctx);

// Every use of an identifier as code (not in comments or strings): (line, column).
std::vector<std::pair<std::size_t, std::size_t>> find_occurrences(const std::vector<std::string>& lines,
                                                                  const std::string& name);

// For a function declared on `line`: the empty definition to add, or where it already is.
struct DefinitionStub {
  std::string name;                       // "MainWindow::save"
  std::string file;                       // where to add it (the matching .cpp of a header)
  std::string text;                       // "void MainWindow::save(int x) {\n    \n}\n"
  std::optional<SourceLocation> existing; // already defined
};
std::optional<DefinitionStub> definition_stub(const std::vector<std::string>& lines, std::size_t line,
                                              const CompletionContext& ctx);

// Ids (with types) of every view in a pulseui-form JSON text.
void collect_form_views(const std::string& form_json, const std::string& form_class, std::vector<FormView>& out);

} // namespace layer
