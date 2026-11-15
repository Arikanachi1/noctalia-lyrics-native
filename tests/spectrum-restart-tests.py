"""Probe real PipeWire connection attempts with a private dummy socket."""
import os
from pathlib import Path
import runpy
import socket
import subprocess
import sys
import tempfile
import time

Frames=runpy.run_path(str(Path(__file__).with_name('spectrum-stream-tests.py')))['Frames']


def main(binary):
    with tempfile.TemporaryDirectory(prefix='ari-spectrum-restart-') as temporary:
        listener=socket.socket(socket.AF_UNIX,socket.SOCK_STREAM)
        try:
            listener.bind(str(Path(temporary)/'pipewire-test-0'))
        except PermissionError:
            listener.close()
            print('SKIP: sandbox prohibits a private Unix socket for the connection restart probe')
            return 77
        listener.listen(4)
        listener.settimeout(.3)
        lease=Path(temporary)/'spectrum.lease'
        def enable():lease.write_text(str(int(time.time()*1000)+2500))
        enable()
        environment=dict(os.environ,XDG_RUNTIME_DIR=temporary,PIPEWIRE_RUNTIME_DIR=temporary,
                         PIPEWIRE_REMOTE='pipewire-test-0')
        process=subprocess.Popen([binary,'--lease',str(lease)],env=environment,
                                 stdout=subprocess.PIPE,stderr=subprocess.DEVNULL,bufsize=0)
        reader=Frames(process.stdout)
        connections=[]
        try:
            reader.until(lambda frame:frame['active'] is True,2)
            connections.append(listener.accept()[0])
            lease.write_text('0')
            reader.until(lambda frame:frame['active'] is False,1)
            began=time.monotonic()
            enable()
            try:
                connections.append(listener.accept()[0])
            except TimeoutError:
                raise AssertionError('Reopen must attempt actual capture immediately, not retain a five-second retry deadline') from None
            print(f'Spectrum restart: second private PipeWire connection in {(time.monotonic()-began)*1000:.1f}ms')
        finally:
            process.terminate();process.wait(timeout=2)
            reader.selector.close();listener.close()
            for connection in connections:connection.close()
    return 0


if __name__=='__main__':
    raise SystemExit(main(sys.argv[1]))
