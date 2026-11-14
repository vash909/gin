"""Actual CLI admission waits for current receive drain, not READY history.
Partial TEXT receipt is observed at real curl_ws_recv in a test-only CLI.
"""
import base64
import contextlib
import hashlib
import json
import os
import select
import signal
import subprocess
import threading
import time
import unittest
from pathlib import Path
from typing import cast
import sys
import test_session as base
import test_session_outbound as outbound
from ws_fixture_frames import server_frame, take_client_frame

class AdmissionServer(base.Server):
    partial: bool
    posts: list[bytes]
    posted: threading.Event
    begin: threading.Event
    release: threading.Event

    def handle_error(self, request, client_address):
        self.errors.append(repr(sys.exc_info()[1]))

class Handler(outbound.Handler):
    def upgrade(self):
        srv = cast(AdmissionServer, self.server)
        assert self.headers.get('Authorization') is None
        assert self.headers.get('Sec-WebSocket-Protocol') == 'phoenix,base64url.bearer.phx.YWF+'
        key = self.headers['Sec-WebSocket-Key']
        accept = base64.b64encode(hashlib.sha1((key+'258EAFA5-E914-47DA-95CA-C5AB0DC85B11').encode()).digest()).decode()
        self.send_response(101)
        for name,value in [('Upgrade','websocket'),('Connection','Upgrade'),('Sec-WebSocket-Accept',accept),('Sec-WebSocket-Protocol','phoenix')]:
            self.send_header(name,value)
        self.end_headers()
        self.wfile.flush()
        self.connection.settimeout(.2)
        buffered=bytearray()
        def receive():
            deadline=time.monotonic()+outbound.budget(8)
            while time.monotonic()<deadline and not srv.stopped.is_set():
                item=take_client_frame(buffered)
                if item is not None: return item
                try: data=self.connection.recv(16384)
                except TimeoutError: continue
                if not data: return None
                buffered.extend(data)
            return None
        try:
            topic = ""
            for index in range(3):
                item=receive()
                assert item is not None and item[0:2]==(1,True), 'missing text join'
                jr,ref,topic,event,body=json.loads(item[2])
                assert event=='phx_join' and jr==ref and body=={}
                srv.joins.append(topic)
                response={'protocol_version':36} if index==0 else {}
                self.connection.sendall(server_frame(json.dumps([jr,ref,topic,'phx_reply',{'status':'ok','response':response}]).encode()))
            assert srv.begin.wait(outbound.budget(8)), 'READY receipt barrier expired'
            if srv.stopped.is_set(): return
            row={'network':'NetA','channel':'#room','kind':'action','sender':'Nick','server_time':0,'body':'\x01ACTION before-fifo'}
            raw=json.dumps([None,None,topic,'event',{'kind':'message','message':row}]).encode()
            self.connection.sendall(server_frame(raw,final=not srv.partial))
            if srv.partial:
                assert srv.release.wait(outbound.budget(8)), 'FIN release expired'
                if srv.stopped.is_set(): return
                self.connection.sendall(server_frame(b'',opcode=0))
            while not srv.stopped.is_set():
                item=receive()
                if item is None: return
                assert item[0] in (8,10), 'unexpected WS mutation'
        finally:
            self.close_connection=True

