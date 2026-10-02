# Frame-local wait records against every wait (optimizer, 2026-10-02)

Keys as in `structures-final.md`: bare names = php-async `1fdacf8`; F/Fc = fork `Zend/zend_async_API.h`/`.c`
(`/home/user/php-src-true-async`, `863f6dd90cf`); Ff = fork `Zend/zend_fibers.c`; R/Ra/Rf = RFC
`zend_async_API.h`/`.c`/`zend_fibers.c` at `834811f2d88`; Rg = RFC `Zend/zend_gc.c` (`git show
834811f2d88:Zend/zend_gc.c`); ts = RFC `ext/test_scheduler/test_scheduler.c`; PC = `passive-edges-critic.md`;
PO = `passive-edges-optimizer.md`; SF = `structures-final.md`. Every line number was read today.
Built and run: the size probe (section 2, gcc 13 against the RFC headers of the `rfctree` copy, identical
to `rfc-core/Zend/zend_async_API.h` by `diff`) and a `zend_try` cost probe (section 10, callgrind on a
stand-in, not PHP). Nothing else was built, run or benchmarked.

## 1. Verdict

The frame-local model holds for every wait in the reference and in both cores, with one change to
Edmond's sketch: **the records are unlinked at wake by the waker, through its pointer, not by the
frame after `suspend()` returns.** The frame only links. Four results follow.

1. **The waker shrinks from 120 B to 40 B** (`error`, `result`, `wait`, `wait_count`); `async_coroutine_t`
   goes from 376 B (allocation 360, bin 384) to 296 B (allocation 280, **bin 320**), computed. The record is
   the fork's edge struct, 40 B, unchanged, on the waiting frame's C stack.
2. **No event method survives.** The event header is `flags`, `ref_count|object_offset`, `callbacks`: 24 B.
   What the deadlock dump and the S7 walk need per type moves to the record: a `kind` pointer to a static
   descriptor `{info, unlink, completers}` chosen by the waiting frame, which knows its target's type. This
   is Edmond's awaiting-info direction (section 8): the record *is* the RFC awaiting-info registration, at
   zero extra instructions per wait.
3. **Bailout is closed by an invariant, not by a `zend_try` per wait.** Records are linked only while the
   coroutine is parked or inside `suspend()` before its switch; every way out of that window unlinks first
   while the records are still intact (sites U1-U6, section 3). A `zend_try` costs 59 instructions:u
   (measured, section 10) and is paid only on the rare branches where `suspend()` runs foreign code on the
   suspender's stack.
4. **Allocations fall** where the reference allocates per wait: blocking channel send/recv −1
   (`channel.c:692`), pool wait −1 (`pool.c:453`), `await_*` over N array items −(N + 1) for N ≤ 8 (context
   `async_API.c:951` and one callback per item `:1002`), `delay()` −1 once S4 puts the Timer op in the frame.
   `spawn`, `suspend`, `await` on a coroutine: equal (0 per op in both).

