#include "checkpoint.h"
#include "fs_private.h"
#include "gin_naming.h"
#include "history_values.h"
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const unsigned char magic[8]={'G','I','N','C','P',0,0,1};
struct gin_checkpoint {
    const struct gin_root *root;
    int window, output, present, failed;
    struct stat expected, output_stat;
    int64_t anchor;
    int unknown_before;
    size_t key_size;
    unsigned char key[GIN_CHECKPOINT_BYTES_MAX];
};
struct bytes { unsigned char *p; size_t left; };
static int put(struct bytes *b, const void *p, size_t n)
{
    if (n>b->left) return 0;
    memcpy(b->p,p,n);b->p+=n;b->left-=n;return 1;
}
static int number(struct bytes *b, uint64_t value)
{
    unsigned char data[8];unsigned i;
    for (i=0;i<8;i++) { data[7-i]=(unsigned char)(value&255u);value>>=8; }
    return put(b,data,8);
}
static int string(struct bytes *b, const char *s)
{
    size_t n=strlen(s);
    return number(b,(uint64_t)n) && put(b,s,n);
}
static uint64_t read_number(const unsigned char *p)
{
    uint64_t n=0;unsigned i;
    for (i=0;i<8;i++) n=(n<<8)|p[i];
    return n;
}
static int same(const struct stat *a, const struct stat *b)
{ return a->st_dev==b->st_dev && a->st_ino==b->st_ino; }
static int private_file(const struct stat *s)
{
    return S_ISREG(s->st_mode) && s->st_uid==geteuid() &&
        (s->st_mode&07777)==0600 && s->st_nlink==1;
}
static enum gin_checkpoint_code fs_error(void)
{
    return errno==ELOOP || errno==ENOTDIR || errno==EPERM ?
        GIN_CHECKPOINT_UNSAFE : GIN_CHECKPOINT_IO;
}
static enum gin_checkpoint_code held_root(const struct gin_root *root, struct stat *lock)
{
    struct stat dir,path;
    if (fstat(root->rootfd,&dir)==-1 || fstat(root->lockfd,lock)==-1 ||
        fstatat(root->rootfd,".gin.lock",&path,AT_SYMLINK_NOFOLLOW)==-1)
        return fs_error();
    if (!S_ISDIR(dir.st_mode) || dir.st_uid!=geteuid() ||
        (dir.st_mode&07777)!=0700 || !private_file(lock) ||
        !private_file(&path) || !same(lock,&path)) return GIN_CHECKPOINT_UNSAFE;
    return GIN_CHECKPOINT_OK;
}
static enum gin_checkpoint_code output_guard(struct gin_checkpoint *h,
                                             const struct stat *lock, struct stat *out)
{
    struct stat path;int flags;
    if (fstat(h->output,out)==-1 ||
        fstatat(h->window,"out",&path,AT_SYMLINK_NOFOLLOW)==-1 ||
        (flags=fcntl(h->output,F_GETFL))==-1) return fs_error();
    if (!private_file(out) || !private_file(&path) || !same(out,&path) ||
        same(out,lock) || !(flags&O_APPEND) || (flags&O_ACCMODE)==O_RDONLY)
        return GIN_CHECKPOINT_UNSAFE;
    if (out->st_size<0) return GIN_CHECKPOINT_IO;
    return GIN_CHECKPOINT_OK;
}
static enum gin_checkpoint_code inspect(struct gin_checkpoint *h,
                                        const struct stat *lock, struct stat *st, int *present)
{
    if (fstatat(h->window,GIN_CHECKPOINT_FILENAME,st,AT_SYMLINK_NOFOLLOW)==-1) {
        if (errno!=ENOENT) return fs_error();
        *present=0;return GIN_CHECKPOINT_OK;
    }
    if (!private_file(st) || same(st,lock)) return GIN_CHECKPOINT_UNSAFE;
    *present=1;return GIN_CHECKPOINT_OK;
}
static enum gin_checkpoint_code unchanged(struct gin_checkpoint *h, const struct stat *lock)
{
    struct stat current;int present;enum gin_checkpoint_code code=inspect(h,lock,&current,&present);
    if (code!=GIN_CHECKPOINT_OK) return code;
    if (present!=h->present || (present && !same(&current,&h->expected)))
        return GIN_CHECKPOINT_UNSAFE;
    return GIN_CHECKPOINT_OK;
}
static int stat_numbers(const struct stat *st, uint64_t n[3])
{
    if (st->st_size<0) return 0;
    n[0]=(uint64_t)(uintmax_t)st->st_dev;n[1]=(uint64_t)(uintmax_t)st->st_ino;
    n[2]=(uint64_t)(uintmax_t)st->st_size;
    return (uintmax_t)n[0]==(uintmax_t)st->st_dev &&
        (uintmax_t)n[1]==(uintmax_t)st->st_ino &&
        (uintmax_t)n[2]==(uintmax_t)st->st_size;
}
static enum gin_checkpoint_code decode(struct gin_checkpoint *h,
                                         const unsigned char *data, size_t n)
{
    uint64_t anchor,unknown,actual[3],key_size;
    if (n<56 || memcmp(data,magic,8)) return GIN_CHECKPOINT_MALFORMED;
    anchor=read_number(data+8);unknown=read_number(data+16);key_size=read_number(data+48);
    if (anchor>INT64_MAX || unknown>1 || (!anchor && !unknown) || key_size!=n-56)
        return GIN_CHECKPOINT_MALFORMED;
    if (key_size!=h->key_size || memcmp(data+56,h->key,h->key_size))
        return GIN_CHECKPOINT_SCOPE;
    if (!stat_numbers(&h->output_stat,actual)) return GIN_CHECKPOINT_IO;
    if (read_number(data+24)!=actual[0] || read_number(data+32)!=actual[1] ||
        read_number(data+40)!=actual[2]) return GIN_CHECKPOINT_AMBIGUOUS;
    h->anchor=(int64_t)anchor;h->unknown_before=(int)unknown;
    return GIN_CHECKPOINT_OK;
}
static enum gin_checkpoint_code load(struct gin_checkpoint *h, const struct stat *lock)
{
    unsigned char data[GIN_CHECKPOINT_BYTES_MAX+1];
    struct stat opened,after,output_after;
    enum gin_checkpoint_code code;
    size_t used=0;unsigned attempts=0;int fd=-1;
    code=inspect(h,lock,&h->expected,&h->present);
    if (code!=GIN_CHECKPOINT_OK) return code;
    if (!h->present) { h->unknown_before=1;return GIN_CHECKPOINT_UNKNOWN; }
    if (h->expected.st_size<0 || (uintmax_t)h->expected.st_size>GIN_CHECKPOINT_BYTES_MAX)
        return GIN_CHECKPOINT_MALFORMED;
    fd=openat(h->window,GIN_CHECKPOINT_FILENAME,O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC);
    if (fd<0) return fs_error();
    if (fstat(fd,&opened)==-1 ||
        fstatat(h->window,GIN_CHECKPOINT_FILENAME,&after,AT_SYMLINK_NOFOLLOW)==-1) {
        code=fs_error();goto done;
    }
    if (!private_file(&opened) || !private_file(&after) || same(&opened,lock) ||
        !same(&h->expected,&opened) || !same(&opened,&after)) {
        code=GIN_CHECKPOINT_UNSAFE;goto done;
    }
    code=GIN_CHECKPOINT_IO;
    while (used<sizeof(data) && attempts++<GIN_CHECKPOINT_BYTES_MAX+128u) {
        ssize_t n=read(fd,data+used,sizeof(data)-used);
        if (n<0 && errno==EINTR) continue;
        if (n<0) goto done;
        if (!n) {
            if (fstat(fd,&after)==-1) goto done;
            if (!private_file(&after) || !same(&opened,&after) ||
                after.st_size!=opened.st_size || (uintmax_t)after.st_size!=used) {
                code=GIN_CHECKPOINT_UNSAFE;goto done;
            }
            code=unchanged(h,lock);
            if (code!=GIN_CHECKPOINT_OK) goto done;
            code=output_guard(h,lock,&output_after);
            if (code!=GIN_CHECKPOINT_OK) goto done;
            if (!same(&h->output_stat,&output_after) || h->output_stat.st_size!=output_after.st_size) {
                code=GIN_CHECKPOINT_AMBIGUOUS;goto done;
            }
            code=decode(h,data,used);goto done;
        }
        used+=(size_t)n;
    }
    if (used==sizeof(data)) code=GIN_CHECKPOINT_MALFORMED;
done:
    if (close(fd)==-1 && code==GIN_CHECKPOINT_OK) code=GIN_CHECKPOINT_IO;
    memset(data,0,sizeof(data));return code;
}
static enum gin_checkpoint_code checkpoint_open(const struct gin_root *root,
    const struct gin_history_scope *scope, int output,
    struct gin_checkpoint **out, struct gin_checkpoint_progress *progress,
    gin_fs_name_prepare_fn prepare, int network_storage)
{
    struct gin_checkpoint *h;struct stat lock;struct gin_name name;
    struct bytes key;enum gin_checkpoint_code code;
    if (!root || root->rootfd<0 || root->lockfd<0 || output<0 || !out || *out || !progress ||
        gin_history_scope_check(scope)!=GIN_HISTORY_OK ||
        (network_storage && (scope->role!=GIN_RENDER_STATUS || strcmp(scope->target,"$server"))) ||
        (prepare ? prepare(scope->network_slug,scope->target,scope->chantypes,&name) :
         gin_name_prepare(scope->network_slug,scope->target,scope->chantypes,&name))==-1)
        return GIN_CHECKPOINT_INVALID;
    h=calloc(1,sizeof(*h));if (!h) return GIN_CHECKPOINT_NOMEM;
    h->root=root;h->output=output;h->window=-1;
    key.p=h->key;key.left=sizeof(h->key);
    if (!number(&key,(uint64_t)scope->network_id) || !number(&key,(uint64_t)scope->role) ||
        !string(&key,scope->origin) || !string(&key,scope->subject_id) ||
        !string(&key,scope->network_slug) || !string(&key,name.canonical_target) ||
        !string(&key,scope->chantypes)) { code=GIN_CHECKPOINT_INVALID;goto fail; }
    h->key_size=sizeof(h->key)-key.left;
    if (h->key_size>GIN_CHECKPOINT_BYTES_MAX-56u) { code=GIN_CHECKPOINT_INVALID;goto fail; }
    code=held_root(root,&lock);if (code!=GIN_CHECKPOINT_OK) goto fail;
    h->window=network_storage ? gin_root_network_open(root,scope->network_slug) :
        prepare ? gin_root_window_open_named(root,scope->network_slug,scope->target,
        scope->chantypes,prepare) :
        gin_root_window_open(root,scope->network_slug,scope->target,scope->chantypes);
    if (h->window<0) { code=fs_error();goto fail; }
    code=output_guard(h,&lock,&h->output_stat);if (code!=GIN_CHECKPOINT_OK) goto fail;
    code=load(h,&lock);
    if (code!=GIN_CHECKPOINT_OK && code!=GIN_CHECKPOINT_UNKNOWN) goto fail;
    progress->anchor=h->anchor;progress->unknown_before=h->unknown_before;*out=h;
    return code;
fail:
    if (h->window>=0) (void)close(h->window);
    memset(h,0,sizeof(*h));free(h);return code;
}
enum gin_checkpoint_code gin_checkpoint_open(const struct gin_root *root,
    const struct gin_history_scope *scope, int output,
    struct gin_checkpoint **out, struct gin_checkpoint_progress *progress)
{
    return checkpoint_open(root,scope,output,out,progress,NULL,0);
}
enum gin_checkpoint_code gin_checkpoint_open_named(const struct gin_root *root,
    const struct gin_history_scope *scope, int output,
    struct gin_checkpoint **out, struct gin_checkpoint_progress *progress,
    gin_fs_name_prepare_fn prepare)
{
    if (!prepare) return GIN_CHECKPOINT_INVALID;
    return checkpoint_open(root,scope,output,out,progress,prepare,0);
}
enum gin_checkpoint_code gin_checkpoint_open_network(const struct gin_root *root,
    const struct gin_history_scope *scope, int output,
    struct gin_checkpoint **out, struct gin_checkpoint_progress *progress)
{
    return checkpoint_open(root,scope,output,out,progress,NULL,1);
}
static int encode(struct gin_checkpoint *h, int64_t anchor, const struct stat *output,
                   unsigned char *data, size_t *size)
{
    struct bytes b={data,GIN_CHECKPOINT_BYTES_MAX};uint64_t st[3];
    if (!stat_numbers(output,st) || !put(&b,magic,8) || !number(&b,(uint64_t)anchor) ||
        !number(&b,(uint64_t)h->unknown_before) || !number(&b,st[0]) || !number(&b,st[1]) ||
        !number(&b,st[2]) || !number(&b,(uint64_t)h->key_size) || !put(&b,h->key,h->key_size)) return 0;
    *size=GIN_CHECKPOINT_BYTES_MAX-b.left;return 1;
}
static int clean_owned(int dir, const char *name, const struct stat *created, int identified)
{
    struct stat current;
    if (!identified || fstatat(dir,name,&current,AT_SYMLINK_NOFOLLOW)==-1 || !same(created,&current))
        return -1;
    return unlinkat(dir,name,0);
}
struct gin_checkpoint_write_result gin_checkpoint_commit(struct gin_checkpoint *h, int64_t anchor)
{
    static unsigned long sequence;
    struct gin_checkpoint_write_result result={GIN_CHECKPOINT_INVALID,0,0};
    unsigned char data[GIN_CHECKPOINT_BYTES_MAX];
    struct stat lock,current,after,created;
    char temp[96];size_t size=0,used=0;unsigned attempts=0,i;
    int fd=-1,own=0,identified=0;
    if (!h || h->failed || anchor<0 || anchor<h->anchor || (!anchor && !h->unknown_before)) return result;
    result.code=held_root(h->root,&lock);if (result.code!=GIN_CHECKPOINT_OK) goto done;
    result.code=output_guard(h,&lock,&current);if (result.code!=GIN_CHECKPOINT_OK) goto done;
    if (!same(&h->output_stat,&current) || current.st_size<h->output_stat.st_size) {
        result.code=GIN_CHECKPOINT_AMBIGUOUS;goto done;
    }
    if (current.st_size==h->output_stat.st_size) { result.code=GIN_CHECKPOINT_INVALID;goto done; }
    result.code=unchanged(h,&lock);if (result.code!=GIN_CHECKPOINT_OK) goto done;
    result.code=GIN_CHECKPOINT_IO;
    if (!encode(h,anchor,&current,data,&size)) goto done;
    for (i=0;i<64;i++) {
        int n=snprintf(temp,sizeof(temp),".gin.checkpoint.tmp.%ld.%lu",(long)getpid(),++sequence);
        if (n<0 || (size_t)n>=sizeof(temp)) goto done;
        fd=openat(h->window,temp,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC,0600);
        if (fd>=0) { own=1;break; }
        if (errno!=EEXIST) goto done;
    }
    if (fd<0 || fstat(fd,&created)==-1) goto done;
    identified=1;
    if (!private_file(&created) || same(&created,&lock)) { result.code=GIN_CHECKPOINT_UNSAFE;goto done; }
    while (used<size && attempts++<GIN_CHECKPOINT_BYTES_MAX+128u) {
        ssize_t n=write(fd,data+used,size-used);
        if (n<0 && errno==EINTR) continue;
        if (n<=0) goto done;
        used+=(size_t)n;
    }
    if (used!=size || fsync(fd)==-1 || fstat(fd,&after)==-1) goto done;
    if (!private_file(&after) || !same(&created,&after) || (uintmax_t)after.st_size!=size) {
        result.code=GIN_CHECKPOINT_UNSAFE;goto done;
    }
    if (fstatat(h->window,temp,&after,AT_SYMLINK_NOFOLLOW)==-1) goto done;
    if (!private_file(&after) || !same(&created,&after)) { result.code=GIN_CHECKPOINT_UNSAFE;goto done; }
    if (close(fd)==-1) { fd=-1;goto done; }fd=-1;
    result.code=held_root(h->root,&lock);if (result.code!=GIN_CHECKPOINT_OK) goto done;
    result.code=unchanged(h,&lock);if (result.code!=GIN_CHECKPOINT_OK) goto done;
    result.code=output_guard(h,&lock,&after);if (result.code!=GIN_CHECKPOINT_OK) goto done;
    if (!same(&current,&after) || current.st_size!=after.st_size) {
        result.code=GIN_CHECKPOINT_AMBIGUOUS;goto done;
    }
    result.code=GIN_CHECKPOINT_IO;
    if (renameat(h->window,temp,h->window,GIN_CHECKPOINT_FILENAME)==-1) goto done;
    own=0;result.published=1;h->expected=created;h->present=1;h->output_stat=current;h->anchor=anchor;
    if (fsync(h->window)==-1) goto done;
    result.code=GIN_CHECKPOINT_OK;
done:
    if (fd>=0 && close(fd)==-1 && result.code==GIN_CHECKPOINT_OK) result.code=GIN_CHECKPOINT_IO;
    if (own && clean_owned(h->window,temp,&created,identified)==-1) result.cleanup_failed=1;
    if (result.code!=GIN_CHECKPOINT_OK && result.code!=GIN_CHECKPOINT_INVALID) h->failed=1;
    memset(data,0,sizeof(data));return result;
}
int gin_checkpoint_close(struct gin_checkpoint **value)
{
    struct gin_checkpoint *h;int result;
    if (!value || !*value) { errno=EINVAL;return -1; }
    h=*value;*value=NULL;result=close(h->window);memset(h,0,sizeof(*h));free(h);return result;
}
