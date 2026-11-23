#!/usr/bin/env python3
"""Real PTYs; child stdin is /dev/null. No password values are printed."""
import os, pty, select, signal, subprocess, sys, termios, time, unittest
BINARY = os.environ['TP_DRIVER']
class PasswordTests(unittest.TestCase):
 def run_case(self, payload, status=0, length=0, sig=None):
  master, slave=pty.openpty(); before=termios.tcgetattr(slave)
  def setup():
   os.setsid()
   import fcntl
   fcntl.ioctl(slave, termios.TIOCSCTTY, 0)
  child=subprocess.Popen([BINARY],stdin=subprocess.DEVNULL,stdout=slave,stderr=slave,pass_fds=(slave,),preexec_fn=setup)
  data=b''; end=time.monotonic()+3
  try:
   while b'Password: ' not in data and time.monotonic()<end:
    if select.select([master],[],[],.05)[0]: data+=os.read(master,8192)
   self.assertIn(b'Password: ',data,'prompt deadline')
   if sig is not None: os.kill(child.pid,sig)
   else: os.write(master,payload)
   while child.poll() is None and time.monotonic()<end:
    if select.select([master],[],[],.05)[0]: data+=os.read(master,16384)
   child.wait(timeout=max(.01,end-time.monotonic()))
   self.assertEqual(child.returncode,0)
   while select.select([master],[],[],.02)[0]: data+=os.read(master,16384)
   self.assertEqual(termios.tcgetattr(slave),before,'exact termios restoration')
   self.assertNotIn(b'HiddenSentinel',data,'password echoed')
   self.assertIn(('status=%d length=%d '%(status,length)).encode(),data)
   self.assertIn(b'restored=1',data)
   if status: self.assertIn(b'cleared=1',data)
   else: self.assertIn(b'match=1',data)
   if sig is not None: self.assertIn(('signal=%d'%sig).encode(),data)
  finally:
   if child.poll() is None: os.killpg(child.pid,signal.SIGKILL)
   child.wait(timeout=1); os.close(master); os.close(slave)
 def test_hidden_success_nostdin(self): self.run_case(b'HiddenSentinel\n',length=14)
 def test_bounds(self):
  self.run_case(b'x'*4096+b'\n',length=4096)
  self.run_case(b'x'*4097+b'HiddenSentinel\n',status=5)
 def test_empty_eof_utf8(self):
  self.run_case(b'\n',status=4)
  self.run_case(b'abc\x04',status=3)
  self.run_case(b'\xc0\x80\n',status=4)
 def test_edit_whitespace(self): self.run_case(b' \xc3\xa9\x7fok \n',length=4)
 def test_no_controlling_tty(self):
  r=subprocess.run([BINARY],stdin=subprocess.DEVNULL,stdout=subprocess.PIPE,stderr=subprocess.PIPE,start_new_session=True,timeout=2)
  self.assertIn(b'status=2 length=0 signal=0 cleared=1 restored=1',r.stdout)
  self.assertEqual(r.returncode,0)
 def test_cancel(self):
  for s in (signal.SIGINT,signal.SIGTERM,signal.SIGHUP,signal.SIGQUIT,signal.SIGTSTP,signal.SIGTTIN,signal.SIGTTOU):
   self.run_case(b'',status=6,sig=s)
class FaultTests(unittest.TestCase):
 def fault(self, mode, control=False):
  master,slave=pty.openpty(); before=termios.tcgetattr(slave)
  def setup():
   os.setsid()
   import fcntl
   fcntl.ioctl(slave,termios.TIOCSCTTY,0)
  argv=[os.environ['TP_FAULT_DRIVER'],str(mode)]+(['off'] if control else [])
  child=subprocess.Popen(argv,stdin=subprocess.DEVNULL,stdout=slave,stderr=slave,pass_fds=(slave,),preexec_fn=setup)
  data=b''; deadline=time.monotonic()+1.5
  try:
   sent=False
   while time.monotonic()<deadline:
    if select.select([master],[],[],.02)[0]: data+=os.read(master,16384)
    if (control or mode!=3) and not sent and b'Password: ' in data:
     payload=b'HiddenSentinel\n' if control else (b'x' if mode==1 else (b'HiddenSentinel\x04' if mode==4 else b'HiddenSentinel\n'))
     os.write(master,payload); sent=True
    if child.poll() is not None: break
   self.assertIsNotNone(child.poll(),'semantic cancellation deadline (stale readiness)')
   child.wait(timeout=.1)
   while select.select([master],[],[],.01)[0]: data+=os.read(master,16384)
   self.assertEqual(child.returncode,0,'fault driver failed; synthetic PTY transcript omitted')
   self.assertIn(('fault=%d ok=1'%mode).encode(),data)
   self.assertIn(('control=%d restored=1 nonblocking=1'%int(control)).encode(),data)
   if mode==5:
    self.assertIn(b'policy=5 status=0 match=1 interrupted=0 blocked=1 pending=1 consumed=1 stop=0',data)
    self.assertIn(b'handled=0',data)
   if mode==6:
    self.assertIn(('handled=%d'%signal.SIGINT).encode(),data)
    self.assertIn(b'policy=6 status=0 match=1 interrupted=0',data)
    self.assertIn(b'stop=1 caller_would_authenticate=0',data)
   self.assertEqual(termios.tcgetattr(slave),before)
   self.assertNotIn(b'HiddenSentinel',data)
  finally:
   if child.poll() is None: os.killpg(child.pid,signal.SIGKILL)
   child.wait(timeout=1)
   termios.tcsetattr(slave,termios.TCSANOW,before)
   os.close(master); os.close(slave)
 def test_stale_readiness(self): self.fault(1)
 def test_prompt_backpressure(self): self.fault(2)
 def test_prompt_pending_cancel(self): self.fault(3)
 def test_wipe_before_unmask(self): self.fault(4)
 # Permanent policy characterizations, not production fixes or login tests.
 def test_policy_originally_blocked_cancel(self): self.fault(5)
 def test_policy_late_pending_submit_stop_gate(self): self.fault(6)
 def test_clean_controls(self):
  for mode in range(1,5): self.fault(mode,control=True)
if __name__=='__main__': unittest.main()
