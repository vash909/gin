#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>
static int mode, disabled, fired, writes, saw_short, checked, clean=1, nonblocking=1;
static char *output;
static unsigned char *scratch;
static volatile sig_atomic_t handled, stop;
/* Only sig_atomic_t stores: no handler-side logging or buffer inspection. */
static void caller(int sig) { handled=(sig_atomic_t)sig; stop=1; }
static int seam_select(int n, fd_set *r, fd_set *w, fd_set *e,
                       const struct timespec *t, const sigset_t *m) {
 int result, fd;
 if(!disabled && mode==5 && fired && !checked) {
  sigset_t pending;
  if(!m || sigismember(m,SIGINT)!=1 || sigpending(&pending)<0 ||
     sigismember(&pending,SIGINT)!=1) _exit(97);
  checked=1;
 }
 result=pselect(n,r,w,e,t,m);
 if (!disabled && mode==1 && result>0 && r && !fired) {
  unsigned char byte;
  for(fd=0;fd<n;fd++) if(FD_ISSET(fd,r)) break;
  if(fd==n || read(fd,&byte,1)!=1) _exit(90);
  fired=1; if(kill(getpid(),SIGINT)<0) _exit(91);
 }
 return result;
}
static ssize_t seam_write(int fd,const void *p,size_t n) {
 if(fd!=STDOUT_FILENO && !(fcntl(fd,F_GETFL)&O_NONBLOCK)) nonblocking=0;
 if(!disabled && (mode==2 || mode==3) && n && fd!=STDOUT_FILENO) {
  writes++;
  if(writes==1) { errno=EAGAIN; return -1; }
  if(writes==2 && n>1) { saw_short=1; return write(fd,p,1); }
  if(mode==3 && writes==3) {
   fired=1; if(kill(getpid(),SIGINT)<0) _exit(92);
   errno=EWOULDBLOCK; return -1;
  }
 }
 return write(fd,p,n);
}
static ssize_t seam_read(int fd,void *p,size_t n) {
 ssize_t result=read(fd,p,n);
 if(!(fcntl(fd,F_GETFL)&O_NONBLOCK)) nonblocking=0;
 scratch=p;
 /* Queue after FIRST real normal read, before the next pselect. */
 if(!disabled && mode==5 && !fired && result==1) {
  fired=1; if(kill(getpid(),SIGINT)<0) _exit(98);
 }
 return result;
}
static int seam_mask(int how,const sigset_t *set,sigset_t *old) {
 if(!disabled && mode==4 && how==SIG_SETMASK && set && output && !checked) {
  size_t i; checked=1;
  for(i=0;i<4097;i++) if(output[i]) clean=0;
  if(!scratch || *scratch) clean=0;
  if(kill(getpid(),SIGINT)<0) _exit(93);
 }
 if(!disabled && mode==6 && how==SIG_SETMASK && set && output && !checked) {
  static const int cancels[]={SIGINT,SIGTERM,SIGHUP,SIGQUIT,SIGTSTP,SIGTTIN,SIGTTOU};
  sigset_t current,pending; size_t i;
  if(sigprocmask(SIG_BLOCK,NULL,&current)<0) _exit(99);
  for(i=0;i<7;i++) if(sigismember(&current,cancels[i])!=1) _exit(99);
  if(sigismember(set,SIGINT)!=0 || memcmp(output,"HiddenSentinel",14) || output[14]) _exit(99);
  /* Late pending cancellation belongs to the restored caller handler. */
  if(kill(getpid(),SIGINT)<0 || sigpending(&pending)<0 ||
     sigismember(&pending,SIGINT)!=1) _exit(99);
  fired=1; checked=1;
 }
 return sigprocmask(how,set,old);
}
#define pselect seam_select
#define write seam_write
#define read seam_read
#define sigprocmask seam_mask
#include "../src/terminal_password.c"
#undef pselect
#undef write
#undef read
#undef sigprocmask
int main(int argc,char **argv) {
 char b[4097]; size_t n=0; int sig=0,ok; enum tp_status status;
 static const int signals[]={SIGINT,SIGTERM,SIGHUP,SIGQUIT,SIGTSTP,SIGTTIN,SIGTTOU};
 struct sigaction action,before[7],after;
 sigset_t initial,mask_before,mask_after,pending,one;
 size_t i; int bit,restored=1,blocked=0,pending_int=0,consumed=0,match;
 int caller_would_authenticate=0;
 if(argc<2 || argc>3 || argv[1][0]<'0' || argv[1][0]>'6' || argv[1][1]) return 95;
 if(argc==3) { if(strcmp(argv[2],"off") || argv[1][0]>'4') return 95; disabled=1; }
 mode=argv[1][0]-'0'; output=b;
 memset(&action,0,sizeof action); action.sa_handler=caller;
 sigemptyset(&action.sa_mask);
 if(sigaction(SIGINT,&action,NULL)<0) return 96;
 if(sigprocmask(SIG_BLOCK,NULL,&initial)<0) return 96;
 sigemptyset(&one); sigaddset(&one,SIGINT);
 if(mode==5 && sigprocmask(SIG_BLOCK,&one,NULL)<0) return 96;
 if(mode==6 && sigprocmask(SIG_UNBLOCK,&one,NULL)<0) return 96;
 if(sigprocmask(SIG_BLOCK,NULL,&mask_before)<0) return 96;
 /* Normalize libc-private action state before taking the exact baseline. */
 for(i=0;i<7;i++) {
  if(sigaction(signals[i],NULL,&before[i])<0 ||
     sigaction(signals[i],&before[i],NULL)<0 ||
     sigaction(signals[i],NULL,&before[i])<0) return 96;
 }
 status=terminal_password_read(b,&n,&sig);
 if(sigprocmask(SIG_BLOCK,NULL,&mask_after)<0) { restored=0; sigemptyset(&mask_after); }
 for(bit=1;bit<=(int)(sizeof(sigset_t)*CHAR_BIT);bit++) if(sigismember(&mask_before,bit)!=sigismember(&mask_after,bit)) restored=0;
 for(i=0;i<7;i++) {
  if(sigaction(signals[i],NULL,&after)<0) { restored=0; continue; }
  if(before[i].sa_handler!=after.sa_handler || before[i].sa_flags!=after.sa_flags) restored=0;
  for(bit=1;bit<=(int)(sizeof(sigset_t)*CHAR_BIT);bit++) if(sigismember(&before[i].sa_mask,bit)!=sigismember(&after.sa_mask,bit)) restored=0;
 }
 match=(n==14 && memcmp(b,"HiddenSentinel",14)==0 && b[14]==0);
 ok=(disabled || mode==0 || mode==2)?(status==TP_OK && match):
    (mode==4?(status==TP_EOF && checked && clean && handled==SIGINT):
     (status==TP_INTERRUPTED && sig==SIGINT && fired));
 if(mode==5) {
  blocked=sigismember(&mask_after,SIGINT)==1;
  pending_int=sigpending(&pending)==0 && sigismember(&pending,SIGINT)==1;
  ok=(status==TP_OK && match && sig==0 && fired && checked &&
      blocked && pending_int && handled==0 && stop==0);
  /* No competing consumer; never sigwait without checked blocked+pending.
   * The real PTY parent independently bounds, group-kills and reaps us. */
  if(blocked && pending_int) {
   int received=0;
   consumed=(sigwait(&one,&received)==0 && received==SIGINT);
   if(sigpending(&pending)<0 || sigismember(&pending,SIGINT)!=0) consumed=0;
  }
  if(!consumed || handled || stop) ok=0;
 }
 if(mode==6) {
  /* Boolean policy model ONLY: no authentication or network operation. */
  caller_would_authenticate=(status==TP_OK && !stop);
  ok=(status==TP_OK && match && sig==0 && fired && checked &&
      handled==SIGINT && stop==1 && !caller_would_authenticate);
  if(sigpending(&pending)<0 || sigismember(&pending,SIGINT)!=0) ok=0;
 }
 if(!disabled && mode==2 && (!saw_short || writes<3)) ok=0;
 if(!restored || !nonblocking) ok=0;
 terminal_password_wipe(b,sizeof b);
 /* Consume checked pending SIGINT above before restoring our own entry mask. */
 if(sigprocmask(SIG_SETMASK,&initial,NULL)<0) ok=0;
 printf("fault=%d ok=%d control=%d restored=%d nonblocking=%d checked=%d clean=%d handled=%d\n",mode,ok,disabled,restored,nonblocking,checked,clean,(int)handled);
 if(mode>=5) printf("policy=%d status=%d match=%d interrupted=%d blocked=%d pending=%d consumed=%d stop=%d caller_would_authenticate=%d\n",mode,(int)status,match,sig,blocked,pending_int,consumed,(int)stop,caller_would_authenticate);
 return ok?0:1;
}
