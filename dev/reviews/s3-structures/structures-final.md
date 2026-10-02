# S3 data structures: final design (Sage, 2026-10-01)

Keys: R = RFC `Zend/zend_async_API.h` (834811f2d88), Ra = its `.c`, Rf = RFC `zend_fibers.c`,
ts = `ext/test_scheduler/test_scheduler.c`; F = fork `zend_async_API.h`, Fc = fork `zend_async_API.c`;
bare names = php-async `1fdacf8`; D = `EDMOND-DECISIONS.md` item (D3 in its REVISED form); C0/CC/CO =
Critic round 0 / on consolidation / on optimization; E1/E2 = the two experts. Every line number
below was re-read in the sources today; "reading" = verified by reading, never run.

## 1. Verdicts

### Critic round 0 (critic0.md)

| # | Ruling | Reason / fix adopted |
|---|---|---|
| 1 | **Stands in substance; fix differs** (D3 revised) | One predicate cannot mean both "has run" and "handed to the scheduler". D3: `ZEND_COROUTINE_F_STARTED` (bit 8) = "the body began executing", `IS_STARTED` reads it. Scenario A re-checked: Rf:770 becomes `ZEND_ASYNC_ON && !IS_FINISHED` → F (QUEUED, never run) is cancelled; ts:1451 drops its `!IS_STARTED` term → `ts_enqueue` stores the error (ts:1389-1399) before the `IS_QUEUED` early return (ts:1409) → first entry with an error skips the body (ts:992; RFC.md:169-171), `zend_fiber_coroutine_entry` never runs with `fiber == NULL`. Rf:1038-1039 releases without cancelling (the only CREATED fiber coroutine). Scenario B: the same ts:1451 change revives ts:992. ts:1069/1083 (bailout) and ts:1166 (drain) keep their text and get the meaning they wanted: a never-run coroutine has no stack to unwind (its context is freed by dispose, ts:692); ts:1774 is `isStarted`. No `IS_SCHEDULED`. E1 section 5/10.1 (`IS_SCHEDULED`) is superseded |
| 2 | **Stands, in E1's narrowed form** | Coroutine bits 17 `EXCEPTION_HANDLED`, 18 `EXC_CAUGHT`, 19 `BAILOUT` (section 2.3). No bit for `RESULT_USED` (only reader `future.c:586` via `ZEND_FUTURE_IS_USED`, F:1941; grep today) nor `ZVAL_RESULT` (constant true for a coroutine, `coroutine.c:666` sets it before the only notify). Typed APIs (section 2.6) make an event macro on a coroutine a compile error |
| 3 | **Owner's side (D4)**: polarity A, 0 = coroutine | The Critic's mitigations are adopted: every event constructor ORs bit 31 (`async_event_init`), the reference prefix carries it, debug asserts at every generic entry (section 2.1) |
| 4 | **Stands** (D3) | `F_STARTED` set immediately before the body's first instruction by whoever runs it: switch-in, in-place run (`scheduler.c:633-641`), main (`scheduler.c:1246`, also every re-minted main), never on the cancelled-before-run path (`coroutine.c:524` after `:466-499`) |
| 5 | **Stands** (D6) | `isSuspended = S ∈ {QUEUED, SUSPENDED}`; no `F_YIELD`; fiber exemption reads `((zend_fiber *) extended_data)->context.status == ZEND_FIBER_STATUS_SUSPENDED` (Rf:899, 920) under `IS_FIBER && extended_data != NULL` |
| 6 | **Stands** (D7) | `was_protected` local (section 2.8); reference bug, section 7 |
| 7 | **Stands, in the reference's form, not E1's** | See CC 3: pending = `F_CANCELLED && waker.error is a cancellation`; `deferred_cancellation` is a separate store; after delivery `waker.error == NULL` so a new cancel is delivered again (`coroutine.c:994-1003`, `scheduler.c:1735-1740`) |
| 8 | **Stands** (D5) | Rf:1375 keeps only `extended_data == NULL`. Behaviour change named by the Critic accepted: a scheduler-cancelled fiber may call `Fiber::suspend()` from `finally` |

### Critic on consolidation.md

