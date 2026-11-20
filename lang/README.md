# Extra languages

Every `*.json` file in this folder is loaded at startup (it is copied next to the executable by the build).
A file adds a new language, or overrides strings of an existing one (same `code`).

```json
{
  "code": "es",
  "name": "Español",
  "strings": {
    "tb.new": "Nuevo",
    "tb.save": "Guardar",
    "status.saved": "Guardado: {0}"
  }
}
```

- `{0}` is replaced by an argument (a file name, a view type, ...).
- Missing keys fall back to English, so a partial translation works.
- Get a template with every key and its English text:

```bash
PulseLayer --export-lang template.json
```

Start in a given language: `PulseLayer --lang de`. The language picked in the toolbar is remembered.

Built-in languages live in `src/lang/*.cpp` (English, Русский, Deutsch, Français).
