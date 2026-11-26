#define _POSIX_C_SOURCE 200809L
#include "terminal_password.h"
#include <stdio.h>
#include <string.h>
#include <signal.h>
static const int signals[]={SIGINT,SIGTERM,SIGHUP,SIGQUIT,SIGTSTP,SIGTTIN,SIGTTOU};
int main(void) {
 char b[4097]; size_t n=0, i; int sig=0, restored=1, cleared=1;
 enum tp_status s; struct sigaction before[7], after; sigset_t mb, ma;
 /* Seed via libc first: Linux adds its private SA_RESTORER on writes.\n  * Compare read-back dispositions, not an uninitialized kernel default. */
 memset(&after,0,sizeof after); after.sa_handler=SIG_IGN;
 after.sa_flags=SA_RESTART; sigemptyset(&after.sa_mask);
 sigaddset(&after.sa_mask,SIGUSR1);
 for(i=0;i<7;i++) if(sigaction(signals[i],&after,NULL)<0) return 1;
 sigemptyset(&mb); sigaddset(&mb,SIGUSR2);
 if(sigprocmask(SIG_BLOCK,&mb,NULL)<0) return 1;
 for(i=0;i<7;i++) if(sigaction(signals[i],NULL,&before[i])<0) return 1;
 if(sigprocmask(SIG_SETMASK,NULL,&mb)<0) return 1;
 memset(b,0x55,sizeof b); s=terminal_password_read(b,&n,&sig);
 if(sigprocmask(SIG_SETMASK,NULL,&ma)<0) return 1;
 for(i=1;i<32;i++) if(sigismember(&mb,(int)i)!=sigismember(&ma,(int)i)) restored=0;
 for(i=0;i<7;i++) {
  int j;
  if(sigaction(signals[i],NULL,&after)<0) return 1;
  if(before[i].sa_handler!=after.sa_handler || before[i].sa_flags!=after.sa_flags) restored=0;
  for(j=1;j<32;j++) if(sigismember(&before[i].sa_mask,j)!=sigismember(&after.sa_mask,j)) restored=0;
 }
 for(i=0;i<sizeof b;i++) if(b[i]) cleared=0;
 {
  int match=0;
  if(s==TP_OK) {
   if(n==14 && memcmp(b,"HiddenSentinel",14)==0) match=1;
   if(n==4 && memcmp(b," ok ",4)==0) match=1;
   if(n==4096) { match=1; for(i=0;i<n;i++) if(b[i]!='x') match=0; }
  }
  printf("status=%d length=%lu signal=%d cleared=%d restored=%d match=%d\n",(int)s,(unsigned long)n,sig,cleared,restored,match);
 }
 terminal_password_wipe(b,sizeof b); return 0;
}