Why unlink at wake and not after resume (the sketch's `del` after `suspend()`): the waker-driven unlink is
needed anyway for bailout, a destroyed coroutine and cancellation (U1, U4-U6), so a second site in the frame
duplicates it; the reference unlinks there (`coroutine.c:860-866`, which Edmond trusts); the notifier holds the
target's vector in cache at that moment; and a record that stays linked after the wake receives every later
firing of its target until the waiter runs (section 5.6). The frame-side `del` would be correct only with the
extra guard and teardown detach listed in 5.6; it saves nothing.

## 2. Layouts (computed by `scratchpad/frame/probe.c`, x86-64, RFC headers)

```c
/* Vector element base: fork F:830-834, 24 B, unchanged size */
struct _async_event_callback_s {
	uint32_t ref_count;                          /*  0 4  unused by frame records */
	uint32_t flags;                              /*  4 4  ASYNC_CALLBACK_F_RECORD (bit 0), F_COUNTED (bit 1) */
	async_event_callback_fn callback;            /*  8 8  wake function: resolve, cancel, timeout, await_* item */
	union {
		async_event_callback_dispose_fn dispose; /* 16 8  heap subscribers (finish handlers, signal callbacks) */
		const async_wait_kind_t *kind;           /* 16 8  frame records (F_RECORD) */
	};
};

/* The wait record: fork zend_coroutine_event_callback_t F:862-868, 40 B, lives in the waiting frame */
typedef struct {
	async_event_callback_t base;                 /*  0 24 */
	async_coroutine_t *coroutine;                /* 24  8  the waiter */
	async_awaitable_t *event;                    /* 32  8  the target; no reference owned (invariant F) */
} async_coroutine_event_callback_t;

/* One per wait kind, const, .rodata, 24 B */
typedef struct _async_wait_kind_s {
	zend_coroutine_awaiting_info_fn info;        /* R:68 signature; data = the record */
	void (*unlink)(async_coroutine_event_callback_t *rec);  /* NULL: plain removal from target->callbacks */
	void (*completers)(async_coroutine_event_callback_t *rec, void *walker); /* S7; NULL until S7 */
} async_wait_kind_t;

/* Waker: 40 B (SF 2.4: 120 B) */
typedef struct {
	zend_object *error;                          /*  0  8  R:248-254 delivery channel */
	zval result;                                 /*  8 16  written only while UNDEF (5.6) */
	async_coroutine_event_callback_t *wait;      /* 24  8  first record of the current wait; NULL = none linked */
	uint32_t wait_count;                         /* 32  4  (+4 pad) */
} async_waker_t;

/* Event header: 24 B (SF 2.5: 32 B with the methods pointer) */
struct _async_event_s {
	uint32_t flags;                              /*  0 4  bit 31 = 1 */
	union { uint32_t ref_count; uint32_t object_offset; };  /* 4 4 */
	async_callbacks_vector_t callbacks;          /*  8 16  SF 2.4 vector, single-inline */
};

struct _async_coroutine_s {                      /* 296 B; allocation 280 (sizeof − 16, zend_objects_API.h:93-96);
	                                                bin 320 (zend_alloc_sizes.h:48); SF: 376 / 360 / bin 384 */
	zend_coroutine_t coroutine;                  /*   0 144 */
	async_fiber_context_t *fiber_context;        /* 144   8 */
	async_callbacks_vector_t callbacks;          /* 152  16 */
	async_waker_t waker;                         /* 168  40 */
	async_scope_t *scope;                        /* 208   8 */
	zend_object *deferred_cancellation;          /* 216   8 */
	HashTable *finally_handlers;                 /* 224   8 */
	async_coroutine_switch_handlers_vector_t *switch_handlers; /* 232 8 */
	zend_object std;                             /* 240  56 */
};
```

Stack cost of a wait (computed): `await($x, $cancellation)` = 2 records = 80 B. An `await_*` frame with 8
inline items, a cancellation and a timeout record, item zvals and keys = 704 B. The default fiber C stack is
2 MiB (`ZEND_FIBER_DEFAULT_C_STACK_SIZE`, `zend_fibers.h:28`); the reference already puts
`ZEND_FIBER_VM_STACK_SIZE` bytes of VM stack on it (`scheduler.c:1796`).

`O6` (inline `zend_fcall_t`, 104 B) now fits the 384 bin (296 + 104 = 400 B, allocation 384), where in SF it
needed 512 (computed, not part of this proposal; D17 still decides it by B1).

Event flag bits: 5 `F_EXTERNAL` and 12 `F_COMPLETION` as PO section 5; bit 8 `ASYNC_EVENT_F_ARM_ON_WAIT`
(free since SF 2.5). No type code in the header: section 7.

## 3. Protocol

### 3.1 Invariants

- **W.** `waker.wait != NULL` only while the coroutine is parked (SUSPENDED, or QUEUED inside the
  reference's deferral window, 5.6) or is the current coroutine between its first link and its switch out
  inside `suspend()`. In both states the frame that holds the records is alive and unmodified.
- **F.** A linked record's target outlives the link because the waiting frame keeps it alive: the call
  argument (`Async\await`, `Future::await`, `$this` of a channel or pool method), a zval copy per item in
  the `await_*` frame (one `Z_ADDREF` per item, the same count as the reference's owned reference per edge,
  Fc:1240-1241), or typed ownership of a classless event (the frame created it or holds its owner). The RFC
  await slot takes a reference for the wait, as ts does (ts:542, 553, 558).
- **L.** Linking never allocates and never calls foreign code. Everything that can (vector growth, arming a
  timer, submitting an op, the completed-target check) runs before the first link.

### 3.2 Wait sequence

```c
/* await($x, $cancellation), simplified; c = current coroutine */
async_coroutine_event_callback_t rec[2];                 /* 80 B on c's stack */
if (completed(x)) return read_completion(x);             /* phase 0: closed target, fired cancellation */
reserve(x); if (cancel) reserve(cancel);                 /* phase 1: may allocate, arm ARM_ON_WAIT, throw */
link(&rec[0], x, resolve, kind_of(x));                   /* phase 2: stores + push into reserved capacity */
if (cancel) link(&rec[1], cancel, on_cancel, kind_of(cancel));
c->waker.wait = rec; c->waker.wait_count = n;
if (!suspend()) return FAILURE;                          /* records already unlinked by whoever woke c */
return read_result(x);                                   /* coroutine target: x->result in place */
```

`kind_of(obj)` reads the descriptor from the object's handlers table (`obj->handlers` is already loaded for
`handlers->offset`, SF 2.6): each awaitable class gets `struct { zend_object_handlers std; const
async_wait_kind_t *kind; }`. Assumption, not checked: every awaitable class has its own handlers table in the
extension (true for the S3 Coroutine class).

### 3.3 Unlink: one function, six call sites

`async_wait_unlink(c)`: for each of `c->waker.wait_count` records, if `rec->event != NULL`: plain kind → remove
from the target's `callbacks` (the SF 2.4 removal and cursor rule; scan as Fc:1665-1681, the cursor slot
`cur−1` checked first, so self-removal during a notify is O(1)); typed kind → `kind->unlink(rec)`; then
`ARM_ON_WAIT` disarm when the target's length reaches 0, `F_COUNTED` decrement. Finally `wait = NULL`. It is
idempotent, allocates nothing and calls no PHP code (the disarm is a synchronous timer cancel, PC 11).

