# Portable lyrics keybindings

Goal: one editable keybindings.toml in the project, with commands and portable key chords.

- Add a Python 3.11 generator with validation, duplicate detection and disabled entries.
- Export Hyprland Lua, Sway and Niri configuration for Noctalia setups.
- Test edited chords, quoting, invalid input, duplicate shortcuts and disabled bindings.
- Document regeneration and connect a generated Lua file to this user's Hyprland config.

Global shortcut registration remains the compositor/desktop's responsibility. Exporting does not execute commands. Mouse handlers remain in the plugin runtime.