| # | Ruling | Reason / fix adopted |
|---|---|---|
| 1 | **Stands as to D4 (polarity); D3 part overtaken** | E1's bit-8 proposal is now right by D3-revised, `IS_SCHEDULED` is not. The extension has no STARTED bit; ported `ZEND_COROUTINE_IS/SET_STARTED` keep their meaning, so the STARTED rename and its grep gate are **dropped**. Main: the core sets MAIN+RUNNING (Ra:737-738), the extension's `launch` sets `F_STARTED` (reference `scheduler.c:1246`), and so does every re-minted main |
| 2 | **Stands** | Verified: the core parks with zero edges (Rf:911 `Fiber::suspend`, Rf:965 `zend_fiber_await`, `zend_gc.c:2163`) and the reference returns at once on an empty waker (`scheduler.c:1664-1668`). Slot contracts in section 2.9: suspend always parks; enqueue dispatches on the status |
| 3 | **Stands** | Verified `coroutine.c:936-951` (protected: store only if NULL, return before `SET_CANCELLED` :954), `:994-1001` (drop iff `was_cancelled && waker.error` is a cancellation), `scheduler.c:867-868, 924-925` (deadlock/shutdown clear PROTECTED then cancel). E1's "drop while `deferred_cancellation` holds" would drop the deadlock cancellation of a protected pair. Rule = the reference's, section 2.8 |
| 4 | **Stands** | Verified `scheduler.c:1464-1469`. First enqueue = `STATUS == CREATED` read before `SET_STATUS(QUEUED)`; the `ASYNC_G(coroutines)` lookup goes |
| 5 | **Stands** | Verified `scheduler.c:730-733` (`trigger->event->info`). Every edge target goes through `async_awaitable_info()` (type dispatch → `async_coroutine_info` or `methods->info`); `get_awaiting_info` stays `[]` in S3 |
| 6 | **Stands, with one choice** | Handle = `uint32_t` id from one per-thread counter `ASYNC_G(handler_id_seq)` (starts at 1, skips 0); a finish handler is an `async_finish_handler_callback_t` (56 B, one emalloc per add; the only core adder is `zend_gc.c:2134`, one per GC run) in the target's `callbacks`; remove = scan by id. Add/remove during notify are **allowed**: the corrected cursor (CO 1) makes removal safe and the loop re-reads `data` each step so a realloc on push is safe. E2's option A (RFC `void *` handles) rejected: no core caller needs it (P1.1: fewer core changes) |
| 7 | **Stands** | Verified `scheduler.c:966-968` (queued coroutine switched with the BAILOUT transfer flag, no `SET_BAILOUT`) and `coroutine.c:547-552`. Fix: the catch at `:547` sets bit 19 before finalize, and `is_bailout = bit 19 || ASYNC_G(bailing_out)` (set in the execute catch and in bailout_all). Reference bug, section 7 |
| 8 | **Stands** | Verified Ra:186-189 (bare `zend_hash_destroy`), `coroutine.c:212-214, 676-678, 280-286`. Finalize: `zend_hash_clean(&internal_context)`; `free_obj`: `zend_async_internal_context_destroy` once |
| 9 | **Stands** | Formulas give `isSuspended = false` and `getSuspendLocation = "unknown"` for the current coroutine (reference: true / last location); no test pins it; needs a `DECISIONS.md` line (open question 5). Factual corrections accepted: `protect/003` nests without a cancel (read today); `DECISIONS.md` has no "16 STARTED, 17 YIELD" entry (read today) |

### Critic on optimization.md

| # | Ruling | Reason / fix adopted |
|---|---|---|
| 1 | **Stands** | Verified Fc:1654-1676 (self-removal correct, removal before the cursor wrong) and `coroutine.c:668, 864-866, 1026-1028`. Tombstones dropped. Rule (section 2.4): swap removal with a corrected cursor, O(1), reference wake order kept. The cursor lives **outside** the vector in a notify frame chain (a thread-local single pair is unsound under a nested notify of a different vector; section 2.4 explains) |
| 2 | **E2's side stands; no bits** | `RESULT`/`NO_STATUS`: suspend sets SUSPENDED before starting the edges, a synchronous wake sets RUNNING back, suspend sees RUNNING and returns; `coroutine.c:805` becomes "refuse enqueue of a RUNNING non-current coroutine"; `:846` `was_waiting = (S == SUSPENDED)`; `:84` clean unconditionally (the flat waker makes it two slot tests). `IGNORED` without a cancel (`async_API.c:172, 181, 199`, `scheduler.c:1486, 1495`): these are spawn failures after the registry insert; in the reference they skip finalize and leave the registry pointer dangling (section 7, bug 4), so the rule is **cancel-before-run** (`F_CANCELLED` + the failure exception as the error, body skipped, finalize runs). S3 has no SpawnStrategy, so only the registry-add failure remains |
| 3 | **Stands** | (a) start/stop once **per edge**, never a scan; a "started" bit in the callback's `flags` word says whether stop is owed; reactor events count (`libuv_reactor.c:351-375`, per CO; not re-read). (b) each edge owns one reference to its target (`trans_event` only for the edge it came with); event-side dispose sets `edge->event = NULL` and releases. `await_all([$f, $f])` = two edges, two references. The S5 index stays deferred to the N-benchmark (section 5) |
| 4 | **Stands** | Verified: `fiber_context->execute_data` is written only at the switch (`scheduler.c:290-296`, callers :400, 411, 519, 537, 625); `scheduler_next_tick` runs microtasks (:1540) and `resolve_deadlocks` (:1587) before it; the reference captures the location at :1713-1716 before the tick. Fix: one store `fiber_context->execute_data = EG(current_execute_data)` at suspend entry; read only when `(QUEUED || SUSPENDED) && fiber_context` |
| 5 | **Stands** | `await` takes any `Completable` (`async.c:302`), the reference has no `interface_gets_implemented` (grep today): a user class lands on `handlers->offset == 0`. Fix: `Awaitable` gets `interface_gets_implemented` rejecting classes the extension did not register; `async_awaitable_from_object` also asserts `handlers->offset != 0`. Reference bug, section 7 |
| 6 | **Stands** | Two helpers behind the bit-31 branch: `ZEND_COROUTINE_OBJECT` (R:166-171) for a coroutine, `object_offset`/`ZEND_OBJ` for an event |
| 7 | **Stands** | Recomputed with `scope` (D11) and `switch_handlers`: 376 B struct / 360 B allocation / **384 bin** (section 2.2). O6 (inline `zend_fcall_t`): 480 / 464 / 512 bin, not 448 |

### Where the experts conflict

