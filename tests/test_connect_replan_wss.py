#!/usr/bin/env python3
"""CHARACTERIZATION, LIVE LOCAL FAKE only. No account or external authority.
max_live_WSS counts server-admitted upgrades; the fixture enforces their order.
"""
import base64, contextlib, hashlib, json, os, selectors, signal, socket, subprocess, threading, time, unittest
from pathlib import Path
from typing import ClassVar
import test_http
from ws_fixture_frames import server_frame, take_client_frame
ROOT=Path(__file__).resolve().parents[1]
TOKEN="fixture-token.DO-NOT-LOG"
CONFIG={"server":"grappa","protocol_version":7,"min_protocol_version":3}
ME={"kind":"user","id":"22345678-1234-1234-1234-123456789abc","name":"Alice"}
BOOT={"networks":[{"id":3000000001,"slug":"one","nick":"Own1"},{"id":2,"slug":"two","nick":"Own2"}],"channels":{"one":[{"name":"#old1","joined":True,"source":"joined"}],"two":[{"name":"#old2","joined":False,"source":"autojoin"}]},"heads":{"one":{},"two":{}}}
def drain_client_tail(c, buf, state, attempt):
 """Validate remaining masked client frames through an observed socket EOF."""
 while True:
  while True:
   extra=take_client_frame(buf)
   if extra is None: break
   assert extra[0] in (8,10),'unexpected replay/extra operation'
  block=c.recv(4096)
  if not block:
   assert not buf,'partial WS frame at EOF'
   state['eof'].append(attempt)
   return
  buf.extend(block)

