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
     attempt=1+len(state['joins'])//9
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
     plan=['grappa:user:Alice','grappa:user:Alice/network:one','grappa:user:Alice/network:two','grappa:user:Alice/network:one/channel:$server','grappa:user:Alice/network:two/channel:$server','grappa:user:Alice/network:one/channel:#old1','grappa:user:Alice/network:two/channel:#old2','grappa:user:Alice/network:one/channel:own1','grappa:user:Alice/network:two/channel:own2']
     refs=[]
     for i,topic in enumerate(plan):
      f=recvframe(); state['joins'].append(f); jr,ref,got,event,body=f
      assert got==topic and event=='phx_join' and body=={} and jr==ref
      refs.append(jr)
      send(c,[None,'early',topic,'admitted',{'index':999}])
      send(c,[jr,ref,topic,'phx_reply',{'status':'ok','response':{'protocol_version':7} if i==0 else {}}])
     send(c,[None,None,'grappa:user:Alice/network:alien','admitted',{'index':999}])
     for i,topic in enumerate(plan):
      send(c,['stale',None,topic,'admitted',{'index':999}])
      send(c,[None if i%2==0 else refs[i],'wire',topic,'admitted',{'index':i}])
     if os.environ.get('GIN_RECEIVE_CASE')=='overflow':
      for i in range(65): send(c,[None,'wire',plan[0],'admitted',{'index':i}])
     if os.environ.get('GIN_RECEIVE_CASE')=='bytes':
      for i in range(3): send(c,[None,'wire',plan[0],'admitted',{'index':i,'pad':'x'*800000}])
     drain_client_tail(c,buf,state,attempt)
    else:
     assert path in ('/api/config','/me','/boot')
     if path=='/api/config': assert 'Authorization' not in headers
     else: assert headers['Authorization']=='Bearer '+TOKEN
     body=json.dumps({'/api/config':CONFIG,'/me':dict(ME,id='32345678-1234-1234-1234-123456789abc') if os.environ.get('GIN_RECEIVE_CASE')=='wrong_uuid' else ME,'/boot':BOOT}[path]).encode()
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
 def test_receive(self):
  with self.fixture() as (s,origin,gate):
   p=subprocess.Popen([os.environ['GIN_ACCOUNT_RECEIVE_DRIVER'],origin,str(self.certdir/'ca.pem')],stdout=subprocess.PIPE,stderr=subprocess.PIPE,start_new_session=True)
   try: out,err=p.communicate(timeout=18)
   except BaseException:
    if p.poll() is None: os.killpg(p.pid,signal.SIGKILL)
    p.communicate(); raise
   self.assertEqual(p.returncode,0,err); self.assertEqual(err,b'')
   expected={'wrong_uuid':b'PASS wrong_uuid refused before WSS\n','overflow':b'PASS sticky real count overflow\n','bytes':b'PASS sticky real byte overflow\n'}.get(os.environ.get('GIN_RECEIVE_CASE','positive'),b'PASS actual9 exact_sources owned_frames wrong_stale_prejoin_rejected\n')
   self.assertEqual(out,expected)
   self.assertNotIn(TOKEN.encode(),out+err)
  mode=os.environ.get('GIN_RECEIVE_CASE')
  if mode=='wrong_uuid':
   self.assertEqual(s['paths'],['/api/config','/me']);self.assertEqual(s['joins'],[]);self.assertEqual(s['eof'],[])
   self.assertEqual(s['maxlive'],0)
  else:
   attempts=1 if mode in ('overflow','bytes') else 2
   self.assertEqual(s['paths'],['/api/config','/me','/socket/websocket?vsn=2.0.0&client_proto=4']*attempts)
   self.assertEqual(len(s['joins']),9*attempts);self.assertEqual(s['queries'],[])
   self.assertEqual(s['eof'],list(range(1,attempts+1)));self.assertEqual(s['live'],0);self.assertEqual(s['maxlive'],1)
if __name__=='__main__': unittest.main(verbosity=2)
