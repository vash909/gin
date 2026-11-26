#!/usr/bin/env python3
"""Owned loopback TLS/WSS fixture. Synthetic credentials only."""
import base64, contextlib, hashlib, json, os, signal, socket, ssl, subprocess, threading, time, unittest
from pathlib import Path
from typing import ClassVar
from unittest import mock
import test_http
from test_topology import BOOT, CONFIG
from test_auth import IDENTITY, TOKEN
from test_connect import server_frame, take_client_frame
ROOT=Path(__file__).resolve().parents[1]
class Discovery(unittest.TestCase):
 certdir: ClassVar[Path]
 @classmethod
 def setUpClass(cls): test_http.HTTPTests.setUpClass.__func__(cls)
 @classmethod
 def tearDownClass(cls): test_http.HTTPTests.tearDownClass.__func__(cls)
 tls=test_http.HTTPTests.tls
 @contextlib.contextmanager
 def fixture(self,mode):
  listener=socket.socket(); listener.bind(('127.0.0.1',0)); listener.listen(8); listener.settimeout(.1)
  state={'records':[], 'joins':[], 'errors':[], 'workers':[], 'active':[], 'intentional':[], 'closed':[], 'observed_eof':[]}
  stop=threading.Event(); ctx=self.tls()
  def worker(raw):
   conn=None
   try:
    raw.settimeout(2); conn=ctx.wrap_socket(raw,server_side=True); state['active'].append(conn)
    data=b''
    while b'\r\n\r\n' not in data:
     block=conn.recv(4096)
     if not block: raise AssertionError('EOF in headers')
     data+=block
     assert len(data)<16384
    head,tail=data.split(b'\r\n\r\n',1); lines=head.decode().split('\r\n'); method,path,_=lines[0].split()
    headers=dict(x.split(': ',1) for x in lines[1:]); state['records'].append((method,path,headers)); assert method=='GET' and not tail
    if path.startswith('/socket/'):
     assert headers['Sec-WebSocket-Protocol']=='phoenix,base64url.bearer.phx.'+base64.b64encode(TOKEN.encode()).decode().rstrip('=')
     accept=base64.b64encode(hashlib.sha1((headers['Sec-WebSocket-Key']+'258EAFA5-E914-47DA-95CA-C5AB0DC85B11').encode()).digest())
     conn.sendall(b'HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Protocol: phoenix\r\nSec-WebSocket-Accept: '+accept+b'\r\n\r\n')
     buf=bytearray()
     while True:
      item=take_client_frame(buf)
      if item: break
      block=conn.recv(4096); assert block; buf.extend(block)
     op,fin,payload=item; assert op==1 and fin
     frame=json.loads(payload); state['joins'].append(frame); jr,ref,topic,event,body=frame
     assert jr==ref and jr and topic=='grappa:user:Alice' and event=='phx_join' and body=={}
     peer={'kind':'query_windows_list','windows':{'3000000001':[{'network_id':3000000001,'target_nick':'Peer','opened_at':'synthetic'}]}}
     empty={'kind':'query_windows_list','windows':{}}
     snapshots=[] if mode=='missing' else [empty if mode=='empty' else peer]
     if mode=='bad': snapshots=[{'kind':'query_windows_list','windows':{'99':[]}}]
     if mode=='malformed': snapshots=[{'kind':'query_windows_list','windows':[]}]
     if mode=='limit': snapshots=[empty]*9
     frames=[[jr,ref,topic,'phx_reply',{'status':'ok','response':{'protocol_version':7}}]]
     frames += [[jr,None,topic,'event',x] for x in snapshots]
     if mode in ('replace','flood'):
      frames += [[jr,None,topic,'event',{'kind':'unrelated','n':i}] for i in range(160)]
      if mode=='replace': frames.append([jr,None,topic,'event',empty])
     conn.sendall(b''.join(server_frame(json.dumps(x).encode()) for x in frames))
     # Sending frames is not proof of owner close: require actual recv EOF.
     state['intentional'].append(topic)
     while True:
      b=conn.recv(4096)
      if not b:
       state['observed_eof'].append(topic)
       if mode=='late-reset': raise ConnectionResetError('injected after actual WS EOF')
       if mode=='late-ssl': raise ssl.SSLEOFError('injected after actual WS EOF')
       break
      buf.extend(b)
      while True:
       extra=take_client_frame(buf)
       if not extra: break
       assert extra[0] in (8,10), 'unexpected Native mutation/extra join'
     state['closed'].append(topic)
    else:
     assert path in ('/api/config','/me','/boot')
     if path!='/api/config': assert headers['Authorization']=='Bearer '+TOKEN
     else: assert 'Authorization' not in headers
     value={'/api/config':CONFIG,'/me':IDENTITY,'/boot':BOOT}[path]
     if mode=='invalid-boot' and path=='/boot': value={}
     body=json.dumps(value).encode()
     conn.sendall(b'HTTP/1.1 200 OK\r\nContent-Length: '+str(len(body)).encode()+b'\r\nConnection: close\r\n\r\n'+body)
     if mode=='injected' and path=='/boot': raise RuntimeError('injected handler failure')
   except BaseException as e: state['errors'].append(repr(e))
   finally:
    if conn: conn.close()
    raw.close()
  def accept():
   try:
    while not stop.is_set():
     try: raw,_=listener.accept()
     except socket.timeout: continue
     if len(state['workers'])>=8:
      raw.close(); raise AssertionError('worker admission limit')
     t=threading.Thread(target=worker,args=(raw,)); state['workers'].append(t); t.start()
   except BaseException as e: state['errors'].append(repr(e))
  thread=threading.Thread(target=accept); thread.start()
  try: yield state,listener.getsockname()[1]
  finally:
   deadline=time.monotonic()+3
   stop.set(); thread.join(max(0,deadline-time.monotonic())); listener.close(); assert not thread.is_alive()
   for t in state['workers']: t.join(max(0,deadline-time.monotonic())); assert not t.is_alive(), 'worker leak'
   assert not state['errors'], state['errors']
 def request(self,port,selector='discover'):
  binary=os.environ.get('GIN_DISCOVERY_DRIVER',str(ROOT/'.build/account_discovery_driver'))
  p=subprocess.Popen([binary,str(port),str(self.certdir/'ca.pem'),selector],stdout=subprocess.PIPE,stderr=subprocess.PIPE,start_new_session=True)
  try: out,err=p.communicate(timeout=7)
  except BaseException:
   os.killpg(p.pid,signal.SIGKILL); p.communicate(); raise
  self.assertEqual(p.returncode,0,err); self.assertEqual(err,b''); self.assertNotIn(TOKEN.encode(),out)
  return list(map(int,out.split()))

 def test_00_legacy_rest_only_bridge(self):
  with self.fixture('peer') as (s,port): r=self.request(port,'LEGACY_REST_ONLY_BRIDGE')
  self.assertEqual([x[:2] for x in s['records']],[('GET',x) for x in ['/api/config','/me','/boot']])
  self.assertEqual(r[:4],[0,0,2,2], 'retained original semantic RED target was [0,1,2,3]')
 def test_native_cases(self):
  for mode,code,windows in [('peer',0,3),('empty',0,2),('missing',5,0),('replace',0,2),('flood',0,3),('bad',2,0),('malformed',2,0),('limit',6,0)]:
   with self.subTest(mode=mode),self.fixture(mode) as (s,port):
    r=self.request(port); self.assertEqual(r[0],code,r)
    self.assertEqual(r[2:4],[2,windows] if not code else [0,0]); self.assertEqual(r[-2:],[0,0])
    self.assertEqual(r[1],int(not code))
   self.assertEqual([x[:2] for x in s['records']], [('GET',x) for x in ['/api/config','/me','/boot','/api/config','/me','/socket/websocket?vsn=2.0.0&client_proto=4']])
   self.assertEqual(len(s['joins']),1); self.assertEqual(len(s['intentional']),1); self.assertEqual(len(s['closed']),1)
 def test_handler_ledger(self):
  with self.assertRaisesRegex(AssertionError,'injected handler failure'):
   with self.fixture('injected') as (s,port): self.request(port,'LEGACY_REST_ONLY_BRIDGE')
 def test_late_exception_ledger(self):
  for mode in ('late-reset','late-ssl'):
   with self.subTest(mode=mode):
    s=None; r=None
    with self.assertRaisesRegex(AssertionError,'injected after actual WS EOF'):
     with self.fixture(mode) as (s,port):
      r=self.request(port)
    if s is None: self.fail('fixture did not yield state')
    if r is None: self.fail('request did not return')
    self.assertEqual(r[:4],[0,1,2,3]); self.assertEqual(r[-2:],[0,0])
    self.assertEqual(len(s['observed_eof']),1)
    self.assertEqual(len(s['joins']),1)
    self.assertEqual([x[:2] for x in s['records']], [('GET',x) for x in ['/api/config','/me','/boot','/api/config','/me','/socket/websocket?vsn=2.0.0&client_proto=4']])
 def test_late_exception_prerequisites_not_masked(self):
  # Real Native TLS request; inject assertions, never synthetic API output.
  for target in ('healthy-result','cleanup-flags','request-completion'):
   with self.subTest(target=target):
    case=Discovery('test_late_exception_ledger')
    original_equal=case.assertEqual; original_request=case.request
    sentinel=AssertionError('prerequisite sentinel: '+target)
    def probe_equal(first,second,msg=None):
     original_equal(first,second,msg)
     if (target=='healthy-result' and second==[0,1,2,3]) or (target=='cleanup-flags' and second==[0,0]):
      raise sentinel
    def probe_request(port,selector='discover'):
     result=original_request(port,selector)
     original_equal(result[:4],[0,1,2,3]); original_equal(result[-2:],[0,0])
     if target=='request-completion': raise sentinel
     return result
    expected='request did not return' if target=='request-completion' else str(sentinel)
    with mock.patch.object(case,'assertEqual',side_effect=probe_equal), mock.patch.object(case,'request',side_effect=probe_request):
     with self.assertRaisesRegex(AssertionError,expected) as caught:
      case.test_late_exception_ledger()
    if target!='request-completion': self.assertIs(caught.exception,sentinel)
 def test_cleanup_primary(self):
  for mode,selector,code,paths in [
   ('peer','stop-taint',4,[]),
   ('invalid-boot','taint',2,['/api/config','/me','/boot']),
   ('peer','taint',9,['/api/config','/me','/boot','/api/config','/me','/socket/websocket?vsn=2.0.0&client_proto=4'])]:
   with self.subTest(mode=mode,selector=selector):
    with self.fixture(mode) as (s,port): r=self.request(port,selector)
    self.assertEqual(r[0],code,r); self.assertEqual(r[1:4],[0,0,0]); self.assertEqual(r[-2:],[1,0])
    self.assertEqual([x[:2] for x in s['records']],[('GET',x) for x in paths])
if __name__=='__main__': unittest.main(verbosity=2)
