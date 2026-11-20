#pragma once
// The designer's model: the edited form, selection and undo history.
// Everything the panels display is derived from these reactive properties.
#include <chrono>
#include <optional>
#include <string>
#include <vector>

#include <pulseui/pulseui.hpp>

#include "i18n.hpp"

namespace layer {

using namespace pulseui;

class Document {
public:
  Document() = default;
  // `host` owns the form root as its only child (the design canvas).
  void set_host(View& host);

  // ── reactive state ──
  pulse::property<int> revision{0};          // bumped on every change of the form
  pulse::property<View*> selection{nullptr};
  pulse::property<std::string> title{"Untitled form"};
  pulse::property<double> width{480}, height{360};
  pulse::property<std::string> path;
  pulse::property<bool> dirty{false};
  pulse::property<i18n::Message> status{i18n::Message{"status.ready", ""}}; // translated by the status bar

  View* root() const { return root_; }

  // ── editing ──
  // Take an undo snapshot before a change. Edits with the same non-empty key
  // that follow each other quickly are merged into one undo step.
  void begin_edit(const std::string& merge_key = {});
  void changed();

  void set_prop(View* v, const meta::Property* p, const meta::Value& value);
  // Form bindings: key "bind.<property>" or "on.<event>", value a member name ("" removes).
  void set_binding(View* v, const std::string& key, const std::string& value);
  View* insert(const std::string& type, View* container, int index, std::optional<Point> pos = std::nullopt);
  View* insert_near_selection(const std::string& type);
  void remove(View* v);
  View* duplicate(View* v);
  void move(View* v, View* new_parent, int index);
  void copy(View* v);
  void paste();
  void set_form_size(double w, double h);
  void set_form_title(const std::string& t);

  bool can_undo() const { return !undo_.empty(); }
  bool can_redo() const { return !redo_.empty(); }
  void undo();
  void redo();

  // ── files ──
  void new_form();
  bool open(const std::string& file);
  bool save(const std::string& file);
  std::string to_json_text();
  std::string export_cpp();
  // The edited form as pulseui-form JSON, and loading another form into the canvas
  // (the project keeps the forms that are not being edited as JSON).
  std::string form_json();
  bool load_json(const std::string& form_json);

  // ── helpers ──
  static bool is_container(const View* v);
  static bool is_layer(const View* v);
  bool is_descendant(const View* v, const View* ancestor) const;
  std::vector<int> path_of(const View* v) const;
  View* at_path(const std::vector<int>& p) const;
  std::string unique_id(const std::string& type) const;

private:
  struct Snapshot {
    std::string json;
    std::vector<int> selection;
    std::string title;
    double width, height;
  };
  Snapshot snapshot() const;
  void restore(const Snapshot& s);
  void set_root(Node n);

  View* host_ = nullptr;
  View* root_ = nullptr;
  std::vector<Snapshot> undo_, redo_;
  std::string last_key_;
  std::chrono::steady_clock::time_point last_edit_{};
  std::string clipboard_;
};

} // namespace layer
