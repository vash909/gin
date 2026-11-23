#!/usr/bin/env python3
"""Real loopback verified TLS; baseline is live-only composition, NOT old CLI."""
import base64
import contextlib
import hashlib
import json
import os
import selectors
import signal
import socket
import stat
import subprocess
import tempfile
import threading
import time
import unittest
from pathlib import Path
import test_account_receive as wire
from ws_fixture_frames import server_frame, take_client_frame
ROOT=Path(__file__).resolve().parents[1]
TOPICS=['grappa:user:Alice','grappa:user:Alice/network:one','grappa:user:Alice/network:two','grappa:user:Alice/network:one/channel:$server','grappa:user:Alice/network:two/channel:$server','grappa:user:Alice/network:one/channel:#aquiet','grappa:user:Alice/network:one/channel:#old1','grappa:user:Alice/network:one/channel:peer','grappa:user:Alice/network:two/channel:#old2','grappa:user:Alice/network:two/channel:other','grappa:user:Alice/network:one/channel:own1','grappa:user:Alice/network:two/channel:own2']
SCOPES=[('one','$server'),('two','$server'),('one','#aquiet'),('one','#old1'),('one','Peer'),('two','#old2'),('two','Other')]
def path(net,target):
 from urllib.parse import quote
 return '/networks/'+quote(net,safe='')+'/channels/'+quote(target,safe='')+'/messages?limit=50&after=0'
def row(net,target,id):
 r={'id':id,'server_time':123,'network':net,'channel':target,'kind':'notice' if target=='$server' else 'privmsg','sender':'sender','body':'row'+str(id)}
 if target in ('Peer','Other'): r['dm_with']=target
 return r
