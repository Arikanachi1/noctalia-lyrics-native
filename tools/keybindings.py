#!/usr/bin/env python3
"""Generate compositor shortcuts from the project's keybindings.toml (Python 3.11+)."""
import argparse
import base64
import json
from pathlib import Path
import re
import tomllib

MODIFIERS = {'super': 'Super', 'ctrl': 'Ctrl', 'control': 'Ctrl', 'alt': 'Alt', 'shift': 'Shift'}
ORDER = ['Super', 'Ctrl', 'Alt', 'Shift']
TARGET_MODIFIERS = {
    'hyprland-lua': {'Super': 'SUPER', 'Ctrl': 'CONTROL', 'Alt': 'ALT', 'Shift': 'SHIFT'},
    'sway': {'Super': 'Mod4', 'Ctrl': 'Ctrl', 'Alt': 'Mod1', 'Shift': 'Shift'},
    'niri': {name: name for name in ORDER},
}


def chord(value):
    if not isinstance(value, str):
        raise ValueError('shortcut must be a string')
    parts = value.split('+')
    mods = []
    for part in parts[:-1]:
        modifier = MODIFIERS.get(part.lower())
        if modifier is None or modifier in mods:
            raise ValueError(f'invalid or duplicate modifier: {part}')
        mods.append(modifier)
    key = parts[-1]
    if not re.fullmatch(r'[A-Za-z0-9_]+', key):
        raise ValueError(f'invalid key: {key!r}; use a keysym such as Y, F8, space or plus')
    if len(key) == 1:
        key = key.lower()
    return [name for name in ORDER if name in mods], key


def render(config, target):
    if target not in TARGET_MODIFIERS:
        raise ValueError(f'unknown target: {target}')
    bindings = config.get('bindings')
    if not isinstance(bindings, dict) or not bindings:
        raise ValueError('expected nonempty [bindings.*] tables')
    lines = ['-- Generated from keybindings.toml; edit that file.' if target == 'hyprland-lua'
             else '// Generated from keybindings.toml; edit that file.' if target == 'niri'
             else '# Generated from keybindings.toml; edit that file.']
    if target == 'niri':
        lines.append('binds {')
    used = set()
    for name, binding in bindings.items():
        if not isinstance(binding, dict):
            raise ValueError(f'{name}: expected a table')
        enabled = binding.get('enabled', True)
        if not isinstance(enabled, bool):
            raise ValueError(f'{name}: enabled must be true or false')
        if not enabled:
            continue
        mods, key = chord(binding.get('shortcut'))
        identity = (tuple(mods), key.lower())
        if identity in used:
            raise ValueError(f'{name}: duplicate shortcut {binding["shortcut"]}')
        used.add(identity)
        command = binding.get('command')
        if not isinstance(command, str) or not command.strip() or any(ord(c) < 32 for c in command):
            raise ValueError(f'{name}: command must be a nonempty single line')
        mapped = [TARGET_MODIFIERS[target][m] for m in mods]
        if target == 'hyprland-lua':
            shortcut = ' + '.join(mapped + [key.upper() if len(key) == 1 else key])
            # UTF-8 literals work in both Lua and Luau; JSON quotes these strings safely.
            lines.append(f'hl.bind({json.dumps(shortcut, ensure_ascii=False)}, hl.dsp.exec_cmd({json.dumps(command, ensure_ascii=False)}))')
        elif target == 'sway':
            # Sway parses commas/semicolons as commands and expands $variables.
            # Transport UTF-8 as base64 so user shell syntax survives both parsers.
            payload = base64.b64encode(command.encode('utf-8')).decode('ascii')
            lines.append(f"bindsym {'+'.join(mapped + [key])} exec sh -c 'printf %s {payload} | base64 -d | sh'")
        else:
            # A shell preserves the same command syntax across all three backends.
            lines.append(f'    {json.dumps("+".join(mapped + [key]))} {{ spawn "sh" "-c" {json.dumps(command, ensure_ascii=False)}; }}')
    if target == 'niri':
        lines.append('}')
    return '\n'.join(lines) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config', type=Path, default=Path(__file__).resolve().parents[1]/'keybindings.toml')
    parser.add_argument('--target', required=True, choices=TARGET_MODIFIERS)
    parser.add_argument('--output', type=Path, help='Write a generated file; otherwise print to stdout')
    args = parser.parse_args()
    try:
        result = render(tomllib.loads(args.config.read_text()), args.target)
        if args.output:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(result)
        else:
            print(result, end='')
    except (OSError, ValueError) as error:
        parser.exit(1, f'keybindings: {error}\n')


if __name__ == '__main__':
    main()