class Admission(unittest.TestCase):
    certdir: Path
    tls=base.StartupTracer.tls
    command=base.StartupTracer.command
    @classmethod
    def setUpClass(cls):
        base.StartupTracer.setUpClass.__func__(cls)
    @classmethod
    def tearDownClass(cls):
        base.StartupTracer.tearDownClass.__func__(cls)
    @contextlib.contextmanager
    def fixture(self,partial):
        srv=AdmissionServer(('127.0.0.1',0),Handler)
        srv.partial=partial
        srv.paths,srv.joins,srv.errors,srv.posts=[],[],[],[]
        for name in ['stopped','begin','release','posted']:
            setattr(srv,name,threading.Event())
        srv.socket=self.tls().wrap_socket(srv.socket,server_side=True)
        thread=threading.Thread(target=srv.serve_forever,kwargs={'poll_interval':.02}); thread.start()
        try: yield srv,f'https://127.0.0.1:{srv.server_port}'
        finally:
            srv.stopped.set(); srv.begin.set(); srv.release.set()
            srv.shutdown(); srv.server_close(); thread.join(3)
            self.assertFalse(thread.is_alive()); self.assertEqual(srv.errors,[], 'server faults (original exception retained as context)')
    def wait_output(self, proc, path, marker):
        deadline = time.monotonic() + outbound.budget(8)
        while marker not in path.read_bytes():
            self.assertIsNone(proc.poll(), 'connected process exited before incoming projection')
            self.assertLess(time.monotonic(), deadline, 'incoming projection deadline')
            time.sleep(.02)

    def run_case(self,partial):
        with self.fixture(partial) as (srv,origin):
            root=self.certdir/('partial' if partial else 'normal'); root.mkdir(mode=0o700)
            credential=root/'.gin.credentials'
            credential.write_text(json.dumps({'version':1,'origin':origin,'token':base.TOKEN,'subject_id':base.UUID,'subject_name':'Alice'})); credential.chmod(0o600)
            rr,rw=os.pipe(); opread,opwrite=os.pipe(); observed,observer=os.pipe()
            for fd in [rr,rw,opread,opwrite,observed,observer]: os.set_blocking(fd,False)
            settings=self.certdir/'settings'
            settings.write_text('connect_ms=10000\ntotal_ms=20000\njoin_ms=12000\nstartup_ms=180000\n' if base.PREFIX else ''); settings.chmod(0o600)
            env=dict(os.environ, GIN_TEST_RECEIVE_FD=str(observer))
            proc=subprocess.Popen(self.command('connect','-f',settings,'-d',root,'-n','NetA','-c','#Room','-e',origin,'-a',self.certdir/'ca.pem','-m','plain','-R',rw,'-O',opwrite),pass_fds=(rw,opwrite,observer),env=env,stdin=subprocess.DEVNULL,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
            writer=-1
            try:
                self.assertTrue(select.select([rr],[],[],outbound.budget(8))[0]); self.assertEqual(os.read(rr,128),b'READY\n')
                srv.begin.set()
                if partial:
                    self.assertTrue(select.select([observed],[],[],outbound.budget(8))[0], 'no real partial TEXT receive marker')
                    self.assertEqual(os.read(observed,128),b'T')
                else:
                    self.wait_output(proc,next(root.rglob('out')),outbound.BEFORE)
                paths=list(srv.paths)
                writer=os.open(next(root.rglob('in')),os.O_WRONLY|os.O_NONBLOCK)
                os.write(writer,(outbound.TEXT+'\n').encode())
                if partial:
                    # Quiescence is not the instrumented HTTP/FIN deadline.
                    quiet = 2.0 if base.PREFIX else .4
                    self.assertLess(quiet, outbound.budget(8), 'quiet interval consumes FIN release budget')
                    self.assertFalse(select.select([opread],[],[],quiet)[0], 'premature OP1 admission while TEXT unfinished')
                    self.assertEqual(srv.paths,paths,'HTTP before local receive drain')
                    self.assertEqual(srv.posts,[])
                srv.release.set()
                data=b''
                deadline=time.monotonic()+outbound.budget(8)
                while data.count(b'\n')<2:
                    self.assertLess(time.monotonic(),deadline)
                    self.assertTrue(select.select([opread],[],[],outbound.budget(8))[0]); data+=os.read(opread,4096)
                self.assertEqual(data,b'OP1 1 SEND PENDING 0\nOP1 1 SEND ACCEPTED_PERSISTED 0\n')
                self.wait_output(proc,next(root.rglob('out')),outbound.BEFORE)
                self.assertEqual(next(root.rglob('out')).read_bytes(),outbound.BEFORE,'local echo or lost receive')
                self.assertEqual(len(srv.posts),1)
                proc.send_signal(signal.SIGTERM); stdout,stderr=proc.communicate(timeout=outbound.budget(8))
                self.assertEqual((proc.returncode,stdout,stderr),(0,b'',b''))
            finally:
                srv.stopped.set(); srv.release.set()
                if proc.poll() is None: proc.kill()
                proc.communicate(timeout=outbound.budget(8))
                if writer>=0: os.close(writer)
                for fd in [rr,rw,opread,opwrite,observed,observer]: os.close(fd)
    def test_complete_text_control(self): self.run_case(False)
    def test_partial_text_defers_fifo_admission(self): self.run_case(True)

if __name__=='__main__': unittest.main()
