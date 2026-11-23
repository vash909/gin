"""Private stdlib PTY ownership for development fixtures only.

The exec'd single-thread owner keeps the controlling session alive until explicit
close. Only that owner signals its unreaped child; native PIDs are observational.
"""
import fcntl
import json
import os
import select
import signal
import socket
import subprocess
import sys
import termios
import time


def read_pty(fd, timeout):
    """None means not ready; empty bytes mean observed EOF, not retry."""
    if not select.select([fd], [], [], timeout)[0]:
        return None
    return os.read(fd, 16384)


def drain_pty(fd: int, transcript: bytes, deadline: float,
              cap: int = 65536) -> bytes:
    """Drain available bytes within the original deadline, including real EOF."""
    assert len(transcript) <= cap, 'PTY transcript cap exceeded (omitted)'
    while True:
        if time.monotonic() >= deadline:
            raise TimeoutError('PTY drain deadline exceeded')
        chunk = read_pty(fd, .01)
        if chunk is None or not chunk:
            break
        assert len(transcript) + len(chunk) <= cap, 'PTY transcript cap exceeded (omitted)'
        transcript += chunk
    return transcript


class PtyProcess:
    """Popen-like actual-child status with separately owned session lifetime."""
    def __init__(self, argv, slave, *, cwd=None, env=None, stderr=None,
                 pass_fds=()):
        self.returncode = None
        self.native_pid = None
        self._owner = None
        self._socket = None
        self._buffer = b''
        borrowed_slave = slave
        if slave <= 2:
            slave = fcntl.fcntl(slave, fcntl.F_DUPFD_CLOEXEC, 3)
        parent = control = None
        try:
            parent, control = socket.socketpair()
        except BaseException:
            if slave != borrowed_slave:
                os.close(slave)
            raise
        self._socket = parent
        try:
            self._owner = subprocess.Popen(
                [sys.executable, '-B', __file__, str(control.fileno()),
                 str(slave), json.dumps(list(argv)), json.dumps(list(pass_fds))],
                cwd=cwd, env=env, stdin=subprocess.DEVNULL, stdout=slave,
                stderr=slave if stderr is None else stderr,
                pass_fds=tuple(set((slave, control.fileno(), *pass_fds))),
                start_new_session=True)
            control.close()
            parent.setblocking(False)
            deadline = time.monotonic() + 2
            while self.native_pid is None:
                self._receive(deadline)
        except BaseException:
            control.close()
            self.close()
            raise
        finally:
            if slave != borrowed_slave:
                os.close(slave)

    @property
    def pid(self):
        return self.native_pid

    def _receive(self, deadline):
        connection = self._socket
        if connection is None:
            raise RuntimeError('PTY owner is closed')
        if not select.select([connection], [], [], max(0, deadline-time.monotonic()))[0]:
            raise subprocess.TimeoutExpired('PTY owner', max(0, deadline-time.monotonic()))
        chunk = connection.recv(4096)
        if not chunk:
            raise RuntimeError('PTY owner closed before status')
        self._buffer += chunk
        if len(self._buffer) > 8192:
            raise RuntimeError('PTY owner status cap')
        while b'\n' in self._buffer:
            line, self._buffer = self._buffer.split(b'\n', 1)
            kind, value = json.loads(line)
            if kind == 'pid':
                self.native_pid = value
            elif kind == 'exit':
                self.returncode = value
            else:
                raise RuntimeError('PTY native setup failed: ' + value)

    def poll(self):
        if self.returncode is None and self._socket is not None:
            while select.select([self._socket], [], [], 0)[0]:
                self._receive(time.monotonic() + .1)
                if self.returncode is not None:
                    break
        return self.returncode

    def wait(self, timeout=None):
        deadline = time.monotonic() + (30 if timeout is None else timeout)
        while self.returncode is None:
            self._receive(deadline)
        return self.returncode

    def _command(self, command):
        if self._socket is None:
            return
        deadline = time.monotonic() + 1
        data = command.encode()
        while data:
            if not select.select([], [self._socket], [], max(0,deadline-time.monotonic()))[1]:
                raise TimeoutError('PTY owner control deadline')
            try:
                count = self._socket.send(data)
            except BlockingIOError:
                continue
            if not count:
                raise RuntimeError('PTY owner control EOF')
            data = data[count:]

    def send_signal(self, number):
        self._command('S %d\n' % number)

    def kill(self):
        self.send_signal(signal.SIGKILL)

    def close(self):
        if self._socket is None:
            return
        try:
            if self._owner is not None and self._owner.poll() is None:
                try:
                    self._command('C\n')
                except (BrokenPipeError, ConnectionResetError):
                    pass  # Setup failure may already have closed the owner.
                finally:
                    # EOF is also an owner-side kill/reap request.
                    self._socket.close()
                    self._owner.wait(timeout=3)
        finally:
            self._socket.close()
            self._socket = None


def _serve(control_fd, slave, argv, passed):
    control = socket.socket(fileno=control_fd)
    child = None
    reported = False
    def report(kind, value):
        control.sendall((json.dumps([kind, value])+'\n').encode())
    try:
        control.settimeout(1)
        fcntl.ioctl(slave, termios.TIOCSCTTY, 0)
        child = subprocess.Popen(argv, stdin=subprocess.DEVNULL,
                                 pass_fds=tuple(set((slave, *passed))))
        report('pid', child.pid)
        # Additional borrowed descriptors belong to the actual child, not to
        # the retained owner (notably the receipt pipe's write endpoint).
        for fd in set(passed):
            if fd != slave and fd > 2:
                os.close(fd)
        pending = b''
        while True:
            rc = child.poll()  # Reap before reporting; no native PID reuse race.
            if rc is not None and not reported:
                report('exit', rc)
                reported = True
            if not select.select([control], [], [], .01)[0]:
                continue
            chunk = control.recv(256)
            if not chunk:
                break
            pending += chunk
            if len(pending) > 1024:
                raise RuntimeError('PTY owner control cap')
            while b'\n' in pending:
                line, pending = pending.split(b'\n', 1)
                if line == b'C':
                    return
                if not line.startswith(b'S '):
                    raise RuntimeError('PTY owner invalid control')
                number = int(line[2:])
                # Exited but unreaped children retain their PID. A reaped child
                # is never signaled. No other thread can reap between these.
                if child.poll() is None:
                    child.send_signal(number)
    except BaseException as exc:
        try:
            report('error', type(exc).__name__)
        except OSError:
            pass
    finally:
        if child is not None:
            if child.poll() is None:
                child.kill()
            child.wait(timeout=2)
        control.close()


if __name__ == '__main__':
    _serve(int(sys.argv[1]), int(sys.argv[2]), json.loads(sys.argv[3]),
           json.loads(sys.argv[4]))
