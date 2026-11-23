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
| ✍️ **Built-in editor** | C++ highlighting, type-aware completion with parameter hints, and compiler errors underlined in place. |
| 🛠 **Zero-setup toolchains** | MSVC, Clang, GCC/MinGW, CMake and Ninja are found automatically. |
| 🌍 **Instant language switch** | The whole UI is re-translated on the fly; add a new language with a single JSON file. |
| 🔗 **Binding suggestions** | The binding fields list the members of the form's class with their types (and `item.<field>` inside list rows). |
| 📋 **Lists and components** | Draw a list row and bind it to a `std::vector`; reuse forms inside forms; the project's own view classes appear in the palette. |
| 🖼 **Preview board** | The form in dark and light theme, other sizes and with pseudo-translated long texts, side by side. |
| 🐞 **Debugger** | Breakpoints, stepping, call stack and locals through GDB (MinGW), CDB (MSVC) or LLDB (macOS). |
| 📦 **Release** | One click: an optimized, self-contained `.exe` with the DLLs it needs, or a signed `.app` and `.dmg`. |

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
./samples/taskboard/TaskBoard.pulse
```

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
| [**Planner**](samples/planner) | A task planner built around a **ForEach row template** (keyed by `id`, bound to `item.title`, `item.done`...; row events call methods with the task) and a **component**: the StatsCard form with an animated progress ring. Menu bar, context menu, `#tag` suggestions, toasts with Undo, a confirmation dialog, DatePicker, Segmented, RadioGroup, Wrap; the list card and the side column glide, the progress tweens. |
| [**Photo Board**](samples/photoboard) | **Drag and drop**: drop images from Explorer/Finder, drag cards out. A keyed `ForEach().wrap()` of cards that **fly to new places** when the sort changes and resize smoothly with the slider; the board color glides; hovering lifts a card. SVG icons, a preview sheet, rename with a prompt, remove with Undo. |
| [**System Monitor**](samples/systemmonitor) | **Tabs** designed in the form: tweened stat cards in a Grid, a smoothly scrolling Canvas chart that cross-fades between styles and colors, settings with NumberField, RadioGroup, Toggle and ColorPicker. In code: a sortable `Table` of processes (confirm before ending one), a `TreeView` of services, CSV export, warning toasts. |
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
tabs for the open files, closing brackets and quotes, a highlight on the matching bracket, and errors underlined
right where they happen, with the message at the end of the line.

**Right-click** the code for a menu with the actions below (Go to Definition, Switch Header/Source, Rename Symbol,
Add Definition, Find, Toggle Comment, Format File, Toggle Breakpoint) and Undo, Redo, Cut, Copy, Paste and
Select All. The click moves the caret first, so the actions apply to the word you clicked on.

### Find your way around

- **Go to definition**: F12 or Ctrl+click. A call jumps to the function's body, a body to its declaration,
  a variable to where it is declared, a class name to the class. `ui_.ok` opens the form in Design mode with
  the **ok** view selected.
- **Hover** a name to see what it is: `pulse::property<std::string> MainWindow::name`,
  `Window::resize(float w, float h) → void`, `Button* ok — view of the form MainWindow`.
- **F4** (or Alt+O; ⌘⌥O on macOS) switches between `mainwindow.hpp` and `mainwindow.cpp`.
- **Ctrl+F** finds in the file (match case, whole word, F3 / Shift+F3 for next / previous);
  **Ctrl+H** also replaces, one at a time or all at once.

### Change code safely

- **Rename** with F2. A local variable is renamed in its file; a class member or a function everywhere in the
  project, including the forms that bind to it, so `on.click: greet` follows the rename to `hello`.
  Comments and strings are left alone.
- **Create definition** with Alt+Enter on a declaration such as `void save(int x = 3) const;` in a header:
  `void MainWindow::save(int x) const { }` is added to the matching `.cpp` and the caret waits in the body.
  If it is already defined, Alt+Enter takes you there.
