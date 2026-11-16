#!/usr/bin/env python3
"""Explicitly authorized one-message smoke of an enrolled ordinary gin CLI.

Development tool only. No login, IRC connection, JOIN, automatic retry or default
live traffic. The operator must select an already joined plaintext test channel.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import select
import signal
import stat
import subprocess
import tempfile
import time
import uuid
from urllib.parse import urlsplit


def arguments(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--authorize-plaintext-send', action='store_true',
                        help='Authorize exactly one persisted test message; never auto-retried')
    for name in ('gin', 'root', 'origin', 'network', 'target', 'work-dir'):
        parser.add_argument('--' + name, required=True)
    args = parser.parse_args(argv)
    if not args.authorize_plaintext_send:
        parser.error('Explicit --authorize-plaintext-send is required before any I/O')
    origin = urlsplit(args.origin)
    try:
        valid_port = origin.port is None or 1 <= origin.port <= 65535
    except ValueError:
        valid_port = False
    if (origin.scheme != 'https' or not origin.hostname or origin.username is not None or
            origin.password is not None or origin.path or origin.query or origin.fragment or
            not valid_port or any(c in args.origin for c in '\r\n\x00')):
        parser.error('Origin must be an HTTPS authority without userinfo/path/query/fragment')
    if not re.fullmatch(r'[a-z][a-z0-9-]{0,47}', args.network):
        parser.error('This narrow smoke requires a lowercase ASCII network slug')
    if not re.fullmatch(r'#[a-z][a-z0-9-]{0,47}', args.target):
        parser.error('This narrow smoke requires a lowercase ASCII #channel')
    for name in ('gin', 'root', 'work_dir'):
        value = getattr(args, name)
        if (not Path(value).is_absolute() or any(c in value for c in '\r\n\x00') or
                '..' in Path(value).parts):
            parser.error('Binary/root/work paths must be absolute without parent traversal or line breaks')
    return args


def private_directory(path, owner_only=True):
    fd = os.open('/', os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
    try:
        for component in Path(path).parts[1:]:
            following = os.open(component, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW,
                                dir_fd=fd)
            os.close(fd)
            fd = following
            meta = os.fstat(fd)
            if meta.st_uid not in (0, os.getuid()) or meta.st_mode & 0o022:
                raise ValueError('Unsafe directory ancestor')
        meta = os.fstat(fd)
        if owner_only and (meta.st_uid != os.getuid() or stat.S_IMODE(meta.st_mode) != 0o700):
            raise ValueError('Directory must be owner-only 0700')
        return fd
    except BaseException:
        os.close(fd)
        raise


def private_digest(path, directory):
    fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK, dir_fd=directory)
    with os.fdopen(fd, 'rb') as stream:
        meta = os.fstat(stream.fileno())
        if not (stat.S_ISREG(meta.st_mode) and meta.st_uid == os.getuid() and
                meta.st_nlink == 1 and stat.S_IMODE(meta.st_mode) == 0o600):
            raise ValueError('Unsafe enrolled credential metadata')
        data = stream.read(16385)
        if len(data) > 16384:
            raise ValueError('Oversized enrolled credential')
        return hashlib.sha256(data).digest()


def smoke(args):
    result = {'passed': False, 'scope': 'One explicit plaintext SEND/persisted receipt/log receive; not recipient delivery'}
    descriptors = set()
    child = None
    fifo = -1
    buffers = {}
    phase = 'private-setup'
    try:
        root_fd = private_directory(args.root)
        descriptors.add(root_fd)
        credential_hash = private_digest('.gin.credentials', root_fd)
        work_fd = private_directory(args.work_dir)
        try:
            meta = os.fstat(work_fd)
            if meta.st_uid != os.getuid() or stat.S_IMODE(meta.st_mode) != 0o700:
                raise ValueError('Work directory must be owner-only 0700')
        finally:
            os.close(work_fd)
        window = Path(args.root) / args.network / args.target[1:]
        out = window / 'out'
        def output():
            directory = private_directory(window)
            try:
                fd = os.open('out', os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK, dir_fd=directory)
            finally:
                os.close(directory)
            with os.fdopen(fd, 'rb') as stream:
                meta = os.fstat(stream.fileno())
                if (not stat.S_ISREG(meta.st_mode) or meta.st_uid != os.getuid() or
                        meta.st_nlink != 1 or stat.S_IMODE(meta.st_mode) != 0o600):
                    raise ValueError('Unsafe selected output')
                data = stream.read(8388609)
                if len(data) > 8388608:
                    raise ValueError('Selected output exceeds smoke observation limit')
                return data, meta.st_ino
        try:
            prefix, inode = output()
        except FileNotFoundError:
            prefix, inode = b'', None
        def pipe():
            pair = os.pipe()
            descriptors.update(pair)
            for fd in pair:
                os.set_blocking(fd, False)
            return pair
        ready, ready_writer = pipe()
        operations, operations_writer = pipe()
        def line(fd, seconds):
            buffer = buffers.setdefault(fd, bytearray())
            assert child is not None
            deadline = time.monotonic() + seconds
            while time.monotonic() < deadline:
                if b'\n' in buffer:
                    head, _, tail = buffer.partition(b'\n')
                    buffer[:] = tail
                    return head.decode('ascii')
                if child.poll() is not None:
                    raise RuntimeError('Owner exited before receipt')
                if select.select([fd], [], [], .1)[0]:
                    part = os.read(fd, 1024)
                    if not part:
                        raise RuntimeError('Owner receipt EOF')
                    buffer.extend(part)
                    if len(buffer) > 4096:
                        raise ValueError('Oversized receipt buffer')
            raise TimeoutError('Independent parent receipt deadline')
        with tempfile.TemporaryDirectory(prefix='gin-live-', dir=args.work_dir) as temp:
            config = Path(temp) / 'session.conf'
            fd = os.open(config, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o600)
            with os.fdopen(fd, 'w') as stream:
                stream.write('root=' + args.root + '\nnetwork=' + args.network +
                             '\ntarget=' + args.target + '\norigin=' + args.origin +
                             '\nmode=plain\nconnect_ms=30000\ntotal_ms=60000\n'
                             'startup_ms=180000\nmax_retries=0\n')
            env = {k: v for k, v in os.environ.items() if not k.startswith('GIN_')}
            phase = 'startup'
            child = subprocess.Popen([args.gin, 'connect', '-f', str(config),
                                      '-R', str(ready_writer), '-O', str(operations_writer)],
                                     pass_fds=(ready_writer, operations_writer),
                                     stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                                     stderr=subprocess.PIPE, env=env, start_new_session=True)
            for fd in (ready_writer, operations_writer):
                os.close(fd)
                descriptors.remove(fd)
            if line(ready, 240) != 'READY':
                raise ValueError('Invalid readiness record')
            result['READY'] = True
            current, current_inode = output()
            if not current.startswith(prefix) or inode is not None and inode != current_inode:
                raise ValueError('Selected prefix/inode changed')
            inode = current_inode
            directory = private_directory(window)
            try:
                fifo = os.open('in', os.O_WRONLY | os.O_NONBLOCK | os.O_NOFOLLOW, dir_fd=directory)
            finally:
                os.close(directory)
            metadata = os.fstat(fifo)
            if (not stat.S_ISFIFO(metadata.st_mode) or metadata.st_uid != os.getuid() or
                    metadata.st_nlink != 1 or stat.S_IMODE(metadata.st_mode) != 0o600):
                raise ValueError('Selected input is not a FIFO')
            marker = ('gin opt-in smoke ' + uuid.uuid4().hex).encode('ascii')
            record = marker + b'\n'
            if len(record) > os.fpathconf(fifo, 'PC_PIPE_BUF'):
                raise ValueError('Record exceeds atomic FIFO bound')
            phase = 'one-send'
            if os.write(fifo, record) != len(record):
                raise RuntimeError('Incomplete FIFO write; never retry')
            receipts = [line(operations, 210), line(operations, 210)]
            if receipts != ['OP1 1 SEND PENDING 0', 'OP1 1 SEND ACCEPTED_PERSISTED 0']:
                raise RuntimeError('Send was not accepted persisted; never retry')
            result['receipts'] = receipts
            phase = 'receive'
            deadline = time.monotonic() + 60
            while time.monotonic() < deadline:
                if child.poll() is not None:
                    raise RuntimeError('Owner exited before observed receive')
                data, actual_inode = output()
                if marker in data:
                    break
                time.sleep(.1)
            else:
                raise TimeoutError('Marker not observed within independent parent deadline')
            if data.count(marker) != 1 or not data.startswith(prefix) or actual_inode != inode:
                raise ValueError('Receive/prefix/inode mismatch')
            phase = 'stop'
            child.send_signal(signal.SIGTERM)
            stdout, stderr = child.communicate(timeout=20)
            if child.returncode != 0 or stdout or stderr:
                raise RuntimeError('Owner did not stop cleanly and quietly')
            data, actual_inode = output()
            if data.count(marker) != 1 or not data.startswith(prefix) or actual_inode != inode:
                raise ValueError('Post-stop receive/prefix/inode mismatch')
            if private_digest('.gin.credentials', root_fd) != credential_hash:
                raise ValueError('Enrolled credentials changed')
            result.update(passed=True, observed_marker_once=True, prefix_inode_credentials_preserved=True)
    except Exception as exc:
        result.update(failed_phase=phase, exception_type=type(exc).__name__)
    finally:
        if child is not None:
            if child.poll() is None:
                child.send_signal(signal.SIGTERM)
            try:
                stdout, stderr = child.communicate(timeout=20)
            except subprocess.TimeoutExpired:
                os.killpg(child.pid, signal.SIGKILL)
                stdout, stderr = child.communicate(timeout=5)
            result.update(owner_exit=child.returncode, stdout_bytes=len(stdout), stderr_bytes=len(stderr))
        if fifo >= 0:
            os.close(fifo)
        for fd in descriptors:
            os.close(fd)
    return result


def main(argv=None):
    result = smoke(arguments(argv))
    print(json.dumps(result, sort_keys=True))
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
