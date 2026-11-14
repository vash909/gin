"""Separate operation reporting exercised through the actual TLS/WSS CLI."""
import os
import unittest
import test_session_outbound as outbound

class OperationTracer(outbound.OutboundTracer):
    def command(self, *args):
        command = super().command(*args)
        if args and args[0] == 'connect':
            command += ['-O', str(self.operation_write)]
        return command

    def test_literal_fifo_send_preserves_live_receive_without_local_echo(self):
        read, self.operation_write = os.pipe()
        os.set_blocking(read, False)
        os.set_blocking(self.operation_write, False)
        # Outbound fixture explicitly passes only READY: extend pass_fds locally
        # through the subclass's command-independent process constructor below.
        original = outbound.subprocess.Popen
        def spawn(*args, **kwargs):
            kwargs['pass_fds'] = (*kwargs.get('pass_fds', ()), self.operation_write)
            return original(*args, **kwargs)
        outbound.subprocess.Popen = spawn
        try:
            super().test_literal_fifo_send_preserves_live_receive_without_local_echo()
            self.assertEqual(os.read(read, 4096),
                b'OP1 1 SEND PENDING 0\nOP1 1 SEND ACCEPTED_PERSISTED 0\n')
        finally:
            outbound.subprocess.Popen = original
            os.close(read)
            os.close(self.operation_write)


import contextlib
import fcntl
import json
import select
import signal
import socket
import subprocess
import threading
import time
import test_session as startup
from pathlib import Path
from typing import cast

class OperationServer(startup.Server):
    mode: str
    paths: list[str]
    posts: list[bytes]

class ResultHandler(startup.Handler):
    def do_GET(self):
        srv = cast(OperationServer, self.server)
        if srv.mode == 'preflight' and self.path == '/api/config' and srv.paths.count('/api/config') == 2:
            srv.paths.append(self.path)
            body = b'{"server":"grappa","protocol_version":99,"min_protocol_version":99}'
            self.send_response(200)
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        else:
            super().do_GET()

    def do_POST(self):
        srv = cast(OperationServer, self.server)
        assert self.path == '/networks/NetA/channels/%23Room/messages'
        assert self.headers.get('Authorization') == 'Bearer ' + startup.TOKEN
        raw = self.rfile.read(int(self.headers['Content-Length']))
        assert json.loads(raw) == {'body': outbound.TEXT}
        srv.paths.append(self.path)
        srv.posts.append(raw)
        if srv.mode == 'disconnect':
            self.close_connection = True
            self.connection.shutdown(socket.SHUT_RDWR)
            self.connection.close()
            return
        body = b'{"ok":true}' if srv.mode == '202' else b'{"error":"private detail"}'
        self.send_response(202 if srv.mode == '202' else 500)
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)
        self.wfile.flush()