| Topic | Ruling |
|---|---|
| Bit numbering | 16 PROTECTED, 17 EXCEPTION_HANDLED, 18 EXC_CAUGHT, 19 BAILOUT, 20 reserved ZOMBIE (S9), 21-30 spare. E2's 21 STARTED is gone (D3: core bit 8); E2's 22 `EVENTS_STOPPED` is gone (per-edge stop, CO 3a) |
| Waker fields | E2's layout (120 B): `error`, `result`, `inline_callbacks[2]`, overflow `callbacks`. Cut: `status` (folded), `events` hash, `triggered_events`, `dtor`, `inline_triggers`, `filename`/`lineno` (lazy, CO 4 fix), `events_stopped`. E1's "keep `status` with a mapping" loses: E1 itself deferred the size question |
| Callbacks vector | 16 B union/single (E2) + swap removal with the corrected cursor in a notify frame chain (CO 1); no tombstones, no `current_iterator` field |
| Handles | uint32 id from a per-thread counter + trampoline callback (CC 6); no RFC change |
| `switch_handlers` | **Kept** (E1), lazy pointer: the core registers per-coroutine switch handlers on the current coroutine (`zend_objects_API.c:127`, `zend_execute_API.c:292`), which S3.4 needs. E2's layout omitted it (CO 7) |
| Scope | `async_scope_t *scope` (fork `zend_async_scope_t *scope`, F:1767) in `async_coroutine_t` now (D11), hook point `scope->after_coroutine_enqueue` at the first enqueue stays a no-op call site in S3 |
| Names | D12: `async_event_t`, `async_waker_t`, `async_callbacks_vector_t`, `async_event_callback_t`, `async_coroutine_event_callback_t`, `async_coroutine_switch_handlers_vector_t`. The waiters vector is `callbacks` (both experts, the reference's name) |
| `isRunning` | E1's reference formula `F_STARTED && !FINISHED` (P2.2); C0's `S == RUNNING` not taken, no test separates them |
| Methods table per type (E2 1.3) | Adopted for events; the coroutine never goes through a table (direct calls by type dispatch). Measured in S4 (section 5); names in open question 2 |

## 2. The final design

### 2.1 The type bit and the awaitable header

```c
typedef struct _async_awaitable_s { uint32_t flags; } async_awaitable_t;   /* never instantiated */
#define ASYNC_AWAITABLE_F_EVENT            (1u << 31)                        /* 0 coroutine, 1 event */
#define ASYNC_AWAITABLE_IS_COROUTINE(p)    ((((const async_awaitable_t *) (p))->flags & ASYNC_AWAITABLE_F_EVENT) == 0)
#define ASYNC_EVENT_REFERENCE_PREFIX       (ASYNC_AWAITABLE_F_EVENT | ASYNC_EVENT_F_REFERENCE)   /* 0x80000080 */
```

Rules: bit 31 is written only by `async_event_init()` (every event constructor, ported `memset` +
`flags =` sites included: `channel.c:841-843`, `task_group.c:397-399`, `libuv_reactor.c:3478`
when their stages come) and by the reference-prefix constant; the coroutine allocator writes
nothing (zeroed memory = coroutine, D4). Debug build: `async_awaitable_coroutine()` asserts
`ZEND_COROUTINE_OBJECT(c)->ce == async_ce_coroutine`; `async_awaitable_event()` asserts
`methods != NULL` and, for an object-backed event, a class other than `Coroutine`;
`async_awaitable_from_object()` asserts `obj->handlers->offset != 0`. The core never writes
bits 16-31 of a coroutine (Ra:737-738 `|=`, R:139-141 masked `SET_STATUS`, Rf:1011 `|=`).

### 2.2 `async_coroutine_t` (compiled with gcc 13 against the RFC headers, x86-64; `S/../sage/probe.c`)

```c
struct _async_coroutine_s {
	zend_coroutine_t coroutine;                               /*   0 144  R:104-132; flags at 0, bit 31 = 0 */
	async_fiber_context_t *fiber_context;                     /* 144   8  hot on every switch; execute_data stored at suspend entry */
	async_callbacks_vector_t callbacks;                       /* 152  16  waiters + finish handlers (fork event.callbacks) */
	async_waker_t waker;                                      /* 168 120  section 2.4 */
	async_scope_t *scope;                                     /* 288   8  D11; logic in S9 */
	zend_object *deferred_cancellation;                       /* 296   8  the cancel that arrived inside protect() */
	HashTable *finally_handlers;                              /* 304   8  lazy, as the reference */
	async_coroutine_switch_handlers_vector_t *switch_handlers;/* 312   8  lazy; fork F:754-759 with (handler, id) entries */
	zend_object std;                                          /* 320  56  last: properties_table tail */
};                                                            /* 376 B; allocation 360 B (sizeof − 16, final class without
                                                                 properties, zend_objects_API.h:82-96); bin 384 (zend_alloc_sizes.h:49) */
```

Measured reference (gdb, brief): 544 B, allocation 528, bin 640. Headroom in the 384 bin: 24 B.
Option O6 (inline `zend_fcall_t`, 104 B): 480 / 464 / bin 512, one allocation fewer per spawn;
decided by B1 (section 5), not part of the base layout. Cache lines (computed): a switch plus a
wake touch line 0 (`flags`, `fcall`) and line 2 (`fiber_context`, `callbacks`, `waker.error`,
`waker.result`); `std.refcount` at 320 (line 5).

Dropped from the fork/reference coroutine, with the reason: the embedded event (agreed); `waker`
pointer (embedded; NULL tests become `IS_FINISHED`); fork `internal_context` pointer (R:130 embeds
56 B); `extra_offset`, `ref_count`, `loop_ref_count`, `notify_handler`, the 8 vtable slots (the
coroutine is reached by type dispatch, section 2.6); `waker.status` (folded into the RFC status),
`waker.events` (hash → flat edges), `triggered_events` and `dtor` (no writer: Fc:1082 has no
caller; `dtor` only reset, Fc:892, 917, 957, `coroutine.c:101`), `inline_triggers` (the edge
carries its target), `waker.filename/lineno` (lazy from the parked frame), `events_stopped`
(per-edge stop bit), `F_YIELD`, `F_ZOMBIE` (S9), fork EH fields (D1: core).

### 2.3 The coroutine's flags word (every bit)

| Bits | Owner | Name | Meaning |
|---|---|---|---|
| 0-3 | core | status | CREATED / QUEUED / RUNNING / SUSPENDED / FINISHED (R:96-102); the scheduler is the only writer |
| 4 | core | `ZEND_COROUTINE_F_CANCELLED` | cancellation requested (R:144, RFC.md:138); never set while PROTECTED |
| 5 | core | `ZEND_COROUTINE_F_MAIN` | set by the core (Ra:737) |
| 6 | core | `ZEND_COROUTINE_F_FIBER` | set by the core (Rf:1011) |
| 7 | core | `ZEND_COROUTINE_F_OBJ_REF` | never set by True Async (embedded object) |
| 8 | core (S3.2) | `ZEND_COROUTINE_F_STARTED` | the body began executing (D3); set by the scheduler right before the first instruction; never on cancelled-before-run |
| 9-15 | core | spare | |
| 16 | extension | `ASYNC_COROUTINE_F_PROTECTED` | fork `F_PROTECTED` (F:1803) |
| 17 | extension | `ASYNC_COROUTINE_F_EXCEPTION_HANDLED` | per-notify: cleared before notify, set by a waiter callback, read after notify and after `extended_dispose` (`coroutine.c:667-705`); "handled" = bit 17 or `->exception` cleared by a core finish handler (R:84) |
| 18 | extension | `ASYNC_COROUTINE_F_EXC_CAUGHT` | somebody observed the exception; read by the destructor's rethrow decision (`coroutine.c:224`) |
| 19 | extension | `ASYNC_COROUTINE_F_BAILOUT` | finalize under bailout: finally handlers destroyed, not run (`coroutine.c:1335`) |
| 20 | extension | reserved `ASYNC_COROUTINE_F_ZOMBIE` | S9, not defined in S3 |
| 21-30 | extension | spare | |
| 31 | type bit | 0 | coroutine |

No `STARTED`, `YIELD`, `CANCEL_REQUESTED`, `IGNORED`, `RESULT`/woken, `RESULT_USED`, `ZVAL_RESULT`
bits in the extension (D3, D5, D6, CO 2, C0 2).

### 2.4 Waker, edges, the callbacks vector (compiled)

```c
struct _async_event_callback_s {                 /* fork F:830-834, 24 B */
	uint32_t ref_count;                          /*  0 4 */
	uint32_t flags;                              /*  4 4  was padding: ASYNC_CALLBACK_F_STARTED (stop owed), F_EDGE (S7 walk) */
	async_event_callback_fn callback;            /*  8 8  (async_awaitable_t *, callback, void *result, zend_object *exception) */
	async_event_callback_dispose_fn dispose;     /* 16 8 */
};
typedef struct {                                 /* fork zend_coroutine_event_callback_t F:862-868, 40 B: one wait-graph edge */
	async_event_callback_t base;                 /*  0 24 */
	async_coroutine_t *coroutine;                /* 24  8  edge source (the waiter) */
	async_awaitable_t *event;                    /* 32  8  edge target; one owned reference per edge */
} async_coroutine_event_callback_t;
typedef struct {                                 /* an RFC finish handler as a callback, 56 B (bin 56) */
	async_event_callback_t base;                 /*  0 24 */
	zend_coroutine_finish_handler_fn handler;    /* 24  8 */
	zend_coroutine_t *waiter;                    /* 32  8 */
	void *data;                                  /* 40  8 */
	uint32_t handler_id;                         /* 48  4  (+4 pad) from ASYNC_G(handler_id_seq), never 0 */
} async_finish_handler_callback_t;

typedef struct {                                 /* fork F:883-890 (24 B) → 16 B */
	union {
		async_event_callback_t **data;           /* capacity > 0: heap array (first growth: 4 slots, 32 B) */
		async_event_callback_t *single;          /* capacity == 0: at most one element inline */
	};                                           /* 0 8 */
	uint32_t length;                             /* 8 4 */
	uint32_t capacity;                           /* 12 4  bit 31 = a notify is iterating this vector */
} async_callbacks_vector_t;

#define ASYNC_WAKER_INLINE_SLOTS 2
typedef struct {                                 /* fork zend_async_waker_t F:1706-1731 (248 B) → 120 B */
	zend_object *error;                          /*   0  8  the RFC delivery channel (R:248-254) */
	zval result;                                 /*   8 16  moved out by await (ZVAL_COPY_VALUE + UNDEF), not copied */
	async_coroutine_event_callback_t inline_callbacks[2]; /* 24 80  free: base.callback == NULL */
	async_callbacks_vector_t callbacks;          /* 104 16  edges beyond two (S5) */
} async_waker_t;
```

**Removal and cursor rule.** Outside a notify: swap with the last, `length--`. During a notify of
this vector (bit 31 of `capacity` set) the cursor `cur` (index of the next callback to run) is
found in the notify frame chain and corrected: removing `i < cur`: `data[i] = data[cur-1];
data[cur-1] = data[--length]; cur--`; removing `i >= cur`: swap with the last. Self-removal is the
`i == cur-1` case. O(1), no tombstones, no compaction, the fork's wake order kept (A, C, B for a
self-removing A). The frame chain: `async_callbacks_notify()` pushes
`{vector, cur, prev}` on its own C stack with the head in `ASYNC_G(notify_frames)`; a push whose
vector is already in the chain is the fork's nested-notify refusal (Fc:1709-1714). A removal walks
the chain only when bit 31 is set (depth ≤ 2 in practice). Why not a thread-local pair: a nested
notify of a second vector (T's notify → callback resolves future F → F's notify → callback resumes
W → W cleans its edge on T) would adjust the wrong cursor. Why not the fork's pointer: 8 B per
vector, and its adjustment is wrong (section 7, bug 3). The notify holds a reference to the
awaitable for its duration (Fc:1689, 1739); adds during notify are allowed (the loop re-reads
`data` and `length` each step).

