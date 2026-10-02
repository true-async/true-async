# Wait model: Sage ruling (2026-10-02)

Keys as in `structures-final.md` (SF): bare names = php-async `1fdacf8`; F/Fc = fork
`Zend/zend_async_API.h`/`.c` (`863f6dd90cf`); R/Ra/Rf/Rg = RFC `zend_async_API.h`/`.c`/
`zend_fibers.c`/`zend_gc.c` at `834811f2d88`; ts = `ext/test_scheduler/test_scheduler.c`; D =
`EDMOND-DECISIONS.md`; FO/FC = `frame-records-optimizer.md`/`-critic.md`; PO/PC = the passive-edges
pair. Every line number below was re-read today. "Reading" = verified by reading, not run. Run
today: the size probe (`scratchpad/sage2/probe.c`, gcc 13 -O2, x86-64, the RFC headers); nothing
else was built, run or benchmarked.

## 1. Verdicts

### 1.1 Variant B (wait records in the waiting frame)

- **B is adopted.** Against the reference it keeps every protocol position Edmond trusts (link
  before `suspend()`, generic unlink at enqueue, typed queue state handled by the frame after
  resume, `scheduler.c:1114-1150` replaced by phase 1) and removes only storage: the per-coroutine
  events hash and triggers, the start/stop loop, one owned reference per edge, the per-wait waker
  init and clean, the methods table. Counted, not measured: fewer instructions on B2 and B4 (FO 10),
  allocations equal or fewer on every operation (FO 4). D2 is decided by B1-B5.
- **B over A.** A kept a 120 B waker with inline edges that owned nothing yet were still generic; B
  needs 40 B, answers PC 0A/0B (dump text, S7 type) through the record's kind without a header tag,
  and answers D25: no class is needed (the linking code names the kind) and no generic ownership is
  assumed (the frame keeps its target alive by whatever it has: argument, zval copy, typed owner).