| Site | When | Records intact because |
|---|---|---|
| U1 | `enqueue` of a SUSPENDED coroutine (the `coroutine.c:860-866` position; deferred through `resumed_coroutines` in scheduler context as the reference, or immediate, option O-F1) | c is parked |
| U2 | short path: c is current and woken in scheduler context (`coroutine.c:840-847`) | c is inside `suspend()`, below the records' frame |
| U3 | link phase fails with an exception (no bailout): the frame unlinks what it linked | synchronous, own frame |
| U4 | `suspend()` returns with the BAILOUT transfer (`scheduler.c:1718-1722`, `fiber_switch_context_ex` `:353-355`): c unlinks itself, then `zend_bailout()` | `suspend()`'s frame lies below the records' frame; nothing has unwound yet |
| U5 | a bailout raised by foreign code that `suspend()` runs on c's own stack before the switch: microtasks, the reactor tick, finalize of a cancelled-before-start coroutine run in place (`scheduler.c:510-517`), switch handlers `leave`, deadlock resolution (`scheduler.c:1519-1612`). Each branch is wrapped in `zend_try`; the catch calls `async_wait_unlink(c)` and re-raises | the catch is in `suspend()`'s frame, below the records |
| U6 | a parked coroutine's fiber context is freed without a switch into it (shutdown after a failed `bailout_all_coroutines`, a dropped coroutine whose stack is never resumed) | the stack is still mapped; unlink before `free` |

Debug asserts: `waker.wait == NULL` at finish, at the coroutine's catch (`coroutine.c:546-551` position,
which today calls `ZEND_ASYNC_WAKER_DESTROY`), before a context returns to the pool, and in an event's
`callbacks` teardown for every element with `F_RECORD` (Fc:1752-1790 position).

**Why no `zend_try` per wait.** A bailout can cross the records' frame only from code running on the same
stack while records are linked. By L, phase 2 runs no such code. After the switch the coroutine is parked
and its stack frozen, so whoever acts on it (enqueue, bailout switch-in, context free) reaches intact
records (U1, U4, U6). The only foreign code on the stack inside the window is `suspend()`'s own tick (U5).
The catch site of a coroutine (`coroutine.c:546-551`; RFC `zend_fiber_coroutine_entry` Rf:805-828) lies
above the records' frame in the stack, so code after the longjmp reuses that memory: it must never read a
record, and by W it finds `wait == NULL`.

## 4. Every wait

Treatment: "records" = frame records of section 3; "zero-record park" = `suspend()` with `wait == NULL`.

