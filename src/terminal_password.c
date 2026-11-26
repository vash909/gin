#define _POSIX_C_SOURCE 200809L
#include "terminal_password.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>
static volatile sig_atomic_t caught;
static void cancel_read(int sig) { if (!caught) caught=sig; }
void terminal_password_wipe(void *p, size_t n) {
 volatile unsigned char *v=p;
 while (n--) *v++=0;
}
static int valid_utf8(const unsigned char *s, size_t n) {
 size_t i=0;
 while (i<n) {
  unsigned long cp; unsigned int more, j; unsigned char c=s[i++];
  if (c<128) { if (!c) return 0; continue; }
  if (c>=0xc2 && c<=0xdf) { cp=c&31; more=1; }
  else if (c>=0xe0 && c<=0xef) { cp=c&15; more=2; }
  else if (c>=0xf0 && c<=0xf4) { cp=c&7; more=3; }
  else return 0;
  if (n-i<more) return 0;
  for (j=0;j<more;j++) {
   c=s[i++]; if ((c&0xc0)!=0x80) return 0; cp=(cp<<6)|(c&63);
  }
  if ((more==1 && cp<128) || (more==2 && cp<2048) ||
      (more==3 && cp<65536) || cp>0x10ffff ||
      (cp>=0xd800 && cp<=0xdfff)) return 0;
 }
 return n!=0;
}
enum tp_status terminal_password_read(char out[4097], size_t *length,
                                      int *interrupted) {
 static const int signals[]={SIGINT,SIGTERM,SIGHUP,SIGQUIT,SIGTSTP,SIGTTIN,SIGTTOU};
 struct sigaction old[7], action; sigset_t blocked, original;
 struct termios saved, hidden; size_t n=0, installed=0, i;
 int fd=-1, masked=0, changed=0, restore_failed=0, overflow=0;
 unsigned char c=0; enum tp_status result=TP_TTY;
 if (out) terminal_password_wipe(out,4097);
 if (length) *length=0;
 if (interrupted) *interrupted=0;
 if (!out || !length || !interrupted) return TP_ARGUMENT;
 sigemptyset(&blocked);
 for (i=0;i<7;i++) sigaddset(&blocked,signals[i]);
 if (sigprocmask(SIG_BLOCK,&blocked,&original)<0) return TP_IO;
 masked=1; caught=0;
 fd=open("/dev/tty",O_RDWR|O_NOCTTY|O_CLOEXEC|O_NONBLOCK);
 if (fd<0) goto done;
 if (tcgetpgrp(fd)!=getpgrp() || tcgetattr(fd,&saved)<0) goto done;
 action.sa_handler=cancel_read; action.sa_flags=0; sigemptyset(&action.sa_mask);
 for (i=0;i<7;i++) {
  if (sigaction(signals[i],&action,&old[i])<0) { result=TP_IO; goto done; }
  installed++;
 }
 hidden=saved;
 hidden.c_lflag &= (tcflag_t)~(ECHO|ECHONL|ICANON|IEXTEN);
 hidden.c_lflag |= ISIG;
 hidden.c_iflag &= (tcflag_t)~(IGNCR|INLCR|ICRNL|IXON|IXOFF|ISTRIP|PARMRK);
 hidden.c_cc[VMIN]=1; hidden.c_cc[VTIME]=0;
 if (tcsetattr(fd,TCSAFLUSH,&hidden)<0) { result=TP_IO; goto done; }
 changed=1;
 /* Unblock cancellation atomically while waiting. Nonblocking I/O handles
  * stale readiness; caller dispositions are restored during cleanup. */
 if (fd>=FD_SETSIZE) { result=TP_IO; goto done; }
 {
  static const char prompt[]="Password: ";
  size_t offset=0;
  while (offset<sizeof prompt-1) {
   fd_set ready; int selected; ssize_t written;
   if (caught) { result=TP_INTERRUPTED; goto done; }
   FD_ZERO(&ready); FD_SET(fd,&ready);
   selected=pselect(fd+1,NULL,&ready,NULL,NULL,&original);
   if (caught) { result=TP_INTERRUPTED; goto done; }
   if (selected<0) {
    if (errno==EINTR) continue;
    result=TP_IO; goto done;
   }
   if (!selected) continue;
   written=write(fd,prompt+offset,sizeof prompt-1-offset);
   if (written<0) {
    if (errno==EINTR || errno==EAGAIN || errno==EWOULDBLOCK) continue;
    result=TP_IO; goto done;
   }
   if (!written) { result=TP_IO; goto done; }
   offset+=(size_t)written;
  }
 }
 for (;;) {
  ssize_t r; fd_set ready; int selected;
  if (caught) { result=TP_INTERRUPTED; break; }
  if (fd>=FD_SETSIZE) { result=TP_IO; break; }
  FD_ZERO(&ready); FD_SET(fd,&ready);
  selected=pselect(fd+1,&ready,NULL,NULL,NULL,&original);
  if (caught) { result=TP_INTERRUPTED; break; }
  if (selected<0) { if (errno==EINTR) continue; result=TP_IO; break; }
  if (!selected) continue;
  r=read(fd,&c,1);
  if (caught) { result=TP_INTERRUPTED; break; }
  if (r==0) { result=TP_EOF; break; }
  if (r<0) {
   if (errno==EINTR || errno==EAGAIN || errno==EWOULDBLOCK) continue;
   result=TP_IO; break;
  }
  if (c==4) { result=TP_EOF; break; }
  if (c=='\n' || c=='\r') {
   result=overflow?TP_OVERLONG:(valid_utf8((unsigned char *)out,n)?TP_OK:TP_INVALID);
   break;
  }
  if (overflow) continue;
  if (c==127 || c==8) {
   if (n) { do { n--; } while (n && ((unsigned char)out[n]&0xc0)==0x80); }
   out[n]=0; continue;
  }
  if (c==21) { terminal_password_wipe(out,4097); n=0; continue; }
  if (n==4096) { overflow=1; continue; }
  out[n++]=(char)c; out[n]=0;
 }
 done:
 if (!masked) {
  if (sigprocmask(SIG_BLOCK,&blocked,NULL)<0) restore_failed=1;
  else masked=1;
 }
 if (caught) { *interrupted=(int)caught; if (result==TP_OK) result=TP_INTERRUPTED; }
 terminal_password_wipe(&c,sizeof c);
 if (result!=TP_OK) terminal_password_wipe(out,4097);
 if (changed) {
  if (result!=TP_OK && tcflush(fd,TCIFLUSH)<0) restore_failed=1;
  if (tcsetattr(fd,TCSANOW,&saved)<0) restore_failed=1;
 }
 if (restore_failed) terminal_password_wipe(out,4097);
 while (installed) {
  installed--;
  if (sigaction(signals[installed],&old[installed],NULL)<0) {
   restore_failed=1; terminal_password_wipe(out,4097);
  }
 }
 if (fd>=0) (void)close(fd);
 if (sigprocmask(SIG_SETMASK,&original,NULL)<0) restore_failed=1;
 if (restore_failed) result=TP_RESTORE;
 terminal_password_wipe(&c,sizeof c);
 if (result==TP_OK) *length=n;
 else terminal_password_wipe(out,4097);
 return result;
}