- **Edmond's sketch, one correction.** The frame does not `del` after `suspend()`; the waker unlinks
  through its pointer at wake (FO 1), and `suspend()` guarantees `waker.wait == NULL` on every exit
  (FC 1). Reasons: U4-U6 need the waker-side path anyway; a record left linked after the wake keeps
  receiving firings and outlives one-shot targets; the exit guarantee is one predicted-NULL compare,
  placed once in `suspend()` instead of at every call site. What stays frame-side is typed protocol
  state (a channel's queue membership), exactly as the reference (`channel.c:720-743`).
- **The window is closed by one predicate.** The tick (`scheduler_next_tick`, microtasks, reactor
  dispatch, in-place finalize, deadlock resolution) runs with `zend_fiber_switch_block()` raised.
  Every extension wait entry refuses on `zend_fiber_switch_blocked()` (D14), the GC defers on it
  (D13), the core's own `Fiber::suspend()` check fires first (Rf:1370). A second wait by the current
  coroutine inside its own window is therefore impossible; FC 2's `wait != NULL` test becomes a
  debug assert. Cost: two counter writes per tick, where the reference already writes the
  scheduler-context flag twice (`scheduler.c:1525, 1572`).
- **Immediate unlink everywhere (FO option O-F1) is the design; the reference's deferral
  (`resumed_coroutines`) is the fallback.** Reason: with no owned reference per edge the suspected
  purpose of the deferral (no release inside the firing event's own callback, Fc:1689-1739) is
  gone, and the deferral is what lets a second target reach a record in the same tick (Fc:1256-1260
  overwrite and leak, PC 12). Open question 1 asks Edmond the purpose; section 3 names the run.

### 1.2 Critic findings on variant B (FC 1-12)

| FC | Ruling | Reason / fix adopted |
|---|---|---|
| 1 suspend returns false with no wake | **Stands.** Fix: `suspend()` exits with `wait == NULL` on every path (refusals, launch failure, the tick's false return without a wake at `scheduler.c:1607-1611`/`1745`, bailout); refusals also move to phase 0 of every extension entry so a refused wait never links. Verified: the reference survives only by `zend_async_waker_clean` at `async.c:366-368, 379` | the reference refuses after the launch and in scheduler context (`scheduler.c:1636-1645`) with nothing cleaned by `suspend()` itself |
| 2 second wait orphans the first | **Stands; fix differs.** The window is switch-blocked (1.1), so a nested wait is refused before it links: `Async\await`/`suspend` by D14, `Fiber::suspend` by Rf:1370, the GC by D13 (the await slot also returns false without throwing when `zend_fiber_switch_blocked()`, so a GC entered from a release inside a microtask returns 0 and its already enqueued coroutine collects next tick, Rg:2217-2234). Phase 0 asserts `wait == NULL` in debug builds, no release-build compare | one predicate instead of two; the reference's own second-wait path is broken (section 5, bug 2) |
| 3 channel protocol | **Stands.** The wake-time unlink is generic only (callbacks vector, `F_COUNTED`, disarm). Typed queue membership is removed by the frame after resume (`channel.c:720-743`, kept) and by the kind's `abort` hook only where the frame never runs again (U4-U6). Verified: the reference's own wake already removes the waiter from the event's vector and leaves it queued (`channel.c:464-467`) | two different facts live in two structures; FO conflated them |
| 4 U5 incomplete | **Stands.** U5 is an invariant: every allocation or foreign call between the first link and the switch runs under a `zend_try` whose catch aborts the wait and re-raises; the fiber-context pool miss (`scheduler.c:531-534, 647-650`) is such a branch (59 instructions per miss, FO 10). Debug: `ASYNC_G(wait_unprotected)` set at the first link, cleared at the switch, asserted clear in every coroutine catch site | a list is wrong the day a branch is added |
| 5 Traversable `await_*` | **Stands (S5).** The compound record may live in the waiter's frame only if the heap `await_context` (kept, `async_API.c:956`) carries a `finished` flag the iterator coroutine tests before every link and every write into a chunk; the waiter sets it on every exit. Whether the iterator coroutine is also cancelled when the waiter leaves is open question 2. The reference has the same dead-waiter write (section 5, bug 4) | the saving is N records per Traversable wait; the hazard is a write into a popped frame |
| 6 fired-but-not-waking record | **Stands.** Rule: a record's callback fired by a one-shot target (coroutine finish, Future completion, Timeout fire) removes the record from that vector at the cursor (O(1)) and clears `rec->event` before anything else, whether or not it wakes the waiter. Vector teardown: `F_RECORD` element → debug assert; release build clears `rec->event` and calls nothing. The `kind`/`dispose` union stays: teardown tests `F_RECORD` before `dispose`, and nothing else calls `dispose` generically (Fc:1673 removal is replaced by the typed unlink) | 40 B record kept; the hazard was the generic `dispose` call, not the union |
| 7 stale result | **Stands.** `suspend()`'s error exit (where `waker.error` is consumed) does `zval_ptr_dtor(&result); ZVAL_UNDEF` (cold path). The success path moves the result out (`ZVAL_COPY_VALUE` + UNDEF). With immediate unlink the "write only while UNDEF" guard is not needed; it returns only with the deferral fallback | the reference clears in `zend_async_waker_clean` (`coroutine.c:121-123`) |
| 8 arming order | **Stands.** Phase order: 0 refusals, 1 completion checks of every target (an `await_*` scan over all items before any side effect), 2 reservations, 3 arms and submits, 4 links (stores only), 5 `suspend()`. An arm or submit never dispatches before the tick, so it may precede the links; an exception in 1 or 2 undoes nothing, in 3 a synchronous cancel. A bailout in 3 with an op in the frame is an S4.1 constraint (the queue must not dispatch into dead frames after a bailout) | FO's invariant L put the completed-target check after the arm |
| 9 userland Completable | **Stands** (already SF CO 5): `Awaitable` gets `interface_gets_implemented` rejecting classes the extension did not register; `kind_of` runs only on registered classes | `async.stub.php:9` has no guard; `async.c:302` accepts any `Completable` |
| 10 U6 call site | **Stands.** U6 runs in RSHUTDOWN (`zend_deactivate_modules`, `main.c:1964`), before `zend_deactivate` frees the object store (`:1990`), for every registered coroutine not FINISHED with `wait != NULL`; also wherever the scheduler frees a context of an unfinished coroutine | after a fatal error no destructor runs (`main.c:1486`) |
| 10 awaiting-info field | **Stands.** `async_awaiting_info_vector_t *awaiting_info` (lazy, ts:387-418 shape) takes the slot of `finally_handlers`, which D21 moves to S9; the coroutine stays 296 B; S9 adds `finally_handlers` back at 304 B, still bin 320 (probe) | no field, no slot implementation |
| 10 S7 completers for FUTURE/CHANNEL | **Stands (S7.1).** A record contributes the waiter → target edge and the kind; completers of a FutureState or a channel's other side need a reachability pass from the roots. `kind->completers` enumerates only what the target itself knows (COROUTINE: itself; SCOPE/ITERATOR: their coroutines) | no reverse references exist |
| 10 foreign zero-record parks and liveness | **Fact missing.** In S3 every zero-record park (fiber, GC) is woken by another coroutine, so counting it as a parked wait with no external source is right. A foreign extension parking through the RFC `SUSPEND` with an external wake has no way to say so: an S8 change-request candidate (R:60-68 could carry an "external" bit). Settled by: S6's `signal`/`exec` tests on the RFC hooks | |
| 10 window against R:64 | **Resolved by immediate unlink**; with the fallback deferral the exposure is diagnostics only | |
| 10 line | `async_API.c:956`, accepted | |
| 11 keep both unlink sites | **Half.** The waker-side unlink stays; the "after every `suspend()`" line is inside `suspend()` itself (FC 1 fix), not in the frames; typed kinds remove typed state frame-side (FC 3) | one site, one compare |
| 12 sound | Accepted as listed (sizes, U4 placement, `bailout_all_coroutines`, ported `zend_try` sites, `call_on_main_stack`, `Fiber::resume`, the `zend_try` cost method) | |

### 1.3 The passive-edges Critic's table under B (FC's last section)

0A dump text: `kind->info`. 0B S7 type: `kind`; completer enumeration: S7.1 reachability (1.2).
0C `thread_channel_receive` with a classless trigger (`thread_pool.c:386`): typed wakeup parameter,
S10. Lost cleanup at destroy: U4-U6 plus the `suspend()` exit guarantee. 1 `F_COUNTED` leak: the
decrement is keyed on the record's own bit and runs in every unlink and abort, so it cannot leak
while W holds. 1/5 Timeout liveness (`channel/027`, `scope/023`): a waited Timeout's record is
`F_COUNTED`. 2/7 eager Timeout: `ARM_ON_WAIT`, armed in phase 3 after the completion scan. 3
`EXCEPTION_HANDLED` on a closed Thread: phase 1 sets it (S5/S10). 4 TaskGroup readiness: S9. 12
result overwrite: immediate unlink.

### 1.4 Event methods

**None survives on the event.** Header: `flags`, `ref_count | object_offset`, `callbacks`: 24 B.
Per-type behaviour that remains and where it sits: the wake function per subscriber (`callback`,
on the record, as Fc:1727); the kind descriptor on the record (`info`, `unlink`, `abort`,
`completers`), chosen by the code that links and therefore known for classless targets; the
Timeout's arm/disarm, a flag-dispatched direct call (`ASYNC_EVENT_F_ARM_ON_WAIT`, one type);
completion state read in phase 1 from the completion block (PO rule 3) or from
`zend_coroutine_t.result/exception`. D25's two objections are answered in 1.1.

### 1.5 Awaiting info and S7

The record is the RFC awaiting-info registration (R:60-68): created by whoever suspends, wiped at
enqueue (U1), diagnostics only; `get_awaiting_info` (R:394) walks `waker.wait` calling
`kind->info(c, rec)`, then the lazy RFC vector of foreign registrations. Zero-record parks get a
line from the coroutine's state (fiber: `F_FIBER` and `context.status`; GC coroutine; `suspend()`:
QUEUED). Tests pin only `is_array` (`coroutine/010`, `026`). S7 walks waiter → record → (`event`,
`kind`): `kind->completers` where the target knows them, a reachability pass for Future and
channel, `F_COUNTED` marks an external root (S7.2 "IO and timer waiters not reported"); core-owned
edges (a fiber's `caller_coroutine`, a GC finish handler's `waiter`) are read from the target side.

## 2. The final wait model

### 2.1 Layouts

Computed by the probe (`scratchpad/sage2/probe.c`; gcc 13 -O2, x86-64, RFC headers; `zend_coroutine_t`
144, `zend_object` 56, `zval` 16 confirmed). Measured (gdb, brief): reference `async_coroutine_t`
544 / allocation 528 / bin 640; fork event 104, fork waker 248. ZendMM bins: 256, 320, 384, 448,
512, 640 (`zend_alloc_sizes.h:47-52`).

```c
/* Vector element base: fork F:830-834. 24 B (probe). */
struct _async_event_callback_s {
	uint32_t ref_count;                           /*  0 4  heap subscribers only; a record never reads it */
	uint32_t flags;                               /*  4 4  bit 0 F_RECORD, 1 F_COUNTED, 2 F_TYPED (kind->unlink set) */
	async_event_callback_fn callback;             /*  8 8  wake function: resolve, cancel, timeout, await_* item */
	union {
		async_event_callback_dispose_fn dispose;  /* 16 8  heap subscribers (finish handlers, signal callbacks) */
		const async_wait_kind_t *kind;            /* 16 8  F_RECORD */
	};
};
/* The wait record: fork zend_coroutine_event_callback_t F:862-868. 40 B (probe); on the waiting frame's C stack. */
typedef struct {
	async_event_callback_t base;                  /*  0 24 */
	async_coroutine_t *coroutine;                 /* 24  8  the waiter */
	async_awaitable_t *event;                     /* 32  8  non-NULL iff linked in that target's callbacks; no reference owned */
} async_coroutine_event_callback_t;
/* One const descriptor per wait kind, .rodata. 32 B (4 pointers; the probe had 3 = 24). */
typedef struct _async_wait_kind_s {
	zend_coroutine_awaiting_info_fn info;         /* R:68; data = the record */
	void (*unlink)(async_coroutine_event_callback_t *);  /* wake-time typed removal (op cancel, chunk walk); NULL = vector removal */
	void (*abort)(async_coroutine_event_callback_t *);   /* the frame never runs again (U4-U6): typed queue detach; NULL = nothing */
	void (*completers)(async_coroutine_event_callback_t *, void *walker); /* S7; NULL until S7 */
} async_wait_kind_t;
/* Waker. 40 B (probe); SF 2.4 had 120, fork 248. */
typedef struct {
	zend_object *error;                           /*  0  8  R:248-254 delivery channel */
	zval result;                                  /*  8 16  moved out by await; cleared on the error exit */
	async_coroutine_event_callback_t *wait;       /* 24  8  first record of the current wait; NULL = none linked (W) */
	uint32_t wait_count;                          /* 32  4  (+4 pad) records are contiguous */
} async_waker_t;
/* Event header. 24 B (probe); SF 2.5 had 32 with the methods pointer. */
struct _async_event_s {
	uint32_t flags;                               /*  0 4  bit 31 = 1; 5 F_EXTERNAL, 8 F_ARM_ON_WAIT, 12 F_COMPLETION; rest as SF 2.5 */
	union { uint32_t ref_count; uint32_t object_offset; }; /* 4 4 */
	async_callbacks_vector_t callbacks;           /*  8 16  SF 2.4 vector, single-inline, corrected cursor */
};
typedef struct { async_event_t base; zval result; zend_object *exception; } async_completion_event_t; /* 48 B (probe) */
/* S3 coroutine. 296 B; allocation 280 (sizeof - 16); bin 320 (probe). S9 with finally_handlers: 304 / 288 / bin 320. */
struct _async_coroutine_s {
	zend_coroutine_t coroutine;                   /*   0 144 */
	async_fiber_context_t *fiber_context;         /* 144   8 */
	async_callbacks_vector_t callbacks;           /* 152  16  waiters' records + finish handlers */
	async_waker_t waker;                          /* 168  40 */
	async_scope_t *scope;                         /* 208   8  D11 */
	zend_object *deferred_cancellation;           /* 216   8 */
	async_awaiting_info_vector_t *awaiting_info;  /* 224   8  lazy; foreign RFC registrations (ts:387-418 shape) */
	async_coroutine_switch_handlers_vector_t *switch_handlers; /* 232 8 lazy */
	zend_object std;                              /* 240  56 */
};
```

Stack cost per wait (probe): `await($x, $cancellation)` 80 B; an `await_*` frame with K = 8 inline
items, cancellation and timeout records, item zvals and keys: 704 B, against a 2 MiB fiber C stack.
Hidden decisions here: `kind` stays in the union (FC 6 ruling); `F_TYPED` on the record spares the
two loads of `kind->unlink` for plain kinds; `wait_count` is not replaced by a terminator (saves 8 B,
changes no bin).

### 2.2 Protocol

**States and invariants.**
- W: `waker.wait != NULL` only while the coroutine is parked (SUSPENDED) or is the current coroutine
  between its first link and its switch out inside `suspend()`. In both the frame holding the
  records is alive and unmodified.
- F: a linked record's target outlives the link because the frame keeps it alive: the call argument
  (`Async\await`, `$this` of a channel or pool method), one `Z_ADDREF` per `await_*` item (the
  reference's count, Fc:1240-1241), typed ownership of a classless event, the await slot's
  reference on the GC coroutine (ts:542).
- L: after the first link nothing allocates, throws or runs foreign code until the switch, except
  under a U5 `zend_try`. Links are stores into reserved capacity.
- `rec->event != NULL` ⇔ the record is in that target's `callbacks`. Whoever removes it (unlink,
  self-removal, typed wake code, teardown) clears it.

**Phases of a wait** (every extension entry: `await`, `await_*`, channel and pool waits, `delay`,
the await slot):
0. Refusals, before any side effect: no current coroutine; `zend_fiber_switch_blocked()` (D14; the
   tick raises it, 1.1); self-await. Extension entries throw `Error`; the await slot returns false
   without throwing on the blocked case (the GC treats false as "not collected", Rg:2223-2234).
   Debug: `ZEND_ASSERT(c->waker.wait == NULL)`.
1. Completion checks of every target: FINISHED coroutine → `result`/`exception` in place; closed
   `F_COMPLETION` event → its block (sets `EXCEPTION_HANDLED` when it delivers an exception, PC 3);
   fired Timeout or cancellation → throw. `await_any` returns here; `await_all` counts.
2. Reservations: vector capacity per link, the `await_*` heap chunk for N > K. May bail out: nothing
   is linked yet.
3. Arms and submits: `F_ARM_ON_WAIT` arm at 0 → 1 subscribers, a Timer or IO op submit. Dispatch
   happens only in a tick, never here. A synchronous failure undoes itself and returns.
4. Links: per record 5 stores (as Fc:1140-1167) and a push into reserved capacity; `c->waker.wait =
   rec; wait_count = n`. Order inside a wait: items, cancellation, timeout.
5. `suspend()`.

**`suspend()` contract** (replaces SF 2.9's suspend paragraph; core callers Rf:911, 965, Rg:2163 arrive
with zero records):
```
0. refuse (Error, return false) when: no current coroutine; ZEND_ASYNC_IS_SCHEDULER_CONTEXT ||
   zend_fiber_switch_blocked(); the launch fails. Before returning: if (c->waker.wait) unlink(c).
1. c->fiber_context->execute_data = EG(current_execute_data)  (one store; getTrace, GC, location)
2. if S != QUEUED: SET_STATUS(SUSPENDED)                        (QUEUED = the yield of Async\suspend)
3. window open: ZEND_ASYNC_SCHEDULER_CONTEXT = true; zend_fiber_switch_block()
   tick: heartbeat, microtasks, reactor dispatch (throttled), deadlock resolution, in-place
   finalize of a cancelled-before-run coroutine, pool-miss context creation. Each branch that can
   allocate or run PHP code runs under zend_try; catch: abort(c); window close; re-raise (U5).
   A wake of c in the window takes U2 (enqueue short path: unlink, S = RUNNING, error stored).
   window close: zend_fiber_switch_unblock(); ZEND_ASYNC_SCHEDULER_CONTEXT = false
4. if S == RUNNING: goto out                                    (woken in the window, no switch)
   switch handlers leave; switch; on return: enter
   BAILOUT transfer (scheduler.c:353-355 shape): abort(c); zend_bailout()        (U4)
out:
5. if (c->waker.wait) unlink(c)        (one compare; non-NULL only on a false return without a wake,
                                        scheduler.c:1607-1611 / 1745 shape)
6. if (waker.error) { e = error; error = NULL; zval_ptr_dtor(&result); ZVAL_UNDEF(&result);
   rethrow(e); return false }  return true
```
Every path out of `suspend()` leaves `wait == NULL`; the frame never touches a record link after
`suspend()` returns. The tick's false return without a wake is the only case where step 5's compare
is non-NULL.

**`enqueue(c, error, transfer)`** (SF 2.9 with the unlink made concrete): FINISHED → release, true.
`error` → applied to `waker.error` by SF 2.8. CREATED → first-enqueue bookkeeping, push, QUEUED.
SUSPENDED → push, QUEUED, `unlink(c)` at once, in scheduler context too (U1; fallback: the
reference's deferral through `resumed_coroutines`, drained after every tick). QUEUED → nothing.
RUNNING: `c == current && scheduler context` → U2 short path (`unlink(c)`, S = RUNNING); `c ==
current` outside → push, QUEUED (yield); otherwise refuse.

**`async_wait_unlink(c)`**: for each of `wait_count` records: if `rec->event`: `F_TYPED` →
`kind->unlink(rec)`, else remove from the target's `callbacks` (coroutine target by bit 31: its
`callbacks` directly; SF 2.4 cursor rule, the cursor slot checked first so self-removal during a
notify is O(1)); `rec->event = NULL`; `F_ARM_ON_WAIT` target whose length reached 0 and which has
not fired → disarm (synchronous cancel, PC 11). If `F_COUNTED`: `external_waits--`, clear the bit.
Then `wait = NULL; wait_count = 0`. Idempotent, allocates nothing, runs no PHP code.
**`async_wait_abort(c)`**: for each record `if (kind->abort) kind->abort(rec)`, then `unlink(c)`.

| Site | When | Call | Records intact because |
|---|---|---|---|
| U1 | enqueue of a SUSPENDED coroutine | unlink | parked |
| U2 | enqueue of the current coroutine inside its window | unlink | inside `suspend()`, below the records |
| U3 | an exception in phases 1-3 or in the frame after a refused `suspend()` | unlink (the frame removes its own typed state) | own frame |
| U4 | `suspend()` returns with the BAILOUT transfer | abort, then `zend_bailout()` | nothing unwound yet |
| U5 | a bailout inside the window on c's stack | abort in the `zend_try` catch, re-raise | catch frame below the records |
| U6 | RSHUTDOWN, and a context freed for an unfinished coroutine | abort | stack still mapped |

**Self-removal rule** (FC 6): a record callback fired by a one-shot target removes its record from
that vector at the cursor and clears `rec->event` before anything else; a Timeout fire also marks
the Timeout fired so no disarm follows. Multi-shot targets (channel close, ARM_ON_WAIT restart)
leave records in place. **Teardown** of a `callbacks` vector: an `F_RECORD` element is a debug
assertion failure; in release `rec->event = NULL` and no `dispose` call (W makes the write safe).
Debug asserts: `wait == NULL` at finish, in every coroutine catch site (`coroutine.c:546-551`
position, Rf:805-828), before a context returns to the pool; `ASYNC_G(wait_unprotected)` clear in
every catch site.

**Bailout.** `bailout_all_coroutines` (`scheduler.c:949-1000`) switches into every started
unfinished coroutine with BAILOUT (queued ones first): each unwinds through U4. A waiter woken by a
finalize during the drain is unlinked by U1 before it is switched into. Never-started coroutines
hold no records. Main parks on the OS stack and takes U4 like any other. The core forwards a bailout
across fibers through `ZEND_FIBER_TRANSFER_FLAG_BAILOUT` (D15). Rejected alternative: run the tick
and pick the next coroutine before linking (no U5 at all); rejected because deadlock resolution
runs inside the tick and must see the suspender's own wait.

### 2.3 Every wait

| Wait | Records and kind | Notes against the reference | Allocations vs reference |
|---|---|---|---|
| `await($coroutine)` | 1 COROUTINE record in the target's `callbacks`; result read in place | `async.c:300-381` minus `WAKER_NEW`, the hash, start/stop, `waker_clean` | 0 = 0 |
| `await($future)`, `Future::await` | 1 FUTURE record; completed future: completion block in phase 1 | | 0 = 0 |
| `await($x, $cancellation)` | 2 records; the cancellation's wake function is `cancel` (Fc:1272 shape); `$x === $cancellation` drops the second (`async.c:323-325`) | | 0 = 0 |
| `await($x, timeout(ms))` | 2 records; TIMEOUT record is `F_COUNTED`; `F_ARM_ON_WAIT` arms in phase 3 at 0 → 1, disarms in unlink at 1 → 0 unless fired; fired Timeout → phase 1 throws | reference arms at suspend (`scheduler.c:1147`), stops at wake; `channel/027` and `scope/023` pass through `F_COUNTED` | 0 = 0 |
| `Async\suspend()` | self-enqueue, zero-record park | `async.c:223-235` | 0 = 0 |
| `delay(ms)` (S4) | Timer op in the frame + 1 TIMER record (`F_TYPED`: unlink cancels the op; `F_COUNTED`); the fire callback self-removes | | −1 (`libuv_reactor.c:1238`) |
| `await_any/all/first`, `*_of` over an array (S5) | K = 8 inline records + cancellation + timeout; N > K: one heap array of N + 2 sized once N is known; item zvals held per item (F); phase 1 scans all items first; item callbacks self-remove | `async_API.c:900-1150`: context `:956`, one callback per item `:1002`, a waker timeout `:945` | −(N + 1) for N ≤ 8, −N above |
| the same over a Traversable (S5) | heap `await_context` kept with a `finished` flag; chunk records in the waiter's frame only under the FC 5 rule; AWAIT_ITER kind's `unlink`/`abort` walk the chunks | open question 2 | −N + chunks, or 0 if the reference shape is kept |
| channel send/recv (S9) | record in the channel's `callbacks` (for close) and the typed queue; the channel's wake removes the vector entry and leaves the queue entry (`channel.c:462-467`); the frame removes it after resume (`:720-743`); CHANNEL kind `abort` = queue removal | the waiter struct can move to the frame only with the abort hook (open question 3) | −1 (`channel.c:692`) with the hook |
| pool, scope, task group, thread channel, fs, exec (S9/S10) | as FO 4 with the kind's `abort` where a typed structure holds the record | | as FO 4 |
| IO through the provider (S6) | the op in `run()`'s frame + 1 IO record (`F_TYPED`, `F_COUNTED`) | PO section 2 | equal to the RFC design |
| GC from a coroutine (Rg:2201-2235) | the await slot: the GC checks `zend_fiber_switch_blocked()` first (D13); the slot's phase 0 returns false without throwing if it is nevertheless blocked; otherwise 1 COROUTINE record in the slot's frame and one reference on the GC coroutine (ts:542) | | 0 = 0 |
| GC coroutine waiting for destructors (Rg:2123-2163) | zero-record park; the edge is the finish handler on the destructor coroutine (heap, SF 2.4) | | equal |
| `Fiber::start/resume` caller (Rf:939-985), `Fiber::suspend` (Rf:887-937) | zero-record parks; the edge is `fiber->caller_coroutine`; a Fiber dropped while parked is cancelled by Rf:770 (D3 form) → U1; `Fiber::suspend` inside the window fails at Rf:1370 | | 0 = 0 |

**Cancellation.** `cancel(c)` on a parked c → enqueue with error → U1 → c runs → step 6 throws
with the result cleared. On a QUEUED c already woken with a result: the error is applied (SF 2.8
(7)), step 6 discards the result. A cancellation token is the second record of the wait.

**PHP GC.** Records own nothing; the coroutine's `get_gc` reports no record (SF bug 2 gone); the
parked frame is reachable through the RFC execute-data slot (R:363); targets live by F.

**Fiber stacks.** A context returns to the pool only after its coroutine finished (FO 5.3, not
re-read), when W gives `wait == NULL`; a stack freed without a switch goes through U6.

## 3. What must be measured before it is final

| Item | Run | Decides |
|---|---|---|
| The whole model (296 B object, no hash, no start/stop, exit compare, window block) | B1-B5 against the reference, `instructions:u` per operation and allocations (S3.md section 11 method, known-answer variant first) | D2 (≤ 3 %, allocations ≤ reference) |
| Immediate unlink (O-F1) against the deferral | S3: B4, B5 (wake outside scheduler context: no difference expected); S4: N `delay()` waiters fired in one reactor tick, N ∈ {1, 100, 10 000}, with and without `resumed_coroutines` | which of the two is the design |
| U5 `zend_try` per branch is not executed per operation | B2 and B4 with a debug counter of U5 entries per operation (expected 0 per operation outside pool misses and reactor ticks) | whether U5 moves to once per tick |
| Pool-miss branch cost | B4 10 000 × 10 (fiber supply) against the reference | the 59 instructions per miss are inside D2 |
| `await_*` inline K = 8 | S5: N ∈ {1, 2, 8, 100, 10 000} | K and the heap-chunk threshold |
| Self-removal at the cursor under fan-in | B5 (1000 waiters on one target) | the SF 2.4 cursor rule under 1000 removals in one notify |

## 4. Open questions for the owner (most important first)

1. `resumed_coroutines` (`coroutine.c:861-866`, `scheduler.c:426-436`): was the deferral there only to keep a wake from releasing the firing event inside its own callback, or for another reason? Only the first is removed by B.
2. Traversable `await_*` (S5): when the waiter leaves (`await_any` satisfied, cancelled, timed out), may the iterator coroutine be cancelled, or must it run the Traversable to its end as the reference does?
3. Channel waiters (S9): on the frame with the kind's `abort` hook (−1 allocation per parked send/recv), or on the heap as the reference?
4. Names without a fork source: `async_wait_kind_t`, `async_wait_unlink`/`abort`, `ASYNC_CALLBACK_F_RECORD`/`F_COUNTED`/`F_TYPED`, `awaiting_info`, `ASYNC_G(wait_unprotected)`.
5. The tick raised as a switch-blocked window (1.1): a destructor that calls `Fiber::suspend()` inside a tick now gets the core's `FiberError` instead of the reference's `Error` text; accept?

## 5. Bugs in the reference and the fork found on the way (reading, not run)

1. **A bailout is lost in `TRY_HANDLE_SUSPEND_EXCEPTION_BOOL`** (`scheduler.c:79-88`): under graceful shutdown the macro calls `switch_to_scheduler(transfer)` and discards its return; a BAILOUT transfer into this coroutine (`bailout_all_coroutines`, `scheduler.c:968`) comes back as `true` and is ignored; the macro returns false, `suspend()` falls through to `resuming:` and the coroutine keeps executing PHP code during the bailout. Scenario: `exit()` in coroutine A starts graceful shutdown; coroutine B is in its tick when a microtask throws; B parks in `switch_to_scheduler`; an OOM in C runs `bailout_all_coroutines`, which switches into B with BAILOUT.
2. **A second wait by the current coroutine cleans the first wait** (`coroutine.c:68, 84-86`: `async_waker_new` calls `zend_async_waker_clean` when `status != NO_STATUS`). Entry: PHP code running on the suspender's stack inside its own tick with `ZEND_ASYNC_CURRENT_COROUTINE` unchanged: an output handler invoked by the deadlock dump (`scheduler.c:699-746` via `php_printf`, scheduler context false at `:1572`) or a destructor run by a microtask release. Scenario: main awaits `$x`; a deadlock prints the report through `ob_start(function ($b) { try { Async\await(Async\spawn(fn() => 1)); } catch (\Throwable) {} return $b; })`; the inner `await` removes main's edge on `$x`, links its own, and nests a second `suspend()`; after it returns, the outer `suspend()` continues with a waker that no longer waits for `$x`. The end state was not traced further; the first step (the outer edge removed) is enough to call it a bug.
3. **Dead call** `zend_fiber_switch_blocked()` at `scheduler.c:1243` (result discarded; the reference never honours the block, D14's check is new).
4. **The Traversable iterator links after the waiter returned** (`async_API.c:526-618`): `await_iterator_handler` has no finished check before `zend_async_resume_when(await_iterator->waiting_coroutine, …)` (`:618`); after `await_any_or_fail(gen())` returned on item 0 (`:1130-1133`), a later `yield` from the generator adds an edge to the waiter's current waker (`zend_async_waker_is_event_exists` reads it at `:540`). Scenario: FC 5's `gen()`.
5. Listed elsewhere, not repeated: SF section 7 (six), PO section 9 (three), PC 3, 4, 12, FO 12 (parks without `zend_try`).

Hidden decisions (rule 12) beyond those named inline: the window is closed with the core's switch
block rather than a new flag; `kind` gets `abort` as a fourth pointer; the `await_*` phase-1 scan
over all items precedes any arm (a second pass over N flag words); the award of the one owner
question to the deferral's purpose. Not run: no benchmark, no build of the extension; sizes are
compiler-computed, reference sizes are the brief's gdb measurements.
