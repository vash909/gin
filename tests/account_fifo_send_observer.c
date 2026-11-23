#define _POSIX_C_SOURCE 200809L
#include "../src/account_owner.h"
#include "../src/http_control.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
static int receipt_fd=-1;
static int part_receipt;
static void prepare(void)
{
 const char *s=getenv("GIN_FIFO_STATUS_FD");struct stat st;char *end;long n;
 if(!s) _exit(91);
 n=strtol(s,&end,10);
 if(*end || n<3 || n>1024 || fstat((int)n,&st) || !S_ISFIFO(st.st_mode) || st.st_uid!=getuid() || !(fcntl((int)n,F_GETFL)&O_NONBLOCK)) _exit(92);
 receipt_fd=(int)n;part_receipt=0;
}
static struct gin_send_result observed_send(const struct gin_root *root,
 const struct gin_send_settings *settings,const struct gin_credential *credential,
 const struct gin_send_route *route,uint64_t id,const unsigned char *body,
 size_t length,struct gin_http_control *control)
{
 char b[160];int n;struct gin_send_result r;
 r=gin_send_plain_controlled(root,settings,credential,route,id,body,length,control);
 n=snprintf(b,sizeof(b),"T %llu %d %d %ld %d\n",(unsigned long long)r.request_id,(int)r.code,(int)r.outcome,r.http_status,r.post_attempted);
 if(n<0 || (size_t)n>=sizeof(b) || write(receipt_fd,b,(size_t)n)!=n) _exit(93);
 return r;
}
static struct gin_control_result observed_join(struct gin_root *root,
 const struct gin_send_settings *settings,const struct gin_credential *credential,
 const struct gin_send_route *route,uint64_t id,struct gin_http_control *control)
{
 char b[160];int n;struct gin_control_result r;
 r=gin_control_join_controlled(root,settings,credential,route,id,control);
 part_receipt=0;
 n=snprintf(b,sizeof(b),"JT %llu %d %d %ld %d\n",(unsigned long long)r.operation_id,(int)r.code,(int)r.outcome,r.http_status,r.attempted);
 if(n<0 || (size_t)n>=sizeof(b) || write(receipt_fd,b,(size_t)n)!=n) _exit(95);
 return r;
}
#define gin_control_join_controlled observed_join
static struct gin_control_result observed_part(struct gin_root *root,
 const struct gin_send_settings *settings,const struct gin_credential *credential,
 const struct gin_send_route *route,uint64_t id,struct gin_http_control *control)
{
 char b[160];int n;struct gin_control_result r;
 r=gin_control_part_controlled(root,settings,credential,route,id,control);
 part_receipt=1;
 n=snprintf(b,sizeof(b),"PT %llu %d %d %ld %d\n",(unsigned long long)r.operation_id,(int)r.code,(int)r.outcome,r.http_status,r.attempted);
 if(n<0 || (size_t)n>=sizeof(b) || write(receipt_fd,b,(size_t)n)!=n) _exit(97);
 return r;
}
#define gin_control_part_controlled observed_part
#define gin_send_plain_controlled observed_send
static int observed_query_take(struct gin_account_receive *a,struct gin_query_result *out)
{
 char b[160];int n,code=gin_account_receive_query_take(a,out);
 if(code==1) {
  n=snprintf(b,sizeof(b),"QT %llu %d %d %llu %s %s\n",(unsigned long long)out->operation_id,(int)out->code,(int)out->outcome,(unsigned long long)out->socket_epoch,out->join_ref,out->request_ref);
  if(n<0 || (size_t)n>=sizeof(b) || write(receipt_fd,b,(size_t)n)!=n) _exit(98);
 }
 return code;
}
#define gin_account_receive_query_take observed_query_take
#define gin_account_owner_run_fifo private_fifo
#include "../src/account_owner.c"
#undef gin_account_owner_run_fifo
#undef gin_send_plain_controlled
#undef gin_control_join_controlled
#undef gin_control_part_controlled
#undef gin_account_receive_query_take
struct gin_account_fifo_result gin_account_owner_run_fifo(const struct gin_account_owner_input *in)
{
 char b[160];int n;struct gin_account_fifo_result r;
 prepare();r=private_fifo(in);
 if(r.has_query_terminal) {
  n=snprintf(b,sizeof(b),"QF %llu %d %d %llu %s %s\n",(unsigned long long)r.query_terminal.operation_id,(int)r.query_terminal.code,(int)r.query_terminal.outcome,(unsigned long long)r.query_terminal.socket_epoch,r.query_terminal.join_ref,r.query_terminal.request_ref);
  if(n<0 || (size_t)n>=sizeof(b) || write(receipt_fd,b,(size_t)n)!=n) _exit(99);
 }
 n=snprintf(b,sizeof(b),"F %d %llu %d %d %d\n",r.has_terminal,(unsigned long long)r.operations,(int)r.terminal.code,(int)r.terminal.outcome,r.owner.stopped);
 if(n<0 || (size_t)n>=sizeof(b) || write(receipt_fd,b,(size_t)n)!=n) _exit(94);
 if(r.has_control_terminal) {
  n=snprintf(b,sizeof(b),"%s %d %llu %d %d %ld %d %d\n",part_receipt ? "PF" : "JF",r.has_control_terminal,(unsigned long long)r.control_terminal.operation_id,(int)r.control_terminal.code,(int)r.control_terminal.outcome,r.control_terminal.http_status,r.control_terminal.attempted,r.owner.stopped);
  if(n<0 || (size_t)n>=sizeof(b) || write(receipt_fd,b,(size_t)n)!=n) _exit(96);
 }
 return r;
}
