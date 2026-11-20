#pragma once
// The shared model of the app. All three forms hold a reference to one Store
// and simply bind to its properties - that is how they stay in sync.
#include <pulseui/pulseui.hpp>

#include <optional>
#include <string>
#include <vector>

struct Snippet {
    int id = 0;
    std::string title;
    int language = 0; // index into Store::languages()
    std::string code;
    bool operator==(const Snippet&) const = default;
};

class Store {
public:
    Store();
    ~Store() { save(); } // keep the last edits even if the autosave timer has not fired yet

    static const std::vector<std::string>& languages();

    // ── data ──
    pulse::property<std::vector<Snippet>> snippets;
    pulse::property<int> selected{0};           // id of the open snippet (0 = none)

    // ── preferences (edited in the Preferences form) ──
    pulse::property<bool> dark{true};
    pulse::property<bool> line_numbers{true};
    pulse::property<int> default_language{0};

    // ── operations ──
    int add();
    void remove(int id);
    std::optional<Snippet> find(int id) const;
    template <class F>
    void edit(int id, F&& fn) {
        snippets.update([&](std::vector<Snippet>& all) {
            for (Snippet& s : all)
                if (s.id == id) fn(s);
        });
    }

private:
    void load();
    void save() const;

    int next_id_ = 1;
    pulse::subscription autosave_;
};
