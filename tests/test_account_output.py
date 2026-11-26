#!/usr/bin/env python3
"""Local HTTPS/WSS storage fixture; not history recovery or CLI readiness."""
import os
import signal
import stat
import subprocess
import tempfile
import unittest
from pathlib import Path
import test_account_dispatch as wire

class Output(wire.Replan):
 def test_receive(self):
  self.run_storage(None)
 def test_replace(self):
  self.run_storage("replace")
 def test_partial(self):
  self.run_storage("partial")
 def run_storage(self, fault):
  with tempfile.TemporaryDirectory(prefix="gin-output-",dir=os.environ.get("TMPDIR") or str(wire.ROOT/".build")) as temp:
   root=Path(temp)/"irc"
   with self.fixture() as (state,origin,_gate):
    env=dict(os.environ,GIN_DISPATCH_CASE="positive")
    env.pop("GIN_OUTPUT_CASE",None)
    if fault: env["GIN_OUTPUT_CASE"]=fault
    p=subprocess.Popen([os.environ["GIN_ACCOUNT_OUTPUT_DRIVER"],origin,str(self.certdir/"ca.pem"),str(root)],stdout=subprocess.PIPE,stderr=subprocess.PIPE,start_new_session=True,env=env)
    try: out,err=p.communicate(timeout=18)
    except BaseException:
     os.killpg(p.pid,signal.SIGKILL);p.communicate();raise
    self.assertEqual(p.returncode,0,err);self.assertEqual(err,b"")
    self.assertEqual(out,b"PASS exact6 destinations bytes IDs local-drain\n"+(b"PASS storage boundary sticky unchanged-checkpoint ambiguous-reopen\n" if fault else b""))
    self.assertNotIn(wire.TOKEN.encode(),out+err);self.assertNotIn(b"hello",out+err)
   logs={str(p.relative_to(root)):p.read_bytes() for p in root.rglob("out")}
   def line(kind): return ('[1970-01-01T00:00:00.123Z] '+kind+' sender="sender" body="hello"\n').encode()
   baseline=line("server_event")+b'[1970-01-01T00:00:00.123Z] notice sender="trusted" body="prefix"\n'
   expected={"one/out":baseline,"two/out":line("notice"),"one/#old1/out":line("privmsg"),"two/#old2/out":line("privmsg"),"one/@peer/out":line("privmsg")*2}
   if fault=="partial": expected["one/out"]+=b"[1970"
   if fault=="replace":
    expected["one/out"]=b"replacement\n"
    self.assertEqual((root/"one/out.saved").read_bytes(),baseline)
    self.assertNotEqual((root/"one/out.saved").stat().st_ino,(root/"one/out").stat().st_ino)
   self.assertEqual(logs,expected)
   cps=list(root.rglob(".gin.checkpoint"));self.assertEqual(len(cps),5)
   for cp in cps:
    data=cp.read_bytes(); st=cp.stat(); log=(cp.parent/("out.saved" if fault=="replace" and cp.parent==root/"one" else "out")).stat()
    self.assertEqual(st.st_mode&0o7777,0o600);self.assertEqual(st.st_nlink,1)
    self.assertEqual(data[:8],b"GINCP\0\0\1")
    self.assertEqual(int.from_bytes(data[32:40],"big"),log.st_ino)
    self.assertEqual(int.from_bytes(data[24:32],"big"),log.st_dev)
    self.assertEqual(int.from_bytes(data[40:48],"big"),log.st_size-(5 if fault=="partial" and cp.parent==root/"one" else 0))
    self.assertEqual(int.from_bytes(data[8:16],"big"),7 if cp.parent==root/"one" else 0)
    self.assertEqual(int.from_bytes(data[16:24],"big"),1)
    self.assertIn(b"22345678-1234-1234-1234-123456789abc",data)
    self.assertIn(origin.encode(),data);self.assertNotIn(wire.TOKEN.encode(),data)
   for entry in root.rglob("*"):
    st=entry.lstat()
    self.assertEqual(stat.S_IMODE(st.st_mode),0o700 if stat.S_ISDIR(st.st_mode) else 0o600)
    if stat.S_ISREG(st.st_mode):
     self.assertEqual(st.st_nlink,1)
     self.assertNotIn(wire.TOKEN.encode(),entry.read_bytes())
   self.assertEqual(state["eof"],[1])
   self.assertEqual(state["paths"],["/api/config","/me","/socket/websocket?vsn=2.0.0&client_proto=4"])
   self.assertEqual(len(state["joins"]),10)
   self.assertEqual(state["queries"],[])
if __name__=="__main__": unittest.main(verbosity=2)
