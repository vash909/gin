#!/usr/bin/env python3
"""Private owner tracer: verified local HTTPS/WSS, not CLI qualification."""
import base64, contextlib, hashlib, json, os, signal, socket, stat
import subprocess, tempfile, threading, time, unittest
from pathlib import Path
import test_account_receive as wire
import test_account_history_bootstrap as support
from ws_fixture_frames import server_frame, take_client_frame
ROOT=Path(__file__).resolve().parents[1]
TOPICS=support.TOPICS
SCOPES=support.SCOPES
path=support.path
row=support.row
class Owner(wire.Replan):
 @contextlib.contextmanager
 def fixture(self):
  listener=socket.socket();listener.bind(('127.0.0.1',0));listener.listen(24);listener.settimeout(.05)
  ctx=self.tls();stop=threading.Event();quiet=threading.Event();held=threading.Event();release=threading.Event();late=threading.Event();state={'paths':[],'joins':[],'errors':[],'eof':[],'workers':[],'live':0,'quiet':quiet,'held':held,'release':release,'late':late}
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
     assert late.wait(4),'client history barrier absent'
     send([None,None,TOPICS[6],'event',{'kind':'message','message':row('one','#old1',40)}])
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
  with tempfile.TemporaryDirectory(prefix='gin-owner-',dir=os.environ.get('TMPDIR') or str(ROOT/'.build')) as tmp:
   root=Path(tmp)/'irc';r,w=os.pipe2(os.O_NONBLOCK);sr,sw=os.pipe2(os.O_NONBLOCK)
   received=b''
   def line(target,id):
    return ('[1970-01-01T00:00:00.123Z] '+('notice' if target=='$server' else 'privmsg')+' sender="sender" body="row'+str(id)+'"\n').encode()
   expected={net+'/'+('' if target=='$server' else ('@'+target.lower() if target in ('Peer','Other') else target)+'/')+'out':b'' if target=='#aquiet' else b''.join(line(target,id) for id in ((10,20,30) if target=='#old1' else (10,))) for net,target in SCOPES}
   try:
    with self.fixture() as (s,origin,_):
     proc=subprocess.Popen([os.environ['GIN_ACCOUNT_OWNER_DRIVER'],origin,str(self.certdir/'ca.pem'),str(root),str(w),str(sr)],pass_fds=(w,sr),stdout=subprocess.PIPE,stderr=subprocess.PIPE,start_new_session=True)
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
     def snapshot(): return {str(p.relative_to(root)):p.read_bytes() for p in root.rglob('out')}
     try:
      self.assertTrue(until(lambda:b'R' in received,4),'Native client READY/drained absent')
      self.assertTrue(until(s['held'].is_set,3),'held REST absent')
      time.sleep(.15);s['release'].set()
      self.assertTrue(until(lambda:b'H' in received,4),'client completed history barrier absent')
      self.assertEqual(len(s['joins']),12);self.assertEqual(snapshot(),expected)
      print('HEALTHY Native12joins bootstrap7dest initial8 client-history-complete',flush=True)
      s['late'].set()
      late=until(lambda:(root/'one/#old1/out').read_bytes()==expected['one/#old1/out']+line('#old1',40),1)
      os.write(sw,b'S');out,err=proc.communicate(timeout=4)
     finally:
      if proc.poll() is None: os.killpg(proc.pid,signal.SIGKILL)
      out,err=proc.communicate()
      print('CLIENT_RETURN_CODE',proc.returncode,'STATUS',received.decode(),flush=True)
     self.assertEqual(proc.returncode,0,err);self.assertEqual(err,b'');self.assertNotIn(wire.TOKEN.encode(),out+err)
    self.assertEqual(s['eof'],[1]);self.assertEqual(s['live'],0)
    expected_paths=['/api/config','/me','/socket/websocket?vsn=2.0.0&client_proto=4']
    for net,target in SCOPES: expected_paths.extend(['/api/config','/me',path(net,target)])
    self.assertEqual(s['paths'],expected_paths)
    print('CLEANUP orderly EOF strict handler ledger empty; client exited',flush=True)
    self.assertTrue(late,'missing POST-bootstrap reception row40 after healthy initial8 and cleanup')
    expected['one/#old1/out']+=line('#old1',40)
    self.assertEqual(snapshot(),expected)
    cps=list(root.rglob('.gin.checkpoint'));self.assertEqual(len(cps),6)
    for cp in cps:
     data=cp.read_bytes();log=(cp.parent/'out').stat()
     self.assertEqual(data[:8],b'GINCP\0\0\1');self.assertEqual(int.from_bytes(data[8:16],'big'),0)
     self.assertEqual(int.from_bytes(data[16:24],'big'),1)
     self.assertEqual(tuple(int.from_bytes(data[i:i+8],'big') for i in (24,32,40)),(log.st_dev,log.st_ino,log.st_size))
     self.assertIn(origin.encode(),data);self.assertIn(wire.ME['id'].encode(),data)
    self.assertFalse((root/'one/#aquiet/.gin.checkpoint').exists())
    for p in root.rglob('*'):
     st=p.lstat();self.assertEqual(stat.S_IMODE(st.st_mode),0o700 if stat.S_ISDIR(st.st_mode) else 0o600)
     if stat.S_ISREG(st.st_mode): self.assertEqual(st.st_nlink,1);self.assertNotIn(wire.TOKEN.encode(),p.read_bytes())
   finally:
    for fd in (r,w,sr,sw): os.close(fd)
if __name__=='__main__': unittest.main(verbosity=2)