class Replan(unittest.TestCase):
 certdir: ClassVar[Path]
 @classmethod
 def setUpClass(cls): test_http.HTTPTests.setUpClass.__func__(cls)
 @classmethod
 def tearDownClass(cls): test_http.HTTPTests.tearDownClass.__func__(cls)
 tls=test_http.HTTPTests.tls
 @contextlib.contextmanager
 def fixture(self):
  listener=socket.socket(); listener.bind(('127.0.0.1',0)); listener.listen(16); listener.settimeout(.05)
  state={'paths':[],'joins':[],'queries':[],'errors':[],'workers':[],'eof':[],'live':0,'maxlive':0}
  stop=threading.Event(); gate=threading.Event(); oldclosed=threading.Event(); ctx=self.tls()
  def send(c,f): c.sendall(server_frame(json.dumps(f).encode()))
  def worker(raw):
   c=None; ws=False; attempt=0
   try:
    raw.settimeout(3); c=ctx.wrap_socket(raw,server_side=True); c.settimeout(3)
    data=b''
    while b'\r\n\r\n' not in data:
     block=c.recv(4096); assert block,'header EOF'; data+=block; assert len(data)<16384
    head,tail=data.split(b'\r\n\r\n',1); lines=head.decode().split('\r\n'); method,path,_=lines[0].split(); assert method=='GET' and not tail
    headers=dict(x.split(': ',1) for x in lines[1:]); state['paths'].append(path)
    if path.startswith('/socket/'):
     attempt=1+len(state['queries']); assert attempt in (1,2)
     if attempt==2: assert oldclosed.wait(2),'old socket still live'
     ws=True; state['live']+=1; state['maxlive']=max(state['maxlive'],state['live']); assert state['live']==1
     assert headers['Sec-WebSocket-Protocol']=='phoenix,base64url.bearer.phx.'+base64.b64encode(TOKEN.encode()).decode().rstrip('=')
     assert 'Authorization' not in headers
     accept=base64.b64encode(hashlib.sha1((headers['Sec-WebSocket-Key']+'258EAFA5-E914-47DA-95CA-C5AB0DC85B11').encode()).digest())
     c.sendall(b'HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Protocol: phoenix\r\nSec-WebSocket-Accept: '+accept+b'\r\n\r\n')
     buf=bytearray()
     def recvframe():
      while True:
       item=take_client_frame(buf)
       if item:
        op,fin,payload=item
        if op==10: continue
        assert op==1 and fin
        return json.loads(payload)
       block=c.recv(16384); assert block,'premature WS EOF'; buf.extend(block)
     plan=['grappa:user:Alice','grappa:user:Alice/network:one',f'grappa:user:Alice/network:one/channel:#{"old" if attempt==1 else "new"}1','grappa:user:Alice/network:two',f'grappa:user:Alice/network:two/channel:#{"old" if attempt==1 else "new"}2']
     for i,topic in enumerate(plan):
      f=recvframe(); state['joins'].append(f); jr,ref,got,event,body=f
      assert got==topic and event=='phx_join' and body=={} and jr==ref
      send(c,[jr,ref,topic,'phx_reply',{'status':'ok','response':{'protocol_version':7} if i==0 else {}}])
     f=recvframe(); state['queries'].append(f); jr,ref,topic,event,body=f
     assert event=='open_query_window' and topic==plan[0]
     assert body==({'network_id':3000000001,'target_nick':'Peer'} if attempt==1 else {'network_id':2,'target_nick':'Other'})
     ack=[jr,ref,topic,'phx_reply',{'status':'ok','response':{}}]
     if attempt==1: state['oldack']=ack
     else: send(c,state['oldack'])
     send(c,[jr,None,topic,'tracer_marker',{}])
     if attempt==2:
      assert gate.wait(3),'manual client receipt gate timed out'; send(c,ack)
     drain_client_tail(c,buf,state,attempt)
    else:
     assert path in ('/api/config','/me','/boot')
     if path=='/api/config': assert 'Authorization' not in headers
     else: assert headers['Authorization']=='Bearer '+TOKEN
     body=json.dumps({'/api/config':CONFIG,'/me':ME,'/boot':BOOT}[path]).encode()
     c.sendall(b'HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: '+str(len(body)).encode()+b'\r\n\r\n'+body)
   except BaseException as e: state['errors'].append(type(e).__name__+': '+str(e))
   finally:
    if c: c.close()
    raw.close()
    if ws:
     state['live']-=1
     if attempt==1: oldclosed.set()
  def accept():
   try:
    while not stop.is_set():
     try: raw,_=listener.accept()
     except socket.timeout: continue
     assert len(state['workers'])<16,'worker cap'
     t=threading.Thread(target=worker,args=(raw,)); state['workers'].append(t); t.start()
   except BaseException as e: state['errors'].append(repr(e))
  thread=threading.Thread(target=accept); thread.start()
  try: yield state,f'https://127.0.0.1:{listener.getsockname()[1]}',gate
  finally:
   stop.set(); end=time.monotonic()+4; thread.join(max(0,end-time.monotonic())); listener.close(); assert not thread.is_alive()
   for t in state['workers']: t.join(max(0,end-time.monotonic())); assert not t.is_alive(),'worker leak'
   assert not state['errors'],state['errors']
 def tail_probe(self, tail, prebuffered, rejection=None):
  """Parser-level real socketpair control, not an injected Native replay."""
  payload=json.dumps(['1','6','grappa:user:Alice','open_query_window',
   {'network_id':3000000001,'target_nick':'Peer'}],separators=(',',':')).encode()
  def masked(data, opcode=1):
   self.assertLess(len(data),65536)
   length=bytes((0x80|len(data),)) if len(data)<126 else b'\xfe'+len(data).to_bytes(2,'big')
   key=b'abcd'
   return bytes((0x80|opcode,))+length+key+bytes(v^key[i%4] for i,v in enumerate(data))
  query=masked(payload)
  suffix=query if tail=='query' else query[:3] if tail=='partial' else masked(b'',10)+masked(b'',8)
  pending=bytearray(query+(suffix if prebuffered else b''))
  self.assertEqual(take_client_frame(pending),(1,True,payload))
  reader,writer=socket.socketpair()
  state={'eof':[]}
  try:
   reader.settimeout(1); writer.settimeout(1)
   if not prebuffered: writer.sendall(suffix)
   writer.shutdown(socket.SHUT_WR)
   if rejection is not None:
    with self.assertRaisesRegex(AssertionError,rejection):
     drain_client_tail(reader,pending,state,1)
    self.assertEqual(state['eof'],[])
   else:
    drain_client_tail(reader,pending,state,1)
    self.assertEqual(state['eof'],[1]); self.assertEqual(pending,bytearray())
  finally:
   reader.close(); writer.close()
 def test_buffered_extra_operation_rejected(self):
  for prebuffered in (True,False):
   with self.subTest(prebuffered=prebuffered):
    self.tail_probe('query',prebuffered,'unexpected replay/extra operation')
 def test_partial_frame_rejected_at_eof(self):
  for prebuffered in (True,False):
   with self.subTest(prebuffered=prebuffered):
    self.tail_probe('partial',prebuffered,'partial WS frame at EOF')
 def test_control_tail_drained(self):
  for prebuffered in (True,False):
   with self.subTest(prebuffered=prebuffered):
    self.tail_probe('control',prebuffered)
 def test_same_object(self):
  with self.fixture() as (s,origin,gate):
   p=subprocess.Popen([os.environ['GIN_REPLAN_DRIVER'],origin,str(self.certdir/'ca.pem')],stdout=subprocess.PIPE,stderr=subprocess.PIPE,start_new_session=True)
   try:
    output=p.stdout
    if output is None: self.fail('driver stdout pipe missing')
    selector=selectors.DefaultSelector(); selector.register(output,selectors.EVENT_READ)
    assert selector.select(10),'outer receipt deadline'
    line=output.readline(); selector.close()
    if not line:
     out,err=p.communicate(timeout=2)
     self.fail('driver early exit: '+err.decode(errors='replace'))
    self.assertEqual(line,b'PENDING202_AFTER_STALE_MARKER\n')
    gate.set(); out,err=p.communicate(timeout=8)
   except BaseException:
    if p.poll() is None: os.killpg(p.pid,signal.SIGKILL)
    p.communicate(); raise
   self.assertEqual(p.returncode,0,err); self.assertEqual(err,b'')
   self.assertEqual(out,b'PASS SAME_OBJECT UNCERTAIN_TAKE REPLAN STALE_REJECT ACCEPTED202\n')
   self.assertNotIn(TOKEN.encode(),line+out+err)
  self.assertEqual(s['paths'],['/api/config','/me','/boot']+['/api/config','/me','/socket/websocket?vsn=2.0.0&client_proto=4']*2)
  self.assertEqual(len(s['joins']),10); self.assertEqual(len(s['queries']),2)
  self.assertEqual(s['eof'],[1,2]); self.assertEqual(s['maxlive'],1); self.assertEqual(s['live'],0)
  refs=[int(f[1]) for f in s['joins']+s['queries']]; self.assertEqual(len(set(refs)),12)
  print('CHARACTERIZATION PASS: GET=9 joins=10 queries=2 EOF=2 max_live_WSS=1')
if __name__=='__main__': unittest.main(verbosity=2)