| Wait | Reference / fork site | Frame model | Kind | Allocations per op vs reference |
|---|---|---|---|---|
| `await($coroutine)` | `async.c:300-381` | 1 record into the target's `callbacks`; result read in place | COROUTINE | 0 = 0 |
| `await($future)`, `Future::await` | `async.c:300-381`, `future.c:1348-1374` | 1 record; completed future: completion block before link (PO rule 3) | FUTURE | 0 = 0 |
| `await($x, $cancellation)` | `async.c:356-364` | 2 records | by class | 0 = 0 |
| `await($x, timeout(ms))` | `async.c:1638-1679` Timeout as a cancellation | record on the Timeout's event; `ARM_ON_WAIT` arms in phase 1 (0→1), disarms in U1 (1→0); record `F_COUNTED` | TIMEOUT | 0 = 0 |
| `Async\suspend()` | `async.c:223-235` | self-enqueue, zero-record park | none | 0 = 0 |
| `delay(ms)` | `async.c:691` → Fc:1303-1325 | S4 Timer op in the frame + 1 record whose `unlink` cancels the op; the fire callback clears `rec->event` before enqueue, so U1 skips it | TIMER | −1 (`libuv_reactor.c:1238`) once S4 lands |
| `await_any/all/first`, `*_of` over an array | `async_API.c:900-1150`, context `:951`, item callbacks `:1002`, waker timeout `:945` | records array in the frame (slot 0 cancellation, 1 timeout, 2.. items), item zvals and keys parallel; N > 8: one array of N + 2 allocated when N is known | AWAIT_ITEM | −(N + 1) for N ≤ 8; −N for N > 8 (one array); trigger and hash growth (Fc:700-760) gone |
| the same over a Traversable | `async_API.c:1060-1110`, items linked by the iterator coroutine (`:618`) | one compound record in the waiter's frame (`wait_count = 1`); its `unlink` walks chunks of records that never move; the iterator coroutine links into chunks | AWAIT_ITER | −N + number of chunks |
| `cancel_on_exit` wait | `async_API.c:780-880` | second wait, records on coroutine targets | COROUTINE | −N |
| `iterate()` | `async.c:1066-1110` | 1 record on the iterator's embedded completion event (PO), then 1 on the scope event | ITERATOR, SCOPE | −1 (`iterator.c:59`) |
| channel send/recv | `channel.c:684-725` | `channel_waiter_t` in the frame, linked into the channel's typed queue; `unlink` = `channel_queue_remove`, which already tolerates "already removed" (`channel.c:721`) | CHANNEL | −1 (`channel.c:692`) |
| pool acquire | `pool.c:450-472` | as channel, plus a Timer op record for the timeout | POOL, TIMER | −1 (`pool.c:453`), −1 timer with S4 |
| scope awaitCompletion / after cancellation | `scope.c:348-370`, `:404-439` | 1 record (its error fci reached by `container_of` on the frame struct) + cancellation record | SCOPE | −1 (`scope.c:410` callback) |
| task group, iterator | `task_group.c:1222-1240`, `:1462-1467`, `:1837-1850` | 1 record; the waiter event of `:1232` sits in the frame | TASK_GROUP | −1 (`task_group.c:145` waiter) |
| thread channel, thread pool | `thread_channel.c:150-181`, `:269-298`; `thread_pool.c:682-686`, `:1289-1314` | records; `thread_channel_receive`'s classless trigger argument (`thread_pool.c:386`) becomes a typed wakeup parameter (PC 0 C) | THREAD_* | 0 = 0 |
| fs watcher, exec | `fs_watcher.c:645-667`, `libuv_reactor.c:4378-4388` | 1 record, `F_COUNTED` | FS, EXEC | 0 = 0 |
| IO through the provider (S6) | fork `main/network_async.c`, `streams.c`, `plain_wrapper.c` (fork only) | RFC ops are caller-allocated in `run()` with an inline deadline (PO section 2); `run()` adds 1 record so cancellation, the dump and S7 see the wait; `unlink` cancels the op | IO | equal to the RFC design |
| `Fiber::start/resume` (caller side) | Rf:939-985 `zend_fiber_await` | zero-record park; the edge is `fiber->caller_coroutine`, owned by the core | none | 0 = 0 |
| `Fiber::suspend` | Rf:887-937 `zend_fiber_coroutine_yield` | zero-record park; woken by enqueue from `resume`/`throw` (Rf:1440, 1477) | none | 0 = 0 |
| fork fiber waits | Ff:905-925, 1002-1017 (`resume_when` on the fiber coroutine's event) | do not exist in the RFC core | — | — |
| GC from a coroutine | Rg:2203-2235, `ZEND_ASYNC_AWAIT` (R:329) | the await slot: 1 record in the slot frame + one reference on the GC coroutine; D13: no wait where switching is blocked | COROUTINE | 0 = 0 |
| GC coroutine waiting for destructors | Rg:2123-2163 | zero-record park; the edge is the finish handler with `waiter` on the destructor coroutine (heap, 56 B, SF 2.4), removed by handle | none | equal |
| fork GC on a scope | fork `zend_gc.c:2185-2189` | does not exist in the RFC core | — | — |
| `zend_try` around suspend in ported code | `thread_pool.c:247-256`, `thread_channel.c:177-194` | still works: the catch frame lies below the records' frame, and U4/U5 unlinked before the longjmp | — | — |

## 5. Hazards

**5.1 Bailout while parked** (`bailout_all_coroutines`, `scheduler.c:949-1015`). Queued coroutines were
unlinked at enqueue (U1); the deferral queue is drained after every tick (`scheduler.c:102-104`), and anything
left is caught by U4. Non-queued started coroutines are switched in with BAILOUT (`:968`, `:991`) and unlink
themselves in U4. Not-started coroutines have no records. Main parks on the OS stack, which stays intact while
parked; U4 applies to it the same way.

**5.2 Bailout while running with records linked.** Phase 1 allocates before any link (L); a bailout there
leaves `wait == NULL`. An armed `ARM_ON_WAIT` timer without its record stays armed until it fires into an
empty vector or the Timeout object is freed: harmless. Phase 2 cannot bail out. Inside `suspend()` before the
switch: U2 or U5. After resume: records are already unlinked, so the code after `suspend()` may allocate
freely. Assumption, not checked: no signal handler raises a bailout asynchronously in C code (the hard
execution timeout path exits the process).

**5.3 Fiber stack destroyed while records are live.** The fiber context pool takes a context only after its
coroutine finished (`scheduler.c:563-660`), when W gives `wait == NULL` (debug assert). A Fiber object dropped
while its coroutine is parked inside an extension wait is cancelled by the core (Rf:770, D3 form) → enqueue →
U1. A stack freed without a switch (shutdown) goes through U6.

**5.4 Coroutine destroyed while waiting.** Its destructor cancels it (enqueue with an error → U1) and the
coroutine unwinds through its own `suspend()`. Where it cannot be resumed (switching blocked, D13/D14, or after
the scheduler stopped), U6 runs before the context is freed. The PHP GC sees no references from records
(they own none), so `get_gc` reports the frames only (R:363), which also removes SF bug 2
(`coroutine.c:310-318`).

**5.5 Event freed while a record points to it.** Excluded by F; the only teardown that ignores refcounts is
the object store free after a bailout, and by then U4-U6 emptied every wait. The teardown of a `callbacks`
vector asserts no `F_RECORD` element in debug builds. A release-build detach is deliberately absent: it
would write into the dead stack it is meant to protect.

**5.6 Waking twice: two targets fire before the waiter runs.** In scheduler context the reference defers the
unlink to `process_resumed_coroutines` (`coroutine.c:861-866`, `scheduler.c:426-436`), so within one reactor
tick a second target of the same wait reaches its record. Today the second `callback_resolve` does
`ZVAL_COPY` over `waker->result` and leaks the first value (Fc:1256-1260; PC 12 confirms it in the reference).
Frame model, default (no departure): keep the deferral; `resolve` writes the result only while
`Z_ISUNDEF(waker.result)` (one compare); errors follow SF 2.8 (a cancellation overrides a plain error). Option
O-F1: unlink at once in scheduler context too. Frame records own no references, so the unlink can no longer
free an event inside its own libuv callback (assumption: that was the deferral's purpose; the history is one
squashed commit, `git log -S resumed_coroutines` prints only `1fdacf8`). O-F1 removes the second firing and
one queue push and pop per wake in scheduler context; it is taken only if B4/B5 measure it no slower.

**5.7 A wake before suspend** (the curl pattern, `scheduler.c:1661-1665`; PO 4.9). A wake that arrives
between link and switch takes U2 (scheduler context) or makes c QUEUED with U1 unlinking at once; `suspend()`
then returns without switching (SF 2.9 fast return). No start loop is needed.

**5.8 The frame-side `del` of the sketch.** If records stayed linked until the frame ran again, three things
would have to be added: the 5.6 guard in every wake callback (not only `resolve`), a detach on target teardown
(the waiter may be QUEUED for a long time), and U4-U6 anyway. Section 1 gives the choice.

## 6. Events without a PHP class

The RFC core has no event type (R:25-28; PO section 2). In php-src fork and php-async, PC section 0 lists
them; under this model each is waited on only by typed code that names its kind:

| Event | Waited on by | Kind and owner in the frame model |
|---|---|---|
| reactor timer | `delay`, `await_*` timeout, pool timeout; callback-only users (scope, channel deadlock timer, fs debounce) | S4 Timer op in the waiting frame (TIMER); callback-only users own theirs |
| trigger (thread wakeups) | thread channel, thread pool, task group `:1462`, fs watcher, remote Future | typed wait (THREAD_*); `thread_pool.c:386` loses its generic path |
| iterator completion | `async.c:1072-1075` | embedded in the iterator (PO), ITERATOR |
| internal scope event | `scope.c:353`, `:404-439`, `iterate`, thread pool task scope | SCOPE; the scope object or the iterator holds it |
| task-group waiter | `task_group.c:1232` (`trans_event`) | the waiter event in the frame, TASK_GROUP |
| pool event | `pool.c:461` | POOL |
| `zend_future_t` before its object | `channel.c:1255`, `task_group.c:161`, `async.c:1441`, `future.c:2398` | wrapped in a Future object before PHP code can await it; FUTURE |
| signal event | owned by the Future extra (`async.c:1255-1270`) | the Future is awaited (FUTURE), the signal event is never a target |
| internal events under a reference-prefix object (Thread, fs, ThreadChannel, Timeout) | object entry points | reached through the object; kind from the class handlers |
| poll, DNS, exec, process, IO | fork core glue and exec | S4/S6 ops in `run()`, IO/EXEC |

Generic code (`await`, `await_*`, a cancellation argument) receives PHP objects only (PC "Holds";
`async.c:302-304`, `:390-642`, `scope.c:306`, `future.c:1348`); `zval_to_event`'s `IS_PTR` branch has no
producer (PO bug 3). So no generic path needs anything from a classless event, and nothing needs a class.

## 7. Event methods: none survives

| Fork method | Fate | Reason |
|---|---|---|
| `add_callback`, `del_callback` | inline functions on `callbacks` | identical code for every type except Future's late subscribe (now phase 0) and remote Future's `observed` (the agreed flag bit) |
| `start`, `stop` | gone; `ARM_ON_WAIT` with a direct call (Timeout only), the owner arms everything else | PC 2 and 7: eager arming adds timer work to calls that never suspend; `ARM_ON_WAIT` keeps the reference's semantics, including restart, and fixes PO bug 1 |
| `replay` | gone | the completion block read in phase 0 (PO rule 3) |
| `dispose` | gone from the event; survives on heap subscribers as `base.dispose` | generic code holds objects (F); classless events are released by their typed owner |
| `info` | moved to the record's `kind->info` | the waiting frame knows the target's type; section 8 |
| `notify_handler` | gone | agreed (timeout becomes a callback) |
| S7 "completers" (new) | `kind->completers` | section 8 |

The per-type behaviour that remains is cold (dump, S7) and reached from the record, not from the event, so
the event carries neither a methods pointer nor a type code: 24 B. One indirect call remains per firing, the
record's `callback`, as in the reference (Fc:1727).

## 8. Awaiting info and S7 through the record (Edmond's question)

**The record is the awaiting-info registration.** R:60-68 describes a registration as `(handler, data)`,
added by whoever suspends, wiped when the coroutine is enqueued, diagnostics only. The record already has
those properties: the suspending frame creates it, U1 removes it at enqueue, and `kind->info` has the R:68
type with `data` = the record. `get_awaiting_info` (R:394) walks `waker.wait` and calls `kind->info(c, rec)`.

| Cost per wait | Record as registration | Separate RFC vector push (ts:387-418 model) |
|---|---|---|
| Registration | one store of `base.kind`, which replaces the reference's `base.dispose` store (Fc:1140, 1147): 0 net | a call through the slot, a duplicate scan (ts:401-405), a 16 B store, `length++` |
| Allocation | 0 | the vector header and its data on a coroutine's first wait (ts:393, 411): +2 per coroutine, which is +2 per op in B1/B4, where every coroutine waits once |
| Wipe at enqueue | part of U1 | `length = 0` (ts:444-449) |

Counted from the code, not measured. The RFC slots stay implemented for foreign callers (no core code calls
`ADD_AWAITING_INFO`: `git grep` at `834811f2d88` finds only the slot plumbing, Ra:506-508, 618-627): a lazily
allocated vector, as ts, wiped in U1, appended by `get_awaiting_info` after the records. Zero-record parks get
a generic description from the coroutine's state: a fiber (`F_FIBER`, `context.status`, D6), the GC coroutine
(the extension allocated it through `gc_new_coroutine`), `suspend()` (QUEUED). The dump is on by default
(`async.c:1722-1723`, `:1746`; `scheduler.c:699-746`), so PC finding A is answered with no per-type method on
the event: the reference's "FutureState(pending)" and "iterator-completion" lines come from the FUTURE and
ITERATOR kinds.

**S7 uses the same record.** The walk is waiter → record → (`rec->event`, `rec->kind`) →
`kind->completers(rec, walker)`, which enumerates whoever can complete that target: for COROUTINE the target
itself; for FUTURE the FutureState holders; for CHANNEL the other side's holders; for SCOPE and ITERATOR their
coroutines. A record with `F_COUNTED` is an external root (timer, IO, signal, thread), which is what S7.2's
"IO and timer waiters not reported" needs. This answers PC finding B without a type tag in the event header.
Core-owned edges (a fiber's `caller_coroutine`, a GC finish handler's `waiter`) are visible from the target
side only; S7 reads them from the fiber and from finish-handler entries in `callbacks`.

## 9. The Critic's findings on passive edges, under this model

| PC item | Status |
|---|---|
| 0 A, dump info per type | answered by `kind->info` (section 8) |
| 0 B, S7 completers need a type tag | answered by `kind->completers`; no header tag |
| 0 C, `thread_channel_receive` with a classless trigger (`thread_pool.c:386`) | still needs the typed wakeup parameter (S10) |
| 0 "lost generic cleanup at destroy" for frames that never resume | U4-U6 |
| 1, `F_EXTERNAL` count leaks | the record's `F_COUNTED` bit: set at link when the target is `F_EXTERNAL` or armed by `ARM_ON_WAIT`; decremented in U1 by the bit; the target is never re-read |
| 1/5, Timeout liveness (`tests/channel/027`, scope/023) | a waited Timeout's record is `F_COUNTED`, so the scheduler's count of external waits is non-zero and deadlock resolution does not run |
| 2/7, eager Timeout cost | `ARM_ON_WAIT` adopted; arming moves from suspend to phase 1 |
| 3, closed Thread without `EXCEPTION_HANDLED` | unchanged by this model; phase 0 must set it (S5/S10) |
| 4, TaskGroup readiness | unchanged (S9) |
| 11, synchronous timer cancel | relied on by U1's disarm and the TIMER unlink; taken from PC (Q:502-530, `php_io_ring.c:1437-1467`), not re-read here |
| 12, result overwrite on a second firing | 5.6 guard; O-F1 removes the case |

## 10. Instructions and allocations per operation versus the reference

Measured today: a `zend_try` (zend.h:275-290 with `SETJMP` = `setjmp`, since `HAVE_SIGSETJMP` is undefined
in the RFC tree's `php_config.h:1446`) costs **59 instructions:u** per execution: callgrind, gcc 13 -O2, a
loop calling a non-inlined function with and without it (8 vs 67 per iteration, identical at 10^6 and 2·10^6
iterations; `_setjmp` saves no signal mask, no `rt_sigprocmask` in `strace -c`). Stand-in code, not PHP.
Everything else below is counted from the code paths, not measured; D2 is decided by B1-B5.

| Operation | Instructions versus the reference | Allocations |
|---|---|---|
| spawn (B1) | 80 B fewer zeroed by `zend_object_alloc`'s memset (240 vs 320 B; reference 488); no waker init | equal count; object bin 320 instead of 384 (SF) / 640 (reference) |
| `suspend()` (B2, B3) | the waker clean after resume (`async.c:234`: dtor check, error, triggered events, filename release, result dtor, hash clean, inline loop) becomes a NULL test of `wait`; no start loop | 0 = 0 |
| `await` on a coroutine (B4) | removed: `WAKER_NEW` indirect call, inline-slot search, `add_callback` indirect, two hash lookups, one hash add, one hash clean, start and stop indirect calls, `del_callback` and `dispose` indirect calls, the owned-reference inc/dec pair, one result copy. Added: the `wait`/`wait_count` stores and the unlink loop (one record). Record init: 5 stores, as the reference's 5 (Fc:1140-1167) | 0 = 0 |
| 1000 waiters on one target (B5) | per waiter: one indirect `callback` (equal), enqueue, unlink with O(1) self-removal at the cursor (the reference also finds index 0 each time after the iterator adjust, Fc:1645-1681) | 0 = 0 |
| `await_*` over N items (S5) | no hash per item, no trigger, no per-item dispose chain | −(N + 1) for N ≤ 8 (`async_API.c:951`, `:1002`), −N above |
| channel send/recv parked (S9) | as await | −1 (`channel.c:692`) |
| `delay(ms)` (S4) | no start/stop indirect calls | −1 (`libuv_reactor.c:1238`) |
| wake in scheduler context | default: equal (deferral kept) + one compare in `resolve`; O-F1: −1 push and pop | 0 = 0 |
| foreign work inside `suspend()` | +59 per executed branch (U5): microtask batch, reactor tick, in-place finalize, switch handlers, deadlock resolution | 0 |

Why not a `zend_try` per wait: +59 instructions on every await. Without a measured instruction count for an
await (S3.md section 11 gives only 76-84 ns for suspend and resume), whether 59 fits the 3 % budget is unknown;
U4/U5 make the question moot for B2 and B4. Assumption, not checked: in B2 and B4 the reactor tick is
throttled (`REACTOR_CHECK_INTERVAL`, `scheduler.c:1557`) and the microtask queue is empty, so U5 is not
executed per operation. If B2/B4 show otherwise, U5's `zend_try` moves to once per tick instead of once per
branch.

## 11. What changes in the plan (not edited here)

- SF 2.4: the waker becomes section 2's 40 B layout; the inline-slot rule, the owned reference per edge and
  `F_STARTED` go; "clean" becomes `async_wait_unlink` with U1-U6. SF 2.5: header 24 B, no methods table. SF 2.9:
  suspend drops "start every edge"; U2, U4, U5 are part of its contract.
- S3: the COROUTINE kind, the await slot, U1-U6 and the debug asserts; tests: a bailout in a microtask while
  the suspender has a linked record (ASAN), `await_all` with a target firing twice in one tick (S5).
- S4: TIMER kind, `F_COUNTED`, `ARM_ON_WAIT`; PO rule 4 (queue destroyed before stack free) is replaced by U6
  for scheduler-owned ops.
- S5: `await_*` frame, FUTURE kind, phase 0 sets `EXCEPTION_HANDLED` (PC 3).
- S6: `run()` registers an IO record. S7: walks records (section 8).
- Measure before adopting: the whole layout on B1-B5 (D2), O-F1 on B4/B5, `await_*` at N ∈ {1, 2, 8, 100,
  10 000} for the inline threshold K = 8.

## 12. Decisions taken here, assumptions, bugs

Decisions (Edmond may reverse any):
- Unlink at wake by the waker, not by the frame after resume (section 1, 5.8).
- The record keeps the fork's name and layout (`async_coroutine_event_callback_t`, D12); "wait record" is its
  role. The new name `async_wait_kind_t` has no fork source (SF open question 2 applies).
- `kind` shares a union with `dispose`, told apart by `F_RECORD`, so the record stays 40 B.
- Records of one wait are contiguous and 40 B each; per-wait extras live in the frame struct around them
  (`container_of`); Traversable `await_*` uses one compound record.
- `kind_of(obj)` through a handlers container rather than a switch on the class entry.
- The reference's scheduler-context deferral is kept by default (no departure); immediate unlink is O-F1.
- `ARM_ON_WAIT` (PC) over eager Timeout (PO 4.4).
- K = 8 inline `await_*` items, unmeasured.

Assumptions, not checked: every awaitable class has its own handlers table; no asynchronous bailout from a
signal handler; the purpose of `resumed_coroutines`; U5 is not executed per operation in B2/B4; the Ring's
timer cancel is synchronous (taken from PC 11).

Bugs (reading, not run): the result overwrite and leak on a second firing in one tick (Fc:1256-1260, in the
reference too; PC 12 found it independently). `pool.c:472`, `async.c:1075`, `scope.c:370`, `:439`,
`task_group.c:1467` park without the `zend_try` that `thread_pool.c:247-256` and `thread_channel.c:177-194`
use, so a bailout leaves their heap waiters in event vectors (PC 0, debug-build leak); under this model U4/U5
unlink them before the longjmp.
