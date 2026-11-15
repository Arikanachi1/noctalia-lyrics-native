import importlib.util
from pathlib import Path
import tomllib
import unittest
import shutil
import subprocess
import tempfile

spec = importlib.util.spec_from_file_location("install_island", Path(__file__).parents[1] / "tools/install-island.py")
installer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(installer)


class ConfigTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which('noctalia'), 'Noctalia validator unavailable')
    def test_generated_config_passes_installed_noctalia_validator(self):
        with tempfile.TemporaryDirectory() as temporary:
            config=Path(temporary)/'config.toml'
            config.write_text(installer.island_config('[plugins]\nenabled=["ari/lyrics"]\n[bar]\norder=["main"]\n[bar.main]\nposition="top"\nenabled=true\ncenter=["clock"]\n'))
            result=subprocess.run(['noctalia','config','validate',str(config)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)

    def test_replace_only_center_clock_and_keep_other_widgets(self):
        source = '''# user settings
[bar.main]
center = ["other", "clock"] # center
end = ["battery"]
[bar.tools]
center = ["resources"]
[widget.clock]
format = "{:%H:%M}"
'''
        result = installer.island_config(source)
        data = tomllib.loads(result)
        self.assertEqual(data['bar']['main']['center'], ['other', 'ari_lyrics'])
        self.assertEqual(data['bar']['tools']['center'], ['resources'])
        self.assertEqual(data['widget']['clock']['format'], '{:%H:%M}')
        self.assertEqual(data['widget']['ari_lyrics']['type'], 'ari/lyrics:lyrics')
        self.assertTrue(data['widget']['ari_lyrics']['capsule'])
        self.assertEqual(data['widget']['ari_lyrics']['capsule_padding'], 12)
        self.assertEqual(data['widget']['ari_lyrics']['capsule_fill'], 'surface_variant')
        self.assertFalse(data['plugin_settings']['ari/lyrics']['player_open_near_click'])
        self.assertEqual(data['plugin_settings']['ari/lyrics']['player_position'], 'top_center')
        self.assertIn('# user settings', result)
        self.assertEqual(installer.island_config(result), result)

    def test_missing_clock_and_conflicting_widget_are_rejected(self):
        with self.assertRaises(ValueError):
            installer.island_config('[bar.main]\ncenter=["media"]\n')
        with self.assertRaises(ValueError):
            installer.island_config('[bar.main]\ncenter=["clock"]\n[widget.ari_lyrics]\ntype="other"\n')

    def test_persist_requested_codex_roots_and_keep_other_config(self):
        source='model="existing"\nsandbox_mode="read-only"\n[sandbox_workspace_write]\nnetwork_access=false\nwritable_roots=["/old"]\n[projects."/project"]\ntrust_level="trusted"\n'
        result=installer.codex_permissions_config(source)
        parsed=tomllib.loads(result)
        self.assertEqual(parsed['sandbox_mode'],'workspace-write')
        self.assertEqual(parsed['sandbox_workspace_write']['writable_roots'],['/'])
        self.assertFalse(parsed['sandbox_workspace_write']['network_access'])
        self.assertEqual(parsed['model'],'existing')
        self.assertEqual(parsed['projects']['/project']['trust_level'],'trusted')
        self.assertEqual(installer.codex_permissions_config(result),result)

    def test_save_codex_permissions_creates_exact_backup(self):
        with tempfile.TemporaryDirectory() as temporary:
            config=Path(temporary)/'config.toml'
            original='model="existing"\n[tui]\nflag=true\n'
            config.write_text(original)
            backup=installer.save_codex_permissions(config)
            self.assertEqual(backup.read_text(),original)
            self.assertEqual(tomllib.loads(config.read_text())['sandbox_workspace_write']['writable_roots'],['/'])


class RuntimeTests(unittest.TestCase):
    def test_backups_do_not_register_as_duplicate_plugins(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary)
            prefix=root/'plugins/ari-lyrics';prefix.mkdir(parents=True)
            (prefix/'plugin.toml').write_text('id="ari/lyrics"\n')
            old=prefix.with_name('ari-lyrics.before-island-123');old.mkdir()
            (old/'plugin.toml').write_text('id="ari/lyrics"\n')
            backup=installer.backup_runtime(prefix,'456')
            self.assertNotEqual(backup.parent,prefix.parent,'Backups must be outside the scanned plugin directory')
            self.assertFalse(old.exists(),'Previous installer backups must also leave the scanned directory')
            self.assertTrue((backup.parent/old.name/'plugin.toml').is_file())
            self.assertEqual((backup/'plugin.toml').read_text(),'id="ari/lyrics"\n')

    def test_update_executing_binary_without_overwriting_its_inode(self):
        with tempfile.TemporaryDirectory(prefix='ari-running-install-') as temporary:
            root = Path(temporary)
            staged, target = root/'staged', root/'installed'
            (staged/'bin').mkdir(parents=True)
            (target/'bin').mkdir(parents=True)
            executable = target/'bin/ari-lyrics'
            shutil.copy2(shutil.which('sleep'), executable)
            shutil.copy2(shutil.which('true'), staged/'bin/ari-lyrics')
            (staged/'widget.luau').write_text('new widget')
            (target/'unrelated').write_text('keep')
            running = subprocess.Popen([str(executable), '30'])
            try:
                installer.install_runtime(staged, target)
                self.assertIsNone(running.poll(), 'Replacing the file must preserve the running process')
                self.assertEqual(subprocess.run([str(executable)], timeout=2).returncode, 0)
                self.assertEqual((target/'unrelated').read_text(), 'keep')
                self.assertEqual((target/'widget.luau').read_text(), 'new widget')
                self.assertEqual(executable.stat().st_mode & 0o111, 0o111)
            finally:
                running.terminate()
                running.wait(timeout=2)


if __name__ == '__main__':
    unittest.main()