**Edges.** Every wait registers one `async_coroutine_event_callback_t` (inline slot first, then
the overflow vector) with `coroutine` = waiter, `event` = target, and one owned reference on the
target; `start`/`stop` are called once per edge (coroutine target: skipped by the type bit); the
callback's `F_STARTED` bit says whether `stop` is owed. Clean (at wake, at finish, at destroy):
for each used slot and overflow entry: stop if owed, remove from the target's `callbacks`
(direct for a coroutine), release the target, free the slot. Event-side dispose of a callback
sets `edge->event = NULL` before releasing. `await_all([$f, $f])` is two edges. The S7 collector and
the deadlock report walk the used slots and the overflow vector, reading `->event`.

### 2.5 `async_event_t` (base of Future, timers, IO; compiled, 32 B)

```c
typedef struct _async_event_methods_s {          /* one const table per event type, 64 B, .rodata */
	bool (*add_callback)(async_event_t *, async_event_callback_t *);
	bool (*del_callback)(async_event_t *, async_event_callback_t *);
	bool (*start)(async_event_t *);
	bool (*stop)(async_event_t *);
	bool (*replay)(async_event_t *, async_event_callback_t *, zval *result, zend_object **exception); /* NULL: not replayable */
	bool (*dispose)(async_event_t *);
	zend_string *(*info)(async_event_t *);
	void (*notify_handler)(async_event_t *, void *result, zend_object *exception);                /* NULL: plain notify */
} async_event_methods_t;
struct _async_event_s {
	uint32_t flags;                              /*  0 4  bit 31 = 1 */
	union { uint32_t ref_count; uint32_t object_offset; };  /* 4 4  ASYNC_EVENT_F_ZEND_OBJ selects */
	const async_event_methods_t *methods;        /*  8 8 */
	async_callbacks_vector_t callbacks;          /* 16 16 */
};                                               /* 32 B (fork 104 B, measured) */
```

