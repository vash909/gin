#ifndef GIN_ACCOUNT_RECEIVE_H
#define GIN_ACCOUNT_RECEIVE_H
#include "account_plan.h"
#include "session_live_queue.h"
/* Private single-owner raw source admission, not routing/rendering or CLI.
 * Existing HTTP global lifecycle belongs to caller. No borrowed Native accepted:
 * new deep-copies settings/credential into its OWN Native, which owns/wipes bearer.
 * Plan supplies exact desired membership, not identity: credential subject_name
 * supplies topic labels; fresh bound /me supplies UUID authority at every start.
 * new requires fresh NULL; no I/O and failure leaves output unchanged. Plan,
 * settings and credential may be destroyed after success. No retained bearer copy
 * outside Native. Caller supplies receive_ms once per service, outside callbacks.
 * All decoded application frames are copied as UNSUPPORTED raw views; kind is
 * NOT semantic classification or permission to render. No metadata fanout.
 * Queue caps: 64 entries/2 MiB, including owned entry metadata, excluding fixed
 * queue storage and Native's own bounded buffers. Sticky overflow/OOM surfaces
 * immediately after service; do not service again after failure. No reconnect.
 * close clears all queued views; explicit start after close begins a fresh epoch.
 * Borrowed peek is invalidated by drop/close/start/free; never retain across those
 * calls. On transport failure queued views are discarded, not exposed as current.
 * No reentry, threads, callback clocks/I/O, implicit retry or replay.
 */
struct gin_account_receive;
/* Narrow owned-Native operation facade, outside callbacks. Any sticky raw
 * admission failure rejects submit as UNAVAILABLE. Result/take remain visible
 * through failure and close until free; non-success take leaves out untouched. */
enum gin_query_code gin_account_receive_open_query(struct gin_account_receive *,
 const struct gin_topology *,const char *,const char *,const char *,
 enum gin_query_mode,uint64_t,unsigned);
struct gin_query_result gin_account_receive_query_result(const struct gin_account_receive *);
int gin_account_receive_query_take(struct gin_account_receive *,struct gin_query_result *);
/* Borrowed identity only, not bearer or authentication proof before start. */
struct gin_account_identity { const char *origin, *subject_id, *subject_name; };
const struct gin_account_identity *gin_account_receive_identity(const struct gin_account_receive *);
enum gin_connect_state gin_account_receive_state(const struct gin_account_receive *);
uint64_t gin_account_receive_epoch(const struct gin_account_receive *);
int gin_account_receive_drained(const struct gin_account_receive *);
/* Private borrowed-lifetime control: caller must keep this facade/owned Native
 * alive, use only outside callbacks, without reentry, and supply the CURRENT
 * bounded HTTP wait. No service, clock, I/O, allocation, retry or queue drain.
 * NULL facade/wait returns INVALID; sticky admission failure returns its exact
 * code, without changing wait. Otherwise Native's code/budget are forwarded;
 * success with owned raw queued work forces zero (admitted deferred work), even
 * when public peek is hidden. Never enlarges the proposed wait. Native errors
 * preserve wait, including IDLE INVALID and STOPPED CLOSED. Zero requests owner
 * service/processing, not completion of a partial frame or permission to reenter.
 * No returned raw Native pointer. Existing borrowed views/lifetimes unchanged.
 * Query cleanup flags after close and BEFORE free; NULL getters return zero.
 * Flags transparently expose sticky Native state, without resetting anything.
 * If quarantined, owner MUST retain both facade and Native and must NOT free
 * them or tear down HTTP/curl global state underneath referenced storage.
 * Process restart handles that anomaly; this is caller ordering, not a changed
 * free API. Normal legacy close/free behavior is unchanged.
 */
enum gin_connect_code gin_account_receive_http_wait_budget(struct gin_account_receive *, unsigned *);
int gin_account_receive_cleanup_failed(const struct gin_account_receive *);
int gin_account_receive_quarantined(const struct gin_account_receive *);
const struct gin_connect_topic_status *gin_account_receive_source(const struct gin_account_receive *, size_t);
enum gin_connect_code gin_account_receive_new(const struct gin_connect_settings *,
    const struct gin_credential *, const struct gin_account_plan *,
    struct gin_account_receive **);
enum gin_connect_code gin_account_receive_start(struct gin_account_receive *);
enum gin_connect_code gin_account_receive_service(struct gin_account_receive *, unsigned, int64_t);
const struct gin_live_view *gin_account_receive_peek(const struct gin_account_receive *);
size_t gin_account_receive_count(const struct gin_account_receive *);
enum gin_live_code gin_account_receive_drop(struct gin_account_receive *);
void gin_account_receive_close(struct gin_account_receive *);
void gin_account_receive_free(struct gin_account_receive *);
#endif
