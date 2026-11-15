"""Exercise the real companion's lease timing without accessing a sound server."""
import json
import os
from pathlib import Path
import selectors
import subprocess
import sys
import tempfile
import time


class Frames:
    def __init__(self, pipe):
        self.pipe=pipe
        self.buffer=b''
        self.selector=selectors.DefaultSelector()
        self.selector.register(pipe,selectors.EVENT_READ)

    def until(self, predicate, timeout):
        deadline=time.monotonic()+timeout
        while True:
            while b'\n' in self.buffer:
                line,self.buffer=self.buffer.split(b'\n',1)
                frame=json.loads(line)
                if predicate(frame):return frame
            remaining=deadline-time.monotonic()
            if remaining<=0 or not self.selector.select(remaining):
                raise TimeoutError('Timed out waiting for spectrum state')
            chunk=os.read(self.pipe.fileno(),65536)
            if not chunk:raise AssertionError('Spectrum companion exited unexpectedly')
            self.buffer+=chunk


def main(binary):
    with tempfile.TemporaryDirectory(prefix='ari-spectrum-stream-') as temporary:
        lease=Path(temporary)/'spectrum.lease'
        def enable():lease.write_text(str(int(time.time()*1000)+2500))
        enable()
        environment=dict(os.environ,XDG_RUNTIME_DIR=temporary,PIPEWIRE_REMOTE='ari-spectrum-test-no-server')
        process=subprocess.Popen([binary,'--lease',str(lease)],env=environment,
                                 stdout=subprocess.PIPE,stderr=subprocess.DEVNULL,bufsize=0)
        reader=Frames(process.stdout)
        try:
            first=reader.until(lambda frame:True,2)
            assert len(first['bands'])==32, 'Native spectrum must supply all 32 independent frequency bands'
            assert first['active'] is True and first['available'] is False
            # writeFile truncates before writing a refreshed deadline. A brief
            # empty read must not tear down the capture on every heartbeat.
            lease.write_text('')
            try:
                reader.until(lambda frame:frame['active'] is False,.06)
            except TimeoutError:
                pass
            else:
                raise AssertionError('Transient lease write must not interrupt a still-valid capture')
            enable()
            lease.write_text('0')
            reader.until(lambda frame:frame['active'] is False,1)
            began=time.monotonic()
            enable()
            reader.until(lambda frame:frame['active'] is True,.18)
            latency=time.monotonic()-began
            # The old 250ms polling interval always missed this bound after
            # acknowledging close. File notification must wake it immediately.
            assert latency<.18, 'Reopening must not wait for the next 250ms lease poll'
            print(f'Spectrum stream: 32 bands, immediate lease wake-up ({latency*1000:.1f}ms), close acknowledgement')
        finally:
            reader.selector.close()
            process.terminate()
            process.wait(timeout=2)


if __name__=='__main__':
    main(sys.argv[1])