Event flags 0-30 (fork positions kept, prefix `ASYNC_EVENT_F_`): 0 `CLOSED`, 1 `RESULT_USED`,
2 `EXC_CAUGHT`, 3 `ZVAL_RESULT`, 4 `ZEND_OBJ`, 5 free (`NO_FREE_MEMORY` cut: no reader), 6
`EXCEPTION_HANDLED`, 7 `REFERENCE` (only inside the prefix), 8 free (`OBJ_REF` cut with
`extra_offset`: no site in the reference sets or reads `OBJ_REF` (grep today; E2's `async.c:1666`
does not exist), the subtype keeps its own object pointer),
9 `CLOSE_FD` (poll subtype only), 10 `HIDDEN`, 11 `BAILOUT`, 12 spare, 13-30 subtype bits (fork
convention F:1131-1135), 31 type bit = 1. A decorator (timeout over a timer, `async.c:1673-1676`)
is its own const table whose `dispose` calls the decorated table's entry.

### 2.6 Typed APIs and dispatch

Event helpers take `async_event_t *`, coroutine waiter helpers `async_coroutine_t *`, generic
wait code `async_awaitable_t *`; all `static zend_always_inline` functions. Build with
`-Werror=incompatible-pointer-types` (config.m4) and `/we4133` (config.w32) so a mismatch fails
(gcc 13 warns only, E2 checked). Generic helpers branch on bit 31 (`testl; js`, same cost either
polarity, E2 measured) and call the coroutine functions directly: `add_callback` →
`async_callbacks_push(&c->callbacks)`, `del_callback` → remove, `start`/`stop` → no-op, `replay` →
`async_coroutine_replay`, `info` → `async_coroutine_info`, `dispose` → `ZEND_COROUTINE_RELEASE`,
object → `ZEND_COROUTINE_OBJECT` (CO 6). `async_awaitable_from_object(obj)`: `base = obj −
handlers->offset`; prefix match → `((async_event_ref_t *) base)->event`; else `base`.

### 2.7 Coroutine methods (S = status; `STARTED` = bit 8; `CANCELLED` = bit 4; `PROTECTED` = bit 16)

