import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('keybindings', ROOT/'tools/keybindings.py')
k = importlib.util.module_from_spec(spec)
spec.loader.exec_module(k)

class BindingsTests(unittest.TestCase):
    def config(self, shortcut='Super+Shift+Y', command='noctalia msg panel-toggle ari/lyrics:search'):
        return {'bindings': {'search': {'shortcut': shortcut, 'command': command}}}

    def test_defaults_export_to_both_compositors(self):
        import tomllib
        config = tomllib.loads((ROOT/'keybindings.toml').read_text())
        lua = k.render(config, 'hyprland-lua')
        sway = k.render(config, 'sway')
        self.assertIn('hl.bind("SUPER + Y"', lua)
        self.assertIn('ari/lyrics:panel', lua)
        self.assertIn('bindsym Mod4+Shift+y exec ', sway)

    def test_edited_shortcut_and_disabled_entry(self):
        config = self.config('Ctrl+Alt+F8')
        self.assertIn('CONTROL + ALT + F8', k.render(config, 'hyprland-lua'))
        config['bindings']['search']['enabled'] = False
        self.assertNotIn('hl.bind', k.render(config, 'hyprland-lua'))

    def test_quotes_are_preserved_in_lua_command(self):
        output = k.render(self.config(command='notify-send "Lyrics" "$HOME"'), 'hyprland-lua')
        self.assertIn(r'notify-send \"Lyrics\" \"$HOME\"', output)

    def test_invalid_and_duplicate_shortcuts_rejected(self):
        for chord in ['Super+', 'Super+bad key', 'Unknown+Y', 'Super+Super+Y', 'Super+Y+Z']:
            with self.subTest(chord=chord), self.assertRaises(ValueError):
                k.render(self.config(chord), 'sway')
        config = self.config()
        config['bindings']['other'] = {'shortcut': 'Shift+Super+y', 'command': 'true'}
        with self.assertRaises(ValueError):
            k.render(config, 'sway')

    def test_line_injection_and_bad_types_rejected(self):
        for command in ['echo hi\nbindsym x exec bad', '', 42]:
            with self.assertRaises(ValueError):
                k.render(self.config(command=command), 'sway')
        config = self.config()
        config['bindings']['search']['enabled'] = 'false'
        with self.assertRaises(ValueError):
            k.render(config, 'sway')

    def test_sway_preserves_shell_punctuation_and_variables(self):
        import base64
        import os
        import subprocess
        command = 'printf "%s" "$LYRICS_TEST_TEXT"; printf "%s" ",; слова"'
        output = k.render(self.config(command=command), 'sway')
        encoded = base64.b64encode(command.encode()).decode()
        self.assertIn(encoded, output)
        shell = output.split(' exec ', 1)[1].strip()
        self.assertNotIn('$', shell)
        self.assertNotIn(',', shell)
        self.assertNotIn(';', shell)
        result = subprocess.run(shell, shell=True, check=True, capture_output=True,
                                text=True, env={**os.environ, 'LYRICS_TEST_TEXT': 'quoted "value"'})
        self.assertEqual(result.stdout, 'quoted "value",; слова')

    def test_niri_quotes_numeric_key(self):
        output = k.render(self.config(shortcut='1'), 'niri')
        self.assertIn('"1" { spawn', output)

    def test_niri_output_keeps_command(self):
        output = k.render(self.config(), 'niri')
        self.assertIn('noctalia msg panel-toggle ari/lyrics:search', output)
        self.assertIn('"Super+Shift+y" { spawn', output)

if __name__ == '__main__':
    unittest.main()