- **Ctrl+/** comments or uncomments the line or the selected lines.
- **Ctrl+Shift+F** formats the file with clang-format. It is found in PATH, LLVM, Visual Studio
  ("C++ Clang tools"), Qt Creator or Homebrew. A `.clang-format` file in the project is used when present;
  otherwise a style that matches the templates (4 spaces, 110 columns).
- **Ctrl+S** also checks the code for errors, without building: after the first build, Pulse Layer reruns the
  compiler with `-fsyntax-only` (`/Zs` for MSVC) using the exact flags of the build, and the problems appear in
  **Issues** and as squiggles a second or two later.

**Completion** appears as you type: after two letters, after `.`, `->`, `::` or `"`, or with **Ctrl+Space**.
It follows the types of whole expressions, so it keeps up with real code:

- variables of every enclosing scope (locals, parameters, lambda parameters, `for` variables);
- members of your class anywhere in its methods, also those declared further down, and inside constructors
  with member initializers;
- members after `.` and `->` for PulseUI, Pulse and standard types and for your own classes, through chains:
  `ui_.ok->clicked.connect`, `name.get().size()`, `window_.root()->children()`, `th().accent.with_alpha(…)`,
  `tasks[0].title`, `Button("…").primary().flex()`;
- `auto` variables (`auto s = name.get();`, `auto* b = window_.find<Button>("ok");`) and range-`for` elements
  (`for (auto& t : tasks)` → `Task`);
- members inherited from base classes, including PulseUI ones (`class Gauge : public ViewT<Gauge>`);
- scopes: `Alignment::`, `Ui::MainWindow::`, `pulse::`, your enums (`Mode::Edit`) and namespaces;
- the ids of your designed views inside `find<Button>("…")` and `get<Button>("…")`;
- Pulse operators after `stream | `, `#include` paths, and C++ keywords.

Smart touches:

- Choosing a function inserts the parentheses: `show()` directly, `resize(|)` with the caret inside.
- After `void MainWindow::` (outside a function) it offers the declared methods with their parameters,
  ready for a definition.
- **Parameter hints**: after `(` and `,` a hint shows the signature with the current parameter in bold,
  e.g. `resize(float w, **float h**)`. Escape hides it.
- Snippets: `if`, `for`, `foreach`, `while`, `switch`, `lambda`, and in view code `VStack`, `HStack`,
  `Panel`, `Button`, `Text`, with the caret where you continue typing.
- Abbreviations work: `sms` finds `set_min_size`, `size` finds `set_min_size` too.
- Enter between `{}` opens an indented line with the closing brace below it.

Completion is a fast, lightweight C++ reader (tokens, brace scopes and a small type evaluator), not a full
compiler front end. It is made to be instant for everyday UI code.

## Build and run

| | |
|---|---|
| **Build** (Ctrl+B) | Generates a CMake project in `.pulselayer-build/` inside the project folder and compiles it. The first build compiles PulseUI once (about 10–20 s); later builds are incremental and take seconds. |
| **Run** (Ctrl+R) | Builds if needed and starts the program. **Stop** ends it, together with any child processes. |
| **Issues** | MSVC, GCC, Clang and linker errors. Click one to jump to its line. |
| **Application output** | What the running program writes to `std::cout`, `std::cerr` and `printf`, line by line as it is written, also under the debugger. Each run starts with a clean pane. |
| **Build output** | The complete log of CMake, the compiler and the debugger. |

Output appears at once, without `std::endl` or `fflush`: a Debug build includes a small generated
`pulselayer_console.cpp` that turns off stdout buffering when Pulse Layer starts the program (it does nothing
otherwise, and Release builds leave it out). Right-click a pane to copy from it or clear it.

**Debug / Release** — like in Qt Creator, the Code toolbar has a configuration selector, and **Build** (Ctrl+B)
and **Run** (F5, Ctrl+R) follow it:

- **Debug:** Run builds and starts the program under the debugger. Click the left margin of the editor (or press F9)
  to set breakpoints; when the program stops, the line is highlighted, Run becomes **Continue**, step buttons appear
  next to it (step over F10, step into F11, step out Shift+F11), and the **Debug** tab shows the call stack (click a
  frame to go there) and the local variables. **Stop** (Shift+F5) ends the session. The debugger fits the kit: GDB
  for MinGW/GCC, CDB (from the Windows SDK "Debugging Tools") for MSVC, `lldb-dap` on macOS; without one the program
  simply runs. Ctrl+Shift+R runs a Debug build without the debugger.
- **Release:** Build compiles the project optimized in `.pulselayer-build/build-release/` and packages it into
  `release/` next to the project file; Run does the same and starts the packaged program.
  - Windows: `<Project>.exe` with the C/C++ runtime linked in (MSVC `/MT`, MinGW `-static`), plus any other DLL it
    imports that Windows does not have (found by reading its import table).
  - macOS: `<Project>.app`, signed with the identity from ⚙ Settings → Kits (ad hoc when empty), and a `.dmg`.
    With Homebrew LLVM, its `libc++` is linked (the system one lacks symbols that clang 18+ headers use) and copied
    into `Contents/Frameworks`.

**Lists in forms.** Add a **ForEach** from the palette: it comes with a row (a checkbox and a text) shown
with dimmed copies below. Edit the row like any other views, bind **items** to a `pulse::property<std::vector<T>>`
member and bind the row's views to `item.<field>`; events of the row call your methods with the item.
**Tabs**, **Segmented**, **Canvas** (bind its `draw` event to `void draw(pulseui::Painter&, const pulseui::Rect&)`)
and the newer widgets are in the palette too.

**Components.** The palette's **Project** section lists the other forms (insert one to reuse it - it is shown
live on the canvas) and the view classes declared in the project's headers (`class Rating : public
pulseui::ViewT<Rating>`), which appear as placeholders with their constructor arguments and modifiers.

**Preview board** (Design mode toolbar) renders the form in several conditions side by side and keeps them
up to date while you edit.

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
| F5 | Preview (Design) · Run / Continue (Code) |
| F9 · F10 · F11 · Shift+F11 · Shift+F5 | Breakpoint · Step over · Step into · Step out · Stop debugging |
| Ctrl+E | Export the form layout as C++ |
| Ctrl+1 · Ctrl+2 | Design · Code |
| Ctrl+B · Ctrl+R (F5) · Ctrl+Shift+R | Build · Run (in the chosen configuration) · Run without the debugger |
| Ctrl+Space | Code completion |
| F12 · Ctrl+click | Go to definition |
| F2 | Rename |
| Alt+Enter | Create the definition of a declaration |
| F4 · Alt+O | Header ↔ source |
| Ctrl+F · Ctrl+H · F3 | Find · Replace · Next match |
| Ctrl+/ | Comment lines |
| Ctrl+Shift+F | Format with clang-format |
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
| `PULSELAYER_PULSEUI_REF` | `0.4.0` | Tag, branch or commit of PulseUI to download |
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