| Method | Formula | Pinned by |
|---|---|---|
| `isStarted` | `STARTED` | coroutine/005, 028 (false after spawn, true after the first run) |
| `isQueued` | `S == QUEUED && !(CANCELLED && !STARTED)` | 028 (true before the first run and after `suspend()`) |
| `isRunning` | `STARTED && !FINISHED` | 013, 038 |
| `isSuspended` | `S == QUEUED \|\| S == SUSPENDED` | 028, 038, info/002 (five never-run coroutines), fiber/019 |
| `isCancelled` | `CANCELLED && FINISHED` | 005, 006, 028, 029, 038 |
| `isCancellationRequested` | `(CANCELLED && !FINISHED) \|\| deferred_cancellation != NULL` | 006, 028, 029, info/002 |
| `isCompleted` | `S == FINISHED` | 005, 028, 038 |
| `getResult` / `getException` | `FINISHED ? result / exception : null` | 002, 003, 004, 028 |
| `getTrace` | `(QUEUED \|\| SUSPENDED) && fiber_context && fiber_context->execute_data` → backtrace on that stack | 009, 037 |
| `getSuspendFileAndLine` / `Location` | same guard → file and line of `fiber_context->execute_data`; else `[null, 0]` / `"unknown"` | 008 (types only) |
| `getSpawnFileAndLine` / `Location` | `coroutine.filename`, `lineno` (R:124-125), captured at spawn | 007 |
| `getAwaitingInfo` | the RFC slot; `[]` when NULL | 010, 026 |
| `cancel`, `finally`, `getId`, `asHiPriority` | the cancel slot; `FINISHED ? call now : append`; `std.handle`; no-op | 006, 014-018, 001 |

### 2.8 Cancellation and protect