class OperationFailures(unittest.TestCase):
    certdir: Path
    tls = startup.StartupTracer.tls
    command = startup.StartupTracer.command

    @classmethod
    def setUpClass(cls):
        startup.StartupTracer.setUpClass.__func__(cls)

    @classmethod
    def tearDownClass(cls):
        startup.StartupTracer.tearDownClass.__func__(cls)

    @contextlib.contextmanager
    def fixture(self, mode):
        srv = OperationServer(('127.0.0.1', 0), ResultHandler)
        srv.mode = mode
        srv.paths, srv.joins, srv.errors, srv.posts = [], [], [], []
        for name in ['final_requested', 'release_final', 'stopped', 'client_closed', 'client_close_expected']:
            setattr(srv, name, threading.Event())
        srv.release_final.set()
        srv.client_close_expected.set()
        srv.socket = self.tls().wrap_socket(srv.socket, server_side=True)
        thread = threading.Thread(target=srv.serve_forever, kwargs={'poll_interval': .02})
        thread.start()
        try:
            yield srv, f'https://127.0.0.1:{srv.server_port}'
        finally:
            srv.stopped.set()
            srv.shutdown()
            srv.server_close()
            thread.join(3)
            self.assertFalse(thread.is_alive())
            self.assertEqual(srv.errors, [])

    def run_case(self, mode='202', sink='normal', record=None, expected=None, quiet=False, eof=False):
        with self.fixture(mode) as (srv, origin):
            root = self.certdir / ('case-' + self._testMethodName + ('-quiet' if quiet else ''))
            root.mkdir(mode=0o700)
            credential = root / '.gin.credentials'
            credential.write_text(json.dumps({'version':1,'origin':origin,'token':startup.TOKEN,
                'subject_id':startup.UUID,'subject_name':'Alice'}))
            credential.chmod(0o600)
            rr, rw = os.pipe()
            opread, opwrite = os.pipe()
            er, ew = os.pipe()
            for fd in [rr,rw,opread,opwrite,er,ew]:
                os.set_blocking(fd,False)
            flags = {fd:(fcntl.fcntl(fd,fcntl.F_GETFL),fcntl.fcntl(fd,fcntl.F_GETFD)) for fd in [rw,opwrite,er]}
            filled = bytearray()
            if sink == 'full':
                while True:
                    try:
                        n=os.write(opwrite,b'x'*4096)
                        filled.extend(b'x'*n)
                    except BlockingIOError:
                        break
            settings = self.certdir / 'failure-settings'
            settings.write_text('connect_ms=10000\ntotal_ms=20000\njoin_ms=12000\nstartup_ms=180000\n' if startup.PREFIX else '')
            settings.chmod(0o600)
            args = ['connect','-f',settings,'-d',root,'-n','NetA','-c','#Room','-e',origin,
                    '-m','plain','-a',self.certdir/'ca.pem','-R',rw]
            if not quiet:
                args += ['-O',opwrite]
            if eof:
                args += ['-E']
            proc = subprocess.Popen(self.command(*args),pass_fds=(rw,opwrite),stdin=er if eof else subprocess.DEVNULL,
                                    stdout=subprocess.PIPE,stderr=subprocess.PIPE)
            writer=-1
            try:
                self.assertTrue(select.select([rr],[],[],outbound.budget(8))[0])
                self.assertEqual(os.read(rr,128),b'READY\n')
                startup_paths = list(srv.paths)
                writer=os.open(next(root.rglob('in')),os.O_WRONLY|os.O_NONBLOCK)
                if eof:
                    os.close(ew)
                    ew=-1
                else:
                    line=record if record is not None else (outbound.TEXT+'\n').encode()
                    self.assertEqual(os.write(writer,line),len(line))
                data=b''
                success = mode=='202' and sink=='normal' and record is None and not eof
                if success:
                    deadline=time.monotonic()+outbound.budget(8)
                    while data.count(b'\n')<2:
                        self.assertLess(time.monotonic(),deadline)
                        self.assertTrue(select.select([opread],[],[],outbound.budget(8))[0])
                        data += os.read(opread,4096)
                    self.assertEqual(data,b'OP1 1 SEND PENDING 0\nOP1 1 SEND ACCEPTED_NO_PERSIST 0\n')
                    self.assertIsNone(proc.poll())
                    proc.send_signal(signal.SIGTERM)
                stdout,stderr=proc.communicate(timeout=outbound.budget(12))
                self.assertEqual(proc.returncode,0 if success or eof else 1,stderr)
                self.assertEqual(stdout,b'')
                if success or eof:
                    self.assertEqual(stderr,b'')
                elif expected is not None:
                    self.assertEqual(stderr,expected[1])
                if not success:
                    while select.select([opread],[],[],0)[0]:
                        data+=os.read(opread,65536)
                    if sink=='full':
                        self.assertEqual(data,bytes(filled))
                        self.assertEqual(stderr,b'gin: operation pipe failure\n')
                    elif not quiet and expected is not None:
                        self.assertEqual(data,expected[0])
                    else:
                        self.assertEqual(data,b'')
                for fd,original in flags.items():
                    self.assertEqual((fcntl.fcntl(fd,fcntl.F_GETFL),fcntl.fcntl(fd,fcntl.F_GETFD)),original)
                if sink=='full' or record is not None or eof:
                    self.assertEqual(srv.paths,startup_paths,'request after rejected admission')
                    self.assertEqual(srv.posts,[])
                elif mode=='preflight':
                    self.assertEqual(srv.paths,startup_paths+['/api/config'])
                    self.assertEqual(srv.posts,[])
                else:
                    self.assertEqual(srv.paths,startup_paths+['/api/config','/me','/networks/NetA/channels/%23Room/messages'])
                    self.assertEqual(len(srv.posts),1,'automatic replay')
                self.assertEqual(next(root.rglob('out')).read_bytes(),b'','local echo')
            finally:
                srv.stopped.set()
                if proc.poll() is None:
                    proc.kill()
                proc.communicate(timeout=outbound.budget(10))
                if writer>=0:
                    os.close(writer)
                for fd in [rr,rw,opread,opwrite,er,ew]:
                    if fd>=0:
                        os.close(fd)

    def test_accepted_no_persist(self):
        self.run_case()

    def test_complete_500(self):
        self.run_case('500',expected=(b'OP1 1 SEND PENDING 0\nOP1 1 SEND REMOTE_FAILURE 7\n',b'gin: remote operation failure\n'))

    def test_recorded_request_disconnect(self):
        self.run_case('disconnect',expected=(b'OP1 1 SEND PENDING 0\nOP1 1 SEND UNCERTAIN 11\n',b'gin: send outcome uncertain\n'))

    def test_preflight_not_attempted(self):
        self.run_case('preflight',expected=(b'OP1 1 SEND PENDING 0\nOP1 1 SEND NOT_ATTEMPTED 5\n',b'gin: send not attempted\n'))

    def test_full_pending_sink_prevents_all_http(self):
        self.run_case(sink='full')

    def test_unknown_command_is_input_rejection(self):
        self.run_case(record=b'/who #Other\n',expected=(b'OP1 1 INPUT REJECTED 3\n',b'gin: unsupported session mode\n'))

    def test_bad_record_is_input_rejection(self):
        self.run_case(record=b'bad\x00record\n',expected=(b'OP1 1 INPUT REJECTED 2\n',b'gin: unsupported session input\n'))

    def test_quiet_remote_failure_is_static(self):
        self.run_case('500',quiet=True,expected=(b'',b'gin: remote operation failure\n'))

    def test_quiet_uncertain_is_static(self):
        self.run_case('disconnect',quiet=True,expected=(b'',b'gin: send outcome uncertain\n'))

    def test_legacy_eof_still_stops(self):
        self.run_case(eof=True)

    def test_operation_option_lexical_only(self):
        root=self.certdir/'must-not-probe'
        args=['connect-check','-d',root,'-n','NetA','-c','#Room','-e','https://127.0.0.1:9','-m','plain','-a',root/'missing']
        for options,code in [(['-O','999999'],0),(['-O','03'],2),(['-O','2'],2),(['-O','+3'],2),
                             (['-O','3','-O','4'],2),(['-R','3','-O','3'],2),(['-R','3','-O','4'],0)]:
            result=subprocess.run(self.command(*args,*options),capture_output=True,timeout=outbound.budget(5))
            self.assertEqual(result.returncode,code)
            self.assertEqual(result.stdout,b'')
        self.assertFalse(root.exists())

if __name__ == '__main__':
    unittest.main(verbosity=2)