class Bootstrap(wire.Replan):
 @contextlib.contextmanager
 def fixture(self):
  listener=socket.socket();listener.bind(('127.0.0.1',0));listener.listen(24);listener.settimeout(.05)
  ctx=self.tls();stop=threading.Event();quiet=threading.Event();held=threading.Event();release=threading.Event();state={'paths':[],'joins':[],'errors':[],'eof':[],'workers':[],'live':0,'quiet':quiet,'held':held,'release':release}
  def worker(raw):
   c=None;ws=False
   try:
    raw.settimeout(4);c=ctx.wrap_socket(raw,server_side=True);c.settimeout(4);data=b''
    while b'\r\n\r\n' not in data:
     block=c.recv(4096);assert block,'header EOF';data+=block;assert len(data)<16384
    head,tail=data.split(b'\r\n\r\n',1);lines=head.decode().split('\r\n');method,p,_=lines[0].split();assert method=='GET' and not tail
    headers=dict(x.split(': ',1) for x in lines[1:]);state['paths'].append(p)
    if p.startswith('/socket/'):
     ws=True;state['live']+=1;assert state['live']==1
     assert headers['Sec-WebSocket-Protocol']=='phoenix,base64url.bearer.phx.'+base64.b64encode(wire.TOKEN.encode()).decode().rstrip('=')
     assert 'Authorization' not in headers
     accept=base64.b64encode(hashlib.sha1((headers['Sec-WebSocket-Key']+'258EAFA5-E914-47DA-95CA-C5AB0DC85B11').encode()).digest())
     c.sendall(b'HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Protocol: phoenix\r\nSec-WebSocket-Accept: '+accept+b'\r\n\r\n');buf=bytearray()
     def send(f): c.sendall(server_frame(json.dumps(f).encode()))
     def recv():
      while True:
       f=take_client_frame(buf)
       if f:
        assert f[0]==1 and f[1];return json.loads(f[2])
       block=c.recv(16384);assert block,'join EOF';buf.extend(block)
     for i,topic in enumerate(TOPICS):
      f=recv();state['joins'].append(f);jr,ref,got,event,body=f;assert got==topic and event=='phx_join' and body=={} and jr==ref
      send([jr,ref,topic,'phx_reply',{'status':'ok','response':{'protocol_version':40} if i==0 else {}}])
     # No application frames until a real quiet GET and held channel request.
     while not getattr(self,'invalid_origin',False) and not held.wait(.01):
      if stop.is_set(): break
     if held.is_set():
      assert quiet.is_set(),'application before quiet GET'
      for id in (20,30): send([None,None,TOPICS[6],'event',{'kind':'message','message':row('one','#old1',id)}])
     wire.drain_client_tail(c,buf,state,1)
    else:
     if p=='/api/config': assert 'Authorization' not in headers;body={'server':'grappa','protocol_version':40,'min_protocol_version':3}
     else:
      assert headers['Authorization']=='Bearer '+wire.TOKEN
      if p=='/me': body=wire.ME
      else:
       matches=[(net,target) for net,target in SCOPES if path(net,target)==p];assert len(matches)==1,'unexpected focused path'
       net,target=matches[0]
       if target=='#aquiet': quiet.set();body=[]
       elif target=='#old1':
        assert quiet.is_set();held.set();assert release.wait(3),'REST release absent';body=[row(net,target,10),row(net,target,20)]
       else: body=[row(net,target,10)]
     data=json.dumps(body).encode();c.sendall(b'HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: '+str(len(data)).encode()+b'\r\n\r\n'+data)
   except BaseException as e: state['errors'].append(type(e).__name__+': '+str(e))
   finally:
    if c: c.close()
    raw.close()
    if ws: state['live']-=1
  def accept():
   try:
    while not stop.is_set():
     try: raw,_=listener.accept()
     except socket.timeout: continue
     assert len(state['workers'])<25
     t=threading.Thread(target=worker,args=(raw,));state['workers'].append(t);t.start()
   except BaseException as e: state['errors'].append(str(e))
  thread=threading.Thread(target=accept);thread.start()
  try: yield state,f'https://127.0.0.1:{listener.getsockname()[1]}',release
  finally:
   stop.set();release.set();thread.join(5);listener.close();assert not thread.is_alive()
   end=time.monotonic()+5
   for t in state['workers']: t.join(max(0,end-time.monotonic()));assert not t.is_alive(),'worker leak'
   assert not state['errors'],state['errors']
 def test_receive(self):
  invalid=getattr(self,'invalid_origin',False)
  stopping=getattr(self,'stop_append',False)
  baseline=os.environ.get('BOOTSTRAP_BASELINE')=='1'
  with tempfile.TemporaryDirectory(prefix='gin-multi-history-',dir=os.environ.get('TMPDIR') or str(ROOT/'.build')) as tmp:
   root=Path(tmp)/'irc';r,w=os.pipe2(os.O_NONBLOCK);sr,sw=os.pipe2(os.O_NONBLOCK)
   try:
    with self.fixture() as (s,origin,_):
     env=dict(os.environ)
     if invalid: env['BOOTSTRAP_NULL_ORIGIN']='1'
     if stopping: env['BOOTSTRAP_STOP_APPEND']='1'
     proc=subprocess.Popen([os.environ['GIN_ACCOUNT_HISTORY_BOOTSTRAP_DRIVER'],origin,str(self.certdir/'ca.pem'),str(root),str(w),str(sr)],env=env,pass_fds=(w,sr),stdout=subprocess.PIPE,stderr=subprocess.PIPE,start_new_session=True)
     received=b''
     def until(predicate,seconds):
      nonlocal received
      end=time.monotonic()+seconds
      while time.monotonic()<end:
       try: received+=os.read(r,4096)
       except BlockingIOError: pass
       if predicate(): return True
       if proc.poll() is not None: break
       time.sleep(.005)
      return False
     try:
      self.assertTrue(until(lambda:b'R' in received,3),'Native READY client receipt absent')
      healthy=len(s['joins'])==12
      quiet=False if invalid else until(s['quiet'].is_set,1.2)
      if invalid: pass
      elif baseline:
       os.write(sw,b'S')
      else:
       self.assertTrue(quiet,'quiet scope GET absent while all application frames held')
       self.assertTrue(until(lambda:b'A' in received and b'B' in received,3),'actual recovery LIVE admissions absent')
       s['release'].set()
      out,err=proc.communicate(timeout=10)
     finally:
      if proc.poll() is None: os.killpg(proc.pid,signal.SIGKILL)
      out,err=proc.communicate()
      print('CLIENT_RETURN_CODE',proc.returncode,'READY',b'R' in received,'JOINS',len(s['joins']),flush=True)
      self.assertEqual(proc.returncode,0,err)
     self.assertEqual(proc.returncode,0,err);self.assertEqual(err,b'');self.assertNotIn(wire.TOKEN.encode(),out+err)
     self.assertTrue(healthy);self.assertEqual(out,b'PASS borrowed handles retained; seven-destination fixture cleanup\n')
    self.assertEqual(s['eof'],[1]);self.assertEqual(s['live'],0)
    if invalid:
     self.assertEqual(s['paths'],['/api/config','/me','/socket/websocket?vsn=2.0.0&client_proto=4'])
     self.assertTrue(all(p.read_bytes()==b'' for p in root.rglob('out')))
     self.assertEqual(list(root.rglob('.gin.checkpoint')),[])
     return
    if baseline:
     self.assertEqual(s['paths'],['/api/config','/me','/socket/websocket?vsn=2.0.0&client_proto=4'])
     print('PASS baseline healthy Native READY/12 joins, orderly EOF and cleanup',flush=True)
     self.assertTrue(quiet,'quiet scope GET absent while all application frames held')
     return
    expected_paths=['/api/config','/me','/socket/websocket?vsn=2.0.0&client_proto=4']
    for net,target in (SCOPES[:4] if stopping else SCOPES): expected_paths.extend(['/api/config','/me',path(net,target)])
    self.assertEqual(s['paths'],expected_paths)
    def line(target,id): return ('[1970-01-01T00:00:00.123Z] '+('notice' if target=='$server' else 'privmsg')+' sender="sender" body="row'+str(id)+'"\n').encode()
    expected={net+'/'+('' if target=='$server' else ('@'+target.lower() if target in ('Peer','Other') else target)+'/')+'out':b'' if target=='#aquiet' else b''.join(line(target,id) for id in ((10,20,30) if target=='#old1' else (10,))) for net,target in SCOPES}
    if stopping:
     expected['one/#old1/out']=line('#old1',10)
     for key in ('one/@peer/out','two/#old2/out','two/@other/out'): expected[key]=b''
    self.assertEqual({str(p.relative_to(root)):p.read_bytes() for p in root.rglob('out')},expected)
    cps=list(root.rglob('.gin.checkpoint'));self.assertEqual(len(cps),3 if stopping else 6)
    for cp in cps:
     data=cp.read_bytes();log=(cp.parent/'out').stat()
     self.assertEqual(data[:8],b'GINCP\0\0\1');self.assertEqual(int.from_bytes(data[8:16],'big'),0);self.assertEqual(int.from_bytes(data[16:24],'big'),1)
     self.assertEqual(tuple(int.from_bytes(data[i:i+8],'big') for i in (24,32,40)),(log.st_dev,log.st_ino,log.st_size))
     self.assertIn(origin.encode(),data);self.assertIn(wire.ME['id'].encode(),data)
    self.assertFalse((root/'one/#aquiet/.gin.checkpoint').exists())
    for p in root.rglob('*'):
     st=p.lstat();self.assertEqual(stat.S_IMODE(st.st_mode),0o700 if stat.S_ISDIR(st.st_mode) else 0o600)
     if stat.S_ISREG(st.st_mode): self.assertEqual(st.st_nlink,1);self.assertNotIn(wire.TOKEN.encode(),p.read_bytes())
   finally:
    for fd in (r,w,sr,sw): os.close(fd)
class NullOrigin(Bootstrap):
 invalid_origin=True
 def test_receive(self):
  super().test_receive()
class StopAppend(Bootstrap):
 stop_append=True
 def test_receive(self):
  super().test_receive()
if __name__=='__main__': unittest.main(verbosity=2)
