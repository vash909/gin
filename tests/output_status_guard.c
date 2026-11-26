#define _POSIX_C_SOURCE 200809L
#include "account_output_hot.h"
#include "account_sinks_hot.h"
#include "checkpoint.h"
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdarg.h>
#include <time.h>
#include <sys/time.h>
#include <sys/stat.h>
int gin_status_guard_armed;
int gin_status_fail_openat;
static void trap(const char *name) { if(gin_status_guard_armed) { gin_status_guard_armed=0;fprintf(stderr,"FORBIDDEN_GETTER_OPERATION:%s\n",name);exit(86); } }
extern void * __real_malloc(size_t n);
void * __wrap_malloc(size_t n) { trap("malloc");return __real_malloc(n); }
extern void * __real_calloc(size_t a,size_t b);
void * __wrap_calloc(size_t a,size_t b) { trap("calloc");return __real_calloc(a,b); }
extern void * __real_realloc(void *p,size_t n);
void * __wrap_realloc(void *p,size_t n) { trap("realloc");return __real_realloc(p,n); }
extern void __real_free(void *p);
void __wrap_free(void *p) { trap("free");__real_free(p); }
extern ssize_t __real_read(int f,void *p,size_t n);
ssize_t __wrap_read(int f,void *p,size_t n) { trap("read");return __real_read(f,p,n); }
extern ssize_t __real_write(int f,const void *p,size_t n);
ssize_t __wrap_write(int f,const void *p,size_t n) { trap("write");return __real_write(f,p,n); }
extern int __real_close(int f);
int __wrap_close(int f) { trap("close");return __real_close(f); }
extern int __real_fstat(int f,struct stat *s);
int __wrap_fstat(int f,struct stat *s) { trap("fstat");return __real_fstat(f,s); }
extern int __real_fstatat(int f,const char *p,struct stat *s,int flags);
int __wrap_fstatat(int f,const char *p,struct stat *s,int flags) { trap("fstatat");return __real_fstatat(f,p,s,flags); }
extern off_t __real_lseek(int f,off_t o,int w);
off_t __wrap_lseek(int f,off_t o,int w) { trap("lseek");return __real_lseek(f,o,w); }
extern int __real_fsync(int f);
int __wrap_fsync(int f) { trap("fsync");return __real_fsync(f); }
extern int __real_clock_gettime(clockid_t c,struct timespec *t);
int __wrap_clock_gettime(clockid_t c,struct timespec *t) { trap("clock_gettime");return __real_clock_gettime(c,t); }
extern time_t __real_time(time_t *t);
time_t __wrap_time(time_t *t) { trap("time");return __real_time(t); }
extern int __real_gettimeofday(struct timeval *t,void *z);
int __wrap_gettimeofday(struct timeval *t,void *z) { trap("gettimeofday");return __real_gettimeofday(t,z); }
extern struct gin_account_sinks_hot_result __real_gin_account_sinks_hot_bind(struct gin_account_sinks *s,const struct gin_account_registry *r,const struct gin_account_registry_scope *c,uint64_t g);
struct gin_account_sinks_hot_result __wrap_gin_account_sinks_hot_bind(struct gin_account_sinks *s,const struct gin_account_registry *r,const struct gin_account_registry_scope *c,uint64_t g) { trap("gin_account_sinks_hot_bind");return __real_gin_account_sinks_hot_bind(s,r,c,g); }
extern struct gin_account_sinks_hot_result __real_gin_account_sinks_hot_append(struct gin_account_sinks *s,const struct gin_account_registry_scope *c,uint64_t g,size_t i);
struct gin_account_sinks_hot_result __wrap_gin_account_sinks_hot_append(struct gin_account_sinks *s,const struct gin_account_registry_scope *c,uint64_t g,size_t i) { trap("gin_account_sinks_hot_append");return __real_gin_account_sinks_hot_append(s,c,g,i); }
extern struct gin_checkpoint_write_result __real_gin_checkpoint_commit(struct gin_checkpoint *c,int64_t a);
struct gin_checkpoint_write_result __wrap_gin_checkpoint_commit(struct gin_checkpoint *c,int64_t a) { trap("gin_checkpoint_commit");return __real_gin_checkpoint_commit(c,a); }
extern int __real_gin_checkpoint_close(struct gin_checkpoint **c);
int __wrap_gin_checkpoint_close(struct gin_checkpoint **c) { trap("gin_checkpoint_close");return __real_gin_checkpoint_close(c); }
extern int __real_open(const char *p,int f,...);
int __wrap_open(const char *p,int f,...) { mode_t m=0;va_list a;trap("open");if(f&O_CREAT) { va_start(a,f);m=(mode_t)va_arg(a,int);va_end(a);return __real_open(p,f,m); } return __real_open(p,f); }
extern int __real_openat(int d,const char *p,int f,...);
int __wrap_openat(int d,const char *p,int f,...) { mode_t m=0;va_list a;trap("openat");if(gin_status_fail_openat) { errno=EIO;return -1; }if(f&O_CREAT) { va_start(a,f);m=(mode_t)va_arg(a,int);va_end(a);return __real_openat(d,p,f,m); } return __real_openat(d,p,f); }
extern int __real_fcntl(int,int,...);
int __wrap_fcntl(int f,int c,...) { va_list a;trap("fcntl"); if(c==F_GETFD || c==F_GETFL) return __real_fcntl(f,c);va_start(a,c);if(c==F_SETLK || c==F_SETLKW || c==F_GETLK) { struct flock *p=va_arg(a,struct flock *);va_end(a);return __real_fcntl(f,c,p); } else { int v=va_arg(a,int);va_end(a);return __real_fcntl(f,c,v); } }
