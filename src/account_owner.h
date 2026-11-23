#ifndef GIN_ACCOUNT_OWNER_H
#define GIN_ACCOUNT_OWNER_H
#include "account_history_bootstrap.h"
#include "gin_send.h"
#include "gin_control.h"
/* Private single synchronous owner; no CLI, discovery, login, reconnect or FIFO.
 * All input handles/strings are borrowed until return, including a held root.
 * Caller initializes HTTP and MUST retain global state if quarantined is set.
 * This owner retains a quarantined receiver/Native allocation until restart.
 * stop and phase run outside curl callbacks; no reentry or borrowed mutation.
 * phase 1 is actual Native READY/drained; phase 2 is completed initial history,
 * not an account/CLI READY guarantee. Startup has one aggregate 60s episode.
 * Steady service is bounded per call and runs until stop/terminal/error.
 * History-return handoff dedup is NOT qualified. Anchors never infer live IDs.
 */
enum gin_account_owner_code {
 GIN_ACCOUNT_OWNER_OK, GIN_ACCOUNT_OWNER_INVALID, GIN_ACCOUNT_OWNER_CLOCK,
 GIN_ACCOUNT_OWNER_DEADLINE, GIN_ACCOUNT_OWNER_RECEIVE,
 GIN_ACCOUNT_OWNER_DISPATCH, GIN_ACCOUNT_OWNER_SINKS,
 GIN_ACCOUNT_OWNER_OUTPUT, GIN_ACCOUNT_OWNER_HISTORY, GIN_ACCOUNT_OWNER_CLEANUP
};
struct gin_account_owner_input {
 const struct gin_root *root;
 const struct gin_account_plan *plan;
 const struct gin_topology *topology;
 const struct gin_connect_settings *settings;
 const struct gin_credential *credential;
 const char *chantypes;
 enum gin_render_encryption encryption;
 struct gin_http_control *stop;
 void (*phase)(void *, unsigned);
 void *phase_context;
};
struct gin_account_owner_result {
 enum gin_account_owner_code code;
 enum gin_connect_code receive;
 enum gin_session_code dispatch;
 struct gin_account_sinks_result sinks;
 struct gin_account_output_result output;
 struct gin_account_history_bootstrap_result history;
 size_t appended;
 int stopped, terminal, cleanup_failed, quarantined;
};
struct gin_account_owner_result gin_account_owner_run(const struct gin_account_owner_input *);
/* Explicit PLAIN FIFO opt-in for frozen-plan channels/already-open queries,
 * and closed /join or /part commands on the exact frozen-plan network FIFO only.
 * Legacy run remains receive-only. Separate last-kind SEND/control receipts
 * are retained before stop handling, sharing nonzero monotonic operation IDs.
 * JOIN/PART borrow a plain root copy only for synchronous descriptor validation;
 * it never closes the copy or transfers ownership. Caller retains the original
 * held root through return. Acceptance is not membership or a local window.
 * PART acceptance does not stop the account or remove local windows/logs/FIFOs.
 * QUERY uses the same live root, explicit PLAIN, one Native operation slot and
 * a 5000ms deadline. Pending QUERY blocks all FIFO parsing, not WSS service.
 * No automatic action, retry, replay, echo or new local window projection. */
struct gin_account_fifo_result {
 struct gin_account_owner_result owner;
 struct gin_send_result terminal;
 uint64_t operations;
 int has_terminal;
 struct gin_control_result control_terminal;
 int has_control_terminal;
 enum gin_query_code query_submission_code;
 uint64_t query_submission_id;
 int has_query_submission;
 struct gin_query_result query_terminal;
 int has_query_terminal;
};
struct gin_account_fifo_result gin_account_owner_run_fifo(const struct gin_account_owner_input *);
#endif