`cancel(c, error, transfer, is_safely)`: (1) FINISHED → release, true. (2) `c == current && S !=
SUSPENDED` (running; "current but already parked" is SUSPENDED because suspend sets the status
before starting the edges) → `CANCELLED`; `exception = error` if none, else a new
`AsyncCancellation("Coroutine cancelled")`; the body continues; true. (3) `error == NULL` → new
`AsyncCancellation`. (4) PROTECTED → `deferred_cancellation = error` only if NULL, else release;
`CANCELLED` **not** set; true. (5) `was = CANCELLED; set CANCELLED`. (6) `!STARTED` → if CREATED,
enqueue; apply the cancellation to `waker.error` (a cancellation overrides a plain error, keeps an
existing cancellation, Fc:1344-1367); the switch-in sees `CANCELLED && !STARTED` and skips the body
("first entry with an error", RFC.md:169-171; the reference's IGNORED path `coroutine.c:466-499`);
`is_safely` is a plain cancel in S3 (zombie: S9). (7) else `was && waker.error` is a cancellation →
release (dropped); else apply; enqueue (push only if SUSPENDED). Delivery: at switch-in
`waker.error` is consumed and thrown; `CANCELLED` stays, so a later cancel finds `error == NULL`
and is delivered again (`edge_cases/003`). `protect(f)`: `was_protected = PROTECTED; set; call f;
if (!was_protected) { clear; if (deferred_cancellation) { set CANCELLED; throw it; NULL } }`
(D7). Deadlock and shutdown clear PROTECTED and cancel through the waker; an earlier
`deferred_cancellation` may then be thrown a second time at protect's exit: as the reference, no
test, left as is.

### 2.9 Suspend and enqueue slot contracts

**suspend** (core callers Rf:911, 965, `zend_gc.c:2163`; `Async\suspend`, `Async\await`):
refuse with an `Error` when there is no current coroutine or in scheduler context (D10 keeps the
blocked-switch case open). `c->fiber_context->execute_data = EG(current_execute_data)` (one
store; replaces the reference's location capture). If `S == QUEUED` (c enqueued itself: yield) keep
it; else `SET_STATUS(SUSPENDED)`, then start every edge (closed target → replay; a synchronous wake
in scheduler context takes the short path: `SET_STATUS(RUNNING)`, clean the edges, store the
error). **A park with zero edges is legal and parks** (CC 2). If RUNNING after the starts → fast
return; else switch handlers `leave`, tick, switch; on return `enter`. Return: `waker.error` →
clean, rethrow, false; else true.

**enqueue** (`c, error, transfer`): FINISHED → release, true (ts:1380-1386; refusing would throw
out of finish handlers). `error != NULL` → apply to `waker.error` (section 2.8 rule). CREATED →
first-enqueue bookkeeping (`first = S == CREATED` before the write; registry insert, count,
`scope->after_coroutine_enqueue` hook), push, QUEUED. SUSPENDED → push, QUEUED, clean the edges
(at once outside scheduler context; via `resumed_coroutines` inside it, unchanged from
`coroutine.c:860-866`). QUEUED → nothing more (one queue entry per coroutine). RUNNING: `c ==
current && !scheduler context` → push, QUEUED (the yield `Async\suspend` performs, `async.c:232`);
`c == current && scheduler context` → short path (above); otherwise refuse with the reference's
`Error("Cannot resume a coroutine that has not been suspended")`.

## 3. Core changes for S3.2 (minimal)

1. `ZEND_COROUTINE_F_STARTED` (bit 8); `ZEND_COROUTINE_IS_STARTED` reads it; contract comment:
   set by the scheduler right before the body's first instruction, never for a coroutine whose
   first entry carries an error (D3).
2. Rf:770: `ZEND_ASYNC_ON && !ZEND_COROUTINE_IS_FINISHED(coroutine)`; Rf:1038-1039: release
   without cancelling.
3. Rf:1375: drop the `ZEND_COROUTINE_IS_CANCELLED(current)` term (D5).
4. ts: set `F_STARTED` before the body (ts_coroutine_entry's normal branch, in-place, main);
   ts:1451 drops `!ZEND_COROUTINE_IS_STARTED`; ts:1069, 1083, 1166, 1774 unchanged in text.
5. EH_THROW window in `zend_fiber_vm_state` plus the reset before the jump, as the fork (D1).
6. Comments only: R:105-106 "bits 0-15 core, 16-31 the scheduler's"; R:358-363 "parked"
   instead of "suspended" (a self-yielded QUEUED coroutine has a frame the GC must see); CREATED =
   "allocated, not yet enqueued".
7. Already listed in S3.md section 9 (unchanged): GC precondition, GC pointers in `gc_reset`,
   NULL exception after `shutdown`, switch block at the bailout drain, current/main cleared at
   deactivation.

## 4. Replacement sites in the reference port

Must-change (S3 unless marked), from E1 section 9 plus the Critics' additions:

- Object ↔ coroutine: `coroutine.c:38, 166, 246, 254, 422` (`ZEND_ASYNC_OBJECT_TO_EVENT`) →
  `container_of(std)` / `zend_async_coroutine_from_object` (Ra:708-715).
- Event pointer → coroutine: `coroutine.c:1045, 1074, 1103` → functions on `async_coroutine_t *`;
  `async_API.c:759` (S5).
- Coroutine as its event: `coroutine.c:132-134, 145-153` (init) → `object_offset`; `248, 674`
  (`callbacks_free`) → on `callbacks`; `430, 492, 503`, `async_API.c:140, 149, 166` (`dispose`) →
  `ZEND_COROUTINE_RELEASE`; `666` (`SET_ZVAL_RESULT`) → drop; `668` (NOTIFY) → `callbacks`; `224,
  671, 697, 720, 740` (`EXC_CAUGHT`) → bit 18; `667, 670, 696, 705` → bit 17; `1335`, `scheduler.c:970,
  987` and the catch at `coroutine.c:547` → bit 19; `scheduler.c:708-716` **and 730-737** (`info`)
  → type dispatch; `async_API.c:1103`, `task_group.c:837, 841` (S5/S9).
- Fork `ZEND_COROUTINE_*` on `event.flags` → `->flags` (E1 9.4 list is complete by its grep): note
  `IS_FINISHED`/`SET_FINISHED` → status FINISHED, set where the reference sets CLOSED
  (`coroutine.c:607`, before notify and finally); `scheduler.c:785, 802` (`IS_YIELD`) →
  `context.status`; `scheduler.c:1247` (`SET_MAIN`) → the core does it; `scheduler.c:1468` → `S ==
  CREATED` read before the status write, hash lookup dropped. **No STARTED rename** (D3).
- Waker status readers and writers → RFC status (grep today): `ZEND_ASYNC_WAKER_IN_QUEUE/NOT_IN_QUEUE`
  (`coroutine.c:168, 601, 958`, `scheduler.c:962, 1450, 1657`), `waker->status` (`coroutine.c:84,
  104, 466, 472, 501, 512, 805, 834, 846-847, 859, 905, 1595`, `scheduler.c:512, 576, 918, 963,
  1139, 1464, 1478, 1582, 1674, 1685, 1726-1727`), `ZEND_COROUTINE_SUSPENDED` (`coroutine.c:1079,
  1490, 1618`; Fc:629) → the `isSuspended` formula; `IGNORED` writers (`coroutine.c:970`,
  `scheduler.c:918`, `async_API.c:172, 181, 199`, `scheduler.c:1486, 1495`) → cancel-before-run.
- Waker location: `scheduler.c:1713-1716` → one `execute_data` store; `coroutine.c:1548-1576,
  1085` → lazy from the frame.
- Generic awaitable code (type dispatch first): `async.c:315-371` (await), `scheduler.c:1135-1147`
  (`start_waker_events`), `coroutine.c:312-318` (waker GC walk → typed edges), moved fork code
  (`resume_when` Fc:1117-1241, waker teardown Fc:795-1039, `callback_resolve` Fc:1256-1266,
  `callbacks_notify` Fc:1689-1739); S5: `async_API.c:311, 432, 842, 1007, 1255-1270`, the
  cancellation-token sites; S8/S9 the channel, thread-channel and scope sites.
- Protect: `async.c:251, 276, 289-293` → `was_protected`.
- Handlers: finish handlers → `async_finish_handler_callback_t` in `callbacks`; switch handlers →
  `(handler, id)` entries; the fork's `ZEND_COROUTINE_FINISH` call (`coroutine.c:610-611`) dropped
  (R:70-81 has no finish argument).
- Event constructors (S4+): every `flags =` → `async_event_init()`.

Grep gates (extension sources, CI): no `zend_async_` type name (D12); no `->event.` or
`ZEND_ASYNC_EVENT_` applied to an `async_coroutine_t`; no `ZEND_ASYNC_OBJECT_TO_EVENT`; no
`waker->status`, `ZEND_ASYNC_WAKER_(NO_STATUS|WAITING|QUEUED|IGNORED|RESULT)`; no `F_YIELD`,
`IS_YIELD`; no `triggered_events`, `inline_triggers`, `current_iterator`; every `flags =` on an
event contains `ASYNC_AWAITABLE_F_EVENT`; `ZEND_ASYNC_EVENT_REFERENCE_PREFIX` absent (replaced by
`ASYNC_EVENT_REFERENCE_PREFIX`); build flags `-Werror=incompatible-pointer-types` / `/we4133`
present.

## 5. What must be measured before it is final (S3.md section 11 method; D2: ≤ 3 % instructions:u, allocations ≤ reference)

| Item | Benchmark | Expected effect (hypothesis) |
|---|---|---|
| Base layout (384 bin, flat waker, 16 B vectors) | B1-B5 | fewer bytes zeroed per spawn (488 → 304), no waker init; must meet D2 as a whole |
| Vector single-inline + corrected cursor | B4, B5 | −1 allocation per first await on a target (B4: every link); B5: O(1) removal per fan-in instead of O(k) under tombstones; wake order unchanged |
| Lazy suspend location, folded waker status, flat edges | B2, B3 | about −5 % (location) and up to −10 % (waker clean, hash ops) of a suspend; one store added |
| O6 inline `zend_fcall_t` | B1, both bins | −1 allocation per spawn; +128 B per live coroutine (bin 384 → 512); taken only if B1 instructions drop and the unbatched 100 000 run does not regress |
| Methods table per event type | S4 timer arm/fire | one dependent load per event call; expected < 1 %; if not, per-instance pointers for reactor types |
| Edge dedupe without a hash | S5 `await_all`, N ∈ {1, 2, 8, 100, 10 000} | sets the threshold for the lazy `HashTable *index` (waker 128 B, still bin 384) |
| Notify frame chain | B5 | one TLS load + chain walk per removal during notify; expected noise |
| Known-answer variant (S3.md) | B1 | one planted allocation must be visible before any number above is trusted |

## 6. Open questions for the owner (most important first)

1. D10: waits where switching is blocked (GC await from a blocked context) — deferred to 2026-10-02; the suspend contract above refuses in scheduler context only.
2. Names without a fork source: `async_awaitable_t`, `ASYNC_AWAITABLE_F_EVENT`, `async_event_methods_t`, `async_event_init`, `async_finish_handler_callback_t`, the notify frame — accept these or give others?
3. O6 (inline `zend_fcall_t`): is +128 B per live coroutine acceptable if B1 shows one allocation less and fewer instructions?
4. Methods table per event type instead of 8 per-instance pointers (56 B per event): accept pending the S4 measurement?
5. `DECISIONS.md` line: `isSuspended()` is false and `getSuspendLocation()` is `"unknown"` for the current (running) coroutine; the reference says true / the last location.
6. `enqueue` on a FINISHED coroutine: silent no-op (ts) or `Error` (the reference's resume)? Taken: no-op.

## 7. Bugs found in the reference/fork to fix there

1. **Nested `protect()`** (`async.c:251, 276, 289-293`): `protect(fn() => protect(fn() => suspend()))`, cancelled while the inner runs → the inner's exit clears PROTECTED and throws the deferred cancel inside the outer protect (D7). Reading; `protect/003` nests but never cancels.
2. **Waker GC walk reads triggers as events** (`coroutine.c:310-318` vs Fc:1203-1204): a coroutine parked in `await($future)` where the future object is only reachable through that wait: the trigger's `length` (1) is read as flags → `CLOSED`, nothing reported → the object is invisible to the cycle collector (leak). A `length` in 16-31 would be read as `ZEND_OBJ` and push a garbage pointer. Reading.
3. **Fork callbacks-vector iterator** (Fc:1654-1660): callbacks A B C D on one target; B's handler removes A → the fork runs A B B C, D never. E2 reproduced it in a transcription (`opt/iter_sim.c`), I confirmed the arithmetic by reading.
4. **Spawn failure after the registry insert** (`async_API.c:170-200`, `scheduler.c:1486-1495`): a `SpawnStrategy::afterCoroutineEnqueue` that throws leaves the coroutine `IGNORED` without `CANCELLED`; `async_coroutine_execute` (`coroutine.c:466-470`) then skips finalize, so `ASYNC_G(coroutines)` keeps the pointer after `OBJ_RELEASE`; `get_coroutines()` or the deadlock report reads freed memory. Reading, not run.
5. **Bailout runs PHP finally handlers** (`scheduler.c:960-968`, `coroutine.c:547-552, 1329-1336`): OOM in coroutine A while B (started, queued, with `finally()`) waits: B is switched into with the BAILOUT transfer flag, `SET_BAILOUT` is not applied, finalize calls B's finally handlers during the bailout. Reading.
6. **A user class implementing `Async\Completable` is awaited as an event** (`async.c:302`, no `interface_gets_implemented`): `await(new class implements Async\Completable { … })` → `ZEND_ASYNC_OBJECT_TO_EVENT` with `handlers->offset == 0` reads the `zend_object` header as an event and writes a callback into it. Reading.

Hidden decisions taken here (rule 12): the notify frame chain instead of a thread-local pair or
the fork's pointer; handle ids from one per-thread counter; spawn failures as cancel-before-run;
a per-edge "stop owed" bit instead of `events_stopped`; `CLOSE_FD` kept at bit 9 for the poll
subtype; enqueue on FINISHED a no-op; `callbacks` as the waiters' name. Verified today in
addition: `future.c:586` via F:1941 is the only `RESULT_USED` reader; the reactor counts
start/stop (`libuv_reactor.c:355-367`). Not run: nothing here was built or benchmarked; sizes are
compiler-computed (gcc 13, x86-64), the reference sizes are the brief's gdb measurements.
