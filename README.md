<div align="center">

# Pulse Layer

**Draw the interface. Write the logic. Press Run.**

A visual studio for [PulseUI](https://github.com/danrom11/PulseUI) apps — built entirely with PulseUI itself.

`C++20` · `Windows` · `macOS` · `English · Русский · Deutsch · Français`

</div>

---

Pulse Layer turns building a desktop UI into something you can *see*. You assemble a form from real PulseUI
views on a live canvas, then switch to code and wire it to your data. Because PulseUI is reactive, there is
very little wiring to do: you bind views to `pulse::property` values and streams, and the UI keeps up on its own.

The app is also a showcase of its own toolkit. Every panel, property row and dialog is a PulseUI view bound to
a reactive model. Nothing is refreshed by hand.

## Contents

- [Highlights](#highlights)
- [Quick start](#quick-start)
- [Samples](#samples)
- [Designing a form](#designing-a-form)
- [Projects](#projects)
- [Writing code](#writing-code)
- [Build and run](#build-and-run)
- [Languages](#languages)
- [Keyboard shortcuts](#keyboard-shortcuts)
- [Building Pulse Layer](#building-pulse-layer)
- [Under the hood](#under-the-hood)

## Highlights

| | |
|---|---|
| 🎨 **Live canvas** | What you see is the real thing: forms are rendered by actual PulseUI views, not mock-ups. |
| 🧩 **Declarative output** | Forms export to short, readable C++ that you would happily write by hand. |
| 🗂 **Multi-form projects** | Every form has a layout, a header and a source file, plus your own sources. |
| ✍️ **Built-in editor** | C++ highlighting, scope-aware completion, and compiler errors underlined in place. |
| 🛠 **Zero-setup toolchains** | MSVC, Clang, GCC/MinGW, CMake and Ninja are found automatically. |
| 🌍 **Instant language switch** | The whole UI is re-translated on the fly; add a new language with a single JSON file. |

## Quick start

```bash
git clone https://github.com/danrom11/PulseLayer.git
cd PulseLayer
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

That's all: Pulse and PulseUI are downloaded automatically (see [Building Pulse Layer](#building-pulse-layer)).
Then open one of the [samples](#samples):

```bash
./build/PulseLayer samples/taskboard/TaskBoard.pulse
```

With Visual Studio the executable is in `build/Release/`, next to its `samples/` folder.

## Samples

Each sample is a complete project: open the `.pulse` file, look at the forms in **Design**, read the code in **Code**,
and press **Run** (Ctrl+R).

| Sample | What it shows |
|---|---|
| [**Task Board**](samples/taskboard) | A task list with priorities, live search and filters. The list, counters, progress bar and button states are all `pulse::computed` values derived from three properties. |
| [**Focus Timer**](samples/focustimer) | A Pomodoro timer. A countdown ring is drawn on a `Canvas` and redrawn from state; `App::every` drives the clock, and the window title shows the time left. |
| [**Color Studio**](samples/colorstudio) | Sliders mix a color; the preview, readable text color, WCAG contrast grade, tints and shades follow. The hex field binds the other way. A palette is rendered with `ForEach`. |
| [**Snippet Box**](samples/snippetbox) | A multi-form app (main window, Preferences, About) sharing one reactive store. Search uses `debounce`, edits are autosaved, and the theme switches live from Preferences. |
| [**Live Dashboard**](samples/livedashboard) | Views bound straight to Pulse streams: `throttle`, `map`, `scan` for a sliding window, and `combine_latest` + `distinct_until_changed` for alerts. Includes a live area chart. |
| [`login.pform`](samples/login.pform) | A single form, as a starting point for your own. |

## Designing a form

- **Palette.** Click a view to add it to the selected container, or drag it to the exact spot on the canvas.
- **Canvas.** Select, drag to reorder or move to another container (a drop indicator shows where it lands),
  position freely inside a `Layer`, and resize with eight snapping handles. Arrow keys nudge or reorder.
  Middle-drag or Alt-drag pans the view.
- **Hierarchy.** The view tree, always in sync with the selection.
- **Properties.** Every editable property: the view's own first, then layout, appearance and behaviour,
  plus the form's title and size.
- **Editing.** Undo and redo (with smart merging of small edits), copy and paste as JSON (between windows too),
  duplicate, delete.
- **Preview** (F5). Opens the form in its own fully interactive window.

**Export form** (Ctrl+E) saves the layout of the current form as a C++ header with plain declarative code.
It contains the design only; the code you write in Code mode stays in your project files:

```cpp
inline pulseui::Node make_sign_in() {
  using namespace pulseui;
  return
    VStack{
      Text("Welcome back").id("title").font_size(22).weight(FontWeight::SemiBold),
      TextField().placeholder("Email").id("email"),
      Button("Sign in").primary().id("ok"),
    }.spacing(12).padding(24);
}
```

Prefer loading layouts at runtime? `.pform` files can be loaded by your program, and views are found by id
(see `PulseUI/examples/form_app`).

## Projects

A project is a folder with a `.pulse` file. Each form comes as a set of three files: the layout you draw,
and a class that holds its logic.

```
MyApp/
├── MyApp.pulse              forms and files of the project
├── main.cpp
├── mainwindow.pform         layout  (Design mode)
├── mainwindow.hpp / .cpp    class MainWindow: the form's logic  (Code mode)
└── logindialog.pform / .hpp / .cpp
```

**Bindings are part of the form.** In the **Properties** panel, the chain icon next to a property binds it to a member of the
form's class (`text` ← `email`, `enabled` ← `can_submit`), and the **Events** section connects events to methods
(`click` → `submit`). Members may be properties, computed values or streams, or paths such as `settings.dark`.

At build time every form becomes a generated `ui_<name>.hpp`: a `Ui::<Class>` struct with a typed pointer for every view
with an id, and `setup(window, model)`, which builds the form and writes the bindings as ordinary PulseUI code
(`TextField().model(m.email)`). The compiler checks them: a renamed member is a build error, not a dead button.
The form's class stays free of wiring code:

```cpp
class MainWindow {
  friend struct Ui::MainWindow;            // lets the generated code bind private members
  pulse::property<std::string> email;      // bound in the form
  pulse::computed<bool> can_submit;
  void submit();                           // called by the form

  Ui::MainWindow ui_;                      // ui_.email, ui_.submit, ... for anything else
  pulseui::Window window_{Ui::MainWindow::form_title, Ui::MainWindow::form_width, Ui::MainWindow::form_height};
public:
  MainWindow() : can_submit(...) { ui_.setup(window_, *this); }
};
```

In Code mode the **project tree** (Forms · Sources · Headers) opens files in the editor and forms on the canvas.
The **+ Form** and **+ File** buttons add items and ✕ removes them. Single `.pform` files from older versions
still open; their embedded code becomes `main.cpp`.

## Writing code

Switch to **Code** with Ctrl+2 and back to **Design** with Ctrl+1.

The editor is PulseUI's own `CodeEditor` widget. It has line numbers, C++ highlighting, auto-indent, undo and redo,
and errors underlined right where they happen, with the message at the end of the line.

**Completion** appears as you type: after two letters, after `.`, `->`, `::` or `"`, or with **Ctrl+Space**. It knows:

- variables of every enclosing scope (locals, parameters, lambda captures, `for` variables);
- members of your class inside its methods;
- members after `.` and `->`, for PulseUI and Pulse types and for your own classes,
  even through `auto* ok = window_.find<Button>(…)`;
- the views of a designed form after `ui_.`, and their members after `ui_.submit->`;
- scopes such as `Alignment::`, `Ui::` and `pulse::`;
- the ids of your designed views inside `find<Button>("…")`;
- C++ keywords.

Completion is a fast, lightweight C++ reader (tokens and brace scopes), not a full compiler front end.
It is made to be instant for everyday UI code.

## Build and run

| | |
|---|---|
| **Build** (Ctrl+B) | Generates a CMake project in `.pulselayer-build/` inside the project folder and compiles it. The first build compiles PulseUI once (about 10–20 s); later builds are incremental and take seconds. |
| **Run** (Ctrl+R) | Builds if needed and starts the program. **Stop** ends it, together with any child processes. |
| **Issues** | MSVC, GCC, Clang and linker errors. Click one to jump to its line. |
| **Build output** | The complete log. |

**Kits** (⚙ Settings → Kits) are discovered in the background:

- Windows: MSVC through `vswhere` (`cl`, `link`, `vcvars64`), Clang from Visual Studio or LLVM,
  and MinGW from `PATH`, MSYS2 and other common install locations.
- macOS: Apple Clang through `xcrun`, and GCC/LLVM from Homebrew.
- CMake and Ninja are found on `PATH` or next to the compilers.

Choose a default kit in the settings or in the code toolbar. You can also point the builder to a different
PulseUI checkout.

## Languages

English is the default; Русский, Deutsch and Français are built in. Switch in **⚙ Settings → General**, where the
theme lives too (the **About** tab is one F1 away). Every label is bound to a translation stream, so the switch is
instant and needs no restart. Your choice is saved in the user settings:

- Windows: `%APPDATA%\PulseLayer\settings.json`
- macOS: `~/Library/Application Support/PulseLayer/settings.json`

To force a language for one session, run `PulseLayer --lang de`.

**Adding a language**

- **Without recompiling:** drop `xx.json` into `lang/` next to the executable.
  `PulseLayer --export-lang template.json` writes every key with its English text, ready to translate
  (see `lang/README.md`). Partial translations are welcome; missing keys fall back to English.
- **Built in:** copy `src/lang/de.cpp`, translate it, add it to `CMakeLists.txt`, and add one line to
  `register_builtin()` in `src/i18n.cpp`.

## Keyboard shortcuts

| Keys | Action |
|---|---|
| Ctrl+N · Ctrl+O · Ctrl+S · Ctrl+Shift+S | New · Open · Save · Save as |
| Ctrl+Z · Ctrl+Y (Ctrl+Shift+Z) | Undo · Redo |
| Ctrl+C · Ctrl+V · Ctrl+D | Copy · Paste · Duplicate |
| Delete · Backspace | Delete the selection |
| Esc | Select the parent |
| F5 | Preview |
| Ctrl+E | Export the form layout as C++ |
| Ctrl+1 · Ctrl+2 | Design · Code |
| Ctrl+B · Ctrl+R | Build · Build and run |
| Ctrl+Space | Code completion |
| F1 | About |

On macOS, `Ctrl` means `⌘ Cmd`.

## Building Pulse Layer

**Requirements:** CMake 3.21+ and a C++20 compiler: Visual Studio 2022 or MinGW GCC 11+ on Windows,
Xcode 15+ on macOS. Git is not needed for the dependencies.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

`CMAKE_BUILD_TYPE` is for single-configuration generators (Ninja, Makefiles); Visual Studio and Xcode pick the
configuration at build time with `--config`. Both lines are safe to use with any generator.

**Dependencies are fetched automatically.** On the first configure, the matching versions of
[Pulse](https://github.com/danrom11/Pulse) and [PulseUI](https://github.com/danrom11/PulseUI) are downloaded into
`external/` inside this folder and built from source together with the app. Later configures reuse them, even offline.
Pulse Layer's own builder also uses this PulseUI copy to build your programs.

| Option | Default | Meaning |
|---|---|---|
| `PULSELAYER_FETCH_DEPS` | `ON`, or `OFF` when `../Pulse` and `../PulseUI` exist | Download dependencies instead of using sibling checkouts |
| `PULSELAYER_PULSE_REF` | `0.2.1` | Tag, branch or commit of Pulse to download |
| `PULSELAYER_PULSEUI_REF` | `0.3.0` | Tag, branch or commit of PulseUI to download |
| `PULSELAYER_DEPS_DIR` | `external/` | Where downloads are unpacked (one subfolder per version pair) |

Working on all three projects at once? Put them side by side (`Pulse/`, `PulseUI/`, `PulseLayer/`). Your local
checkouts are then used automatically, so changes to the libraries show up in the next build.

**Self-test.** `PULSELAYER_SELFTEST=1 ./build/PulseLayer` drives the real UI with synthetic input: selection,
property edits, drag and drop from the palette, undo and redo, save and open, export, preview, settings and kit
detection. Add `PULSELAYER_SELFTEST_BUILD=1` (optionally with `PULSELAYER_SELFTEST_KIT=MinGW`) to also compile a broken
multi-form project, check the reported error line, fix it, then build, run and stop the program.
`PULSELAYER_SELFTEST_SAMPLES=samples` opens and builds every sample project (in a temporary copy).

## Under the hood

The whole application is derived from a small reactive model. The document's `revision`, `selection`, `title`, `width`,
`height`, `dirty` and `status` are `pulse::property` values.

- The hierarchy is a `ForEach` over a `computed` list of views.
- The Properties panel is a `ForEach` over the selected view's reflected properties (`pulseui::meta`). Each editor is bound
  to `revision`.
- The window title is a `computed` string.
- Translations are streams, so changing the language simply emits new text.

Change the model, and the interface follows. That is the idea behind PulseUI, and Pulse Layer is built the same way.

## License

See [LICENSE](LICENSE).
