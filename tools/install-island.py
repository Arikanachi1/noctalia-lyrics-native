#!/usr/bin/env python3
"""Install a built plugin and replace the main bar's clock, with backups."""
import argparse
import difflib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import time
import tomllib


def section(text, name):
    pattern = r'(?m)^\[' + re.escape(name) + r'\][^\n]*\n(?:(?!^\[).*(?:\n|$))*'
    return re.search(pattern, text)


def table_values(text, name, values):
    existing=section(text,name)
    if existing is None:
        text=text.rstrip()+'\n\n['+name+']\n'
        existing=section(text,name)
    body=existing.group()
    for key,value in values.items():
        encoded=json.dumps(value,ensure_ascii=False)
        pattern=r'(?m)^'+re.escape(key)+r'\s*=\s*'
        if re.search(pattern,body):
            suffix=r'\[[\s\S]*?\]' if isinstance(value,list) else r'[^\n]*'
            body=re.sub(pattern+suffix,lambda _:key+' = '+encoded,body,count=1)
        else:
            body=body.rstrip()+'\n'+key+' = '+encoded+'\n'
    return text[:existing.start()]+body+text[existing.end():]


def island_config(text):
    config = tomllib.loads(text)
    center = config.get('bar', {}).get('main', {}).get('center', [])
    if 'clock' not in center and 'ari_lyrics' not in center:
        raise ValueError('bar.main.center has no clock; refusing to change a different layout')
    widget = config.get('widget', {}).get('ari_lyrics')
    if widget is not None and widget.get('type') != 'ari/lyrics:lyrics':
        raise ValueError('widget.ari_lyrics already belongs to another widget')
    updated = ['ari_lyrics' if item == 'clock' else item for item in center]
    updated = list(dict.fromkeys(updated))
    main = section(text, 'bar.main')
    if main is None:
        raise ValueError('Expected an explicit [bar.main] table')
    body, count = re.subn(r'(?m)^center\s*=\s*\[[\s\S]*?\]',
                          'center = ' + json.dumps(updated, ensure_ascii=False), main.group(), count=1)
    if count != 1:
        raise ValueError('Cannot locate the center widget array')
    text = text[:main.start()] + body + text[main.end():]
    text=table_values(text,'widget.ari_lyrics',{
        'type':'ari/lyrics:lyrics','anchor':True,'capsule':True,'capsule_padding':12,
        'capsule_radius':13,'capsule_fill':'surface_variant','capsule_border':'outline','capsule_border_width':0})
    text=table_values(text,'plugin_settings."ari/lyrics"',{
        'player_placement':'attached','player_position':'top_center','player_open_near_click':False})
    tomllib.loads(text)
    return text


def codex_permissions_config(text):
    config=tomllib.loads(text)
    if 'default_permissions' in config:
        raise ValueError('Codex uses named permission profiles; do not mix them with sandbox_mode')
    first=re.search(r'(?m)^\s*\[',text)
    split=first.start() if first else len(text)
    head,tail=text[:split],text[split:]
    if re.search(r'(?m)^sandbox_mode\s*=',head):
        head=re.sub(r'(?m)^sandbox_mode\s*=[^\n]*','sandbox_mode = "workspace-write"',head)
    else:
        head='sandbox_mode = "workspace-write"\n'+head
    result=table_values(head+tail,'sandbox_workspace_write',{'writable_roots':['/']})
    tomllib.loads(result)
    return result


def save_codex_permissions(path):
    path=Path(path)
    original=path.read_text() if path.exists() else ''
    updated=codex_permissions_config(original)
    if updated==original:return None
    path.parent.mkdir(parents=True,exist_ok=True)
    backup=None
    if path.exists():
        backup=path.with_name(path.name+'.before-island-permissions-'+str(time.time_ns()))
        shutil.copy2(path,backup)
    descriptor,name=tempfile.mkstemp(prefix='.codex-permissions-',dir=path.parent)
    try:
        with os.fdopen(descriptor,'w') as target:target.write(updated)
        if path.exists():shutil.copymode(path,name)
        os.replace(name,path)
    finally:
        Path(name).unlink(missing_ok=True)
    return backup


def atomic_copy(source, destination):
    # A running ELF binary cannot be opened for writing (ETXTBSY). Replacing
    # its directory entry keeps the old inode alive for the running process.
    destination = Path(destination)
    descriptor, name = tempfile.mkstemp(prefix='.island-install-', dir=destination.parent)
    os.close(descriptor)
    try:
        shutil.copy2(source, name)
        os.replace(name, destination)
    finally:
        Path(name).unlink(missing_ok=True)
    return str(destination)


