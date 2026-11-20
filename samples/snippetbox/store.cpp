#include "store.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace pulseui;
using namespace std::chrono_literals;

namespace {

std::filesystem::path data_file() { return std::filesystem::temp_directory_path() / "snippetbox.json"; }

std::vector<Snippet> examples() {
    return {
        {1, "Reactive counter", 0,
         "pulse::property<int> count{0};\n\nText(count.map([](int c) { return std::to_string(c); }));\n"
         "Button(\"+\", [&] { count = count + 1; });\n"},
        {2, "Debounced search", 0,
         "auto query = field.text_changes()\n    | pulse::debounce(300ms, App::ui());\n\n"
         "auto sub = query.subscribe([](const std::string& q) { search(q); });\n"},
        {3, "List files", 3, "for f in *.cpp; do\n    echo \"$f: $(wc -l < \"$f\") lines\"\ndone\n"},
        {4, "Fibonacci", 1, "def fib(n):\n    a, b = 0, 1\n    for _ in range(n):\n        a, b = b, a + b\n    return a\n"},
    };
}

} // namespace

const std::vector<std::string>& Store::languages() {
    static const std::vector<std::string> names{"C++", "Python", "JavaScript", "Shell", "Text"};
    return names;
}

Store::Store() {
    load();
    // Save automatically, but not on every keystroke: wait until typing pauses.
    autosave_ = (snippets.as_observable() | pulse::skip(1) | pulse::debounce(500ms, App::ui()))
                    .subscribe([this](const std::vector<Snippet>&) { save(); });
}

int Store::add() {
    const int id = next_id_++;
    snippets.update([&](std::vector<Snippet>& all) {
        all.insert(all.begin(), Snippet{id, "Untitled snippet", default_language.get(), ""});
    });
    selected = id;
    return id;
}

void Store::remove(int id) {
    snippets.update([id](std::vector<Snippet>& all) { std::erase_if(all, [id](const Snippet& s) { return s.id == id; }); });
    if (selected.get() == id) selected = snippets.get().empty() ? 0 : snippets.get().front().id;
}

std::optional<Snippet> Store::find(int id) const {
    for (const Snippet& s : snippets.get())
        if (s.id == id) return s;
    return std::nullopt;
}

void Store::load() {
    std::ifstream in(data_file(), std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    std::vector<Snippet> list;
    if (auto j = json::Value::parse(text.str())) {
        for (const json::Value& e : (*j)["snippets"].items())
            list.push_back({static_cast<int>(e["id"].as_number()), e["title"].as_string(),
                            static_cast<int>(e["language"].as_number()), e["code"].as_string()});
    }
    if (list.empty()) list = examples();
    for (const Snippet& s : list) next_id_ = std::max(next_id_, s.id + 1);
    snippets = list;
    selected = list.front().id;
}

void Store::save() const {
    json::Value root = json::Value::object();
    json::Value list = json::Value::array();
    for (const Snippet& s : snippets.get()) {
        json::Value e = json::Value::object();
        e["id"] = s.id;
        e["title"] = s.title;
        e["language"] = s.language;
        e["code"] = s.code;
        list.push_back(std::move(e));
    }
    root["snippets"] = std::move(list);
    std::ofstream(data_file(), std::ios::binary) << root.dump(2);
}
