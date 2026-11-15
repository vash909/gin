"""Private two-pipe deterministic owner clock; ACK is not owner completion."""
import os
import select
import time


class OwnerClock:
    def __init__(self, testcase):
        self.testcase = testcase
        self._fds = set()
        try:
            self.command_reader, self.command_writer = os.pipe()
            self._fds.update((self.command_reader, self.command_writer))
            self.ack_reader, self.ack_writer = os.pipe()
            self._fds.update((self.ack_reader, self.ack_writer))
            for fd in self._fds:
                os.set_blocking(fd, False)
        except BaseException:
            self.close()
            raise
        self.child_fds = (self.command_reader, self.ack_writer)
        self._ack = bytearray()
        self._state = ''

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.close()

    def environment(self):
        return dict(os.environ, GIN_OWNER_FAULT='clock-control',
                    GIN_OWNER_CLOCK_COMMAND_FD=str(self.command_reader),
                    GIN_OWNER_CLOCK_ACK_FD=str(self.ack_writer))

    def parent_after_spawn(self):
        for fd in self.child_fds:
            if fd in self._fds:
                os.close(fd)
                self._fds.remove(fd)

    def advance(self, proc, command, timeout):
        self.testcase.assertTrue((not self._state and command in ('A', 'E')) or
                                 (self._state == 'A' and command == 'B'),
                                 'clock command order/replay')
        record = (command + '\n').encode('ascii')
        self.testcase.assertEqual(os.write(self.command_writer, record), 2, 'clock command write')
        self.wait_ack(proc, command, timeout)
        self._state = command

    def wait_ack(self, proc, command, timeout):
        deadline = time.monotonic() + timeout
        expected = (command + '\n').encode('ascii')
        while True:
            # Sample process and independent real deadline on every iteration.
            result = proc.poll()
            remaining = deadline - time.monotonic()
            self.testcase.assertGreater(remaining, 0, 'clock ACK timeout (hook absent or setup I/O)')
            try:
                part = os.read(self.ack_reader, 64)
            except BlockingIOError:
                part = None
            if part is not None:
                self.testcase.assertNotEqual(part, b'', 'clock ACK EOF/setup I/O')
                self._ack.extend(part)
                self.testcase.assertTrue(expected.startswith(self._ack), 'clock ACK surplus/mismatch/replay')
                if len(self._ack) == 2:
                    self._ack.clear()
                    return
            self.testcase.assertIsNone(result, 'owner exited before clock ACK/setup I/O')
            select.select([self.ack_reader], [], [], min(.02, remaining))

    def close(self):
        error = None
        for fd in tuple(self._fds):
            try:
                os.close(fd)
            except OSError as exc:
                if error is None:
                    error = exc
            finally:
                self._fds.remove(fd)
        if error is not None:
            raise error