def install_runtime(staged, prefix):
    shutil.copytree(staged, prefix, dirs_exist_ok=True, copy_function=atomic_copy)


def backup_runtime(prefix, stamp):
    # Sibling plugin directories containing plugin.toml can be discovered as
    # another copy of the same plugin. Keep archives outside the plugin root.
    directory=prefix.parent.parent/'plugin-backups'
    directory.mkdir(parents=True,exist_ok=True)
    backup=directory/(prefix.name+'.before-island-'+stamp)
    shutil.copytree(prefix,backup)
    marker=prefix.name+'.before-island-'
    for old in prefix.parent.glob(marker+'*'):
        if old.name[len(marker):].isdigit() and old.is_dir() and not old.is_symlink():
            old.rename(directory/old.name)
    return backup


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, default=Path('build'))
    parser.add_argument('--prefix', type=Path, default=Path.home()/'.local/share/noctalia/plugins/ari-lyrics')
    parser.add_argument('--config', type=Path, default=Path.home()/'.config/noctalia/config.toml')
    parser.add_argument('--dry-run', action='store_true', help='Print only the proposed config change')
    parser.add_argument('--no-reload', action='store_true', help='Install without contacting the running shell')
    parser.add_argument('--persist-codex-permissions',action='store_true',
                        help='Persist the explicitly requested workspace-write access to / for future Codex sessions')
    parser.add_argument('--codex-config',type=Path,
                        default=Path(os.environ.get('CODEX_HOME',str(Path.home()/'.codex')))/'config.toml')
    args = parser.parse_args()
    original = args.config.read_text()
    updated = island_config(original)
    if args.dry_run:
        print(''.join(difflib.unified_diff(original.splitlines(True), updated.splitlines(True),
                                        fromfile=str(args.config), tofile='music-island')), end='')
        if args.persist_codex_permissions:
            original_codex=args.codex_config.read_text() if args.codex_config.exists() else ''
            updated_codex=codex_permissions_config(original_codex)
            print(''.join(difflib.unified_diff(original_codex.splitlines(True),updated_codex.splitlines(True),
                                            fromfile=str(args.codex_config),tofile='persistent-codex-permissions')),end='')
        return
    if not (args.build_dir/'ari-lyrics').is_file():
        parser.error('Build ari-lyrics before installing')
    with tempfile.TemporaryDirectory(prefix='ari-lyrics-install-') as temporary:
        staged = Path(temporary)/'plugin'
        subprocess.run(['cmake', '--install', str(args.build_dir.resolve()), '--prefix', str(staged)], check=True)
        proposal = Path(temporary)/'config.toml'
        proposal.write_text(updated)
        subprocess.run(['noctalia', 'plugins', 'lint', str(staged)], check=True)
        subprocess.run(['noctalia', 'config', 'validate', str(proposal)], check=True)
        stamp = str(time.time_ns())
        config_backup = args.config.with_name(args.config.name+'.before-island-'+stamp)
        shutil.copy2(args.config, config_backup)
        if args.prefix.exists():
            backup=backup_runtime(args.prefix,stamp)
            print('Runtime backup:', backup)
        print('Config backup:', config_backup)
        install_runtime(staged, args.prefix)
        # Replace in the same directory to keep the update atomic.
        descriptor, name = tempfile.mkstemp(prefix='.music-island-', dir=args.config.parent)
        try:
            with os.fdopen(descriptor, 'w') as target:
                target.write(updated)
            shutil.copymode(args.config, name)
            os.replace(name, args.config)
        finally:
            Path(name).unlink(missing_ok=True)
    if args.persist_codex_permissions:
        backup=save_codex_permissions(args.codex_config)
        if backup:print('Codex config backup:',backup)
        print('Codex writable root / saved for future sessions; managed policies still apply.')
    if not args.no_reload:
        for command in [['plugins', 'disable', 'ari/lyrics'], ['plugins', 'enable', 'ari/lyrics'], ['config-reload']]:
            result = subprocess.run(['noctalia', 'msg', *command], check=False)
            if result.returncode:
                print('Files installed; reload Noctalia manually. Backups are listed above.')
                return 1
    print('Music island installed:', args.prefix)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
