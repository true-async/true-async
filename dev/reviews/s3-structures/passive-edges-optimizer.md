# Passive edges against every real use (optimizer, 2026-10-02)

Keys as in `structures-final.md`: bare names = php-async `1fdacf8` (`git log -1` in `/home/user/php-async`
prints `1fdacf8`); F = fork `Zend/zend_async_API.h`, Fc = fork `Zend/zend_async_API.c` (branch
`true-async`, `863f6dd90cf`); R = RFC `Zend/zend_async_API.h` at `834811f2d88`; H = RFC
`main/php_io_hooks.h`, Q = RFC `main/io/php_io_queue_poll.c` (both read with `git show
834811f2d88:<path>` in `/home/user/php-src`). Every line number was read today. Nothing was built or
run except the size probe of section 5 (gcc 13.3, x86-64, stand-in `zval` of 16 B).

## 1. Verdict

The model holds: the new extension needs **no event methods**. The event header becomes `flags`,
the `ref_count`/`object_offset` union and the callbacks vector, 24 B instead of 32 B. Four rules
replace the methods; without any one of them the model breaks at a site named below.

1. **The generic caller owns the PHP object, not the event.** Every generic entry receives an object
   (section 4.1), but for five classes the edge target under the object is an internal refcounted
   event (Future, Timeout, Thread, FileSystemWatcher, ThreadChannel). `await()` relies on its argument
   to keep the object alive; the `await_*` context holds one object reference per subscribed
   item. Neither takes a reference on the event itself.
2. **Arming leaves the wait.** A Timeout arms its timer in `timeout()` and keeps its
   `TimeoutException` as its completion. A Thread starts in its constructor. A reactor op belongs to
   the provider frame that submitted it (`run()`). Liveness and deadlock read a per-instance
   `F_EXTERNAL` bit (PLAN S4.1 "count of parked user waits"), not armed handles.
3. **A completion block replaces `replay`.** Types that deliver a result after completion put
   `{zval result; zend_object *exception}` right after the header and set `F_COMPLETION`.
4. **Shutdown order.** Owner frames that never resume (bailout) cannot cancel their ops, so the IO
   queue is destroyed before coroutine stacks are freed.

Effect, computed and not measured: per await on an event target, two indirect calls and one
refcount inc/dec pair fewer. Allocations are unchanged for spawn, suspend, await and await_any. A
`delay()` and every timed internal wait save one allocation when the op sits in the frame (rule 4).
`iterate()` saves one allocation.

One semantic choice is Edmond's (section 4.4): with an eager Timeout the clock starts at `timeout()`,
and the timer no longer restarts when the last waiter leaves. The fallback keeps the reference
semantics with one flag and a direct call, not a method.

## 2. Generic call sites (the caller does not know the type)

| Site | Method | What it does today | Passive replacement: who, cost |
|---|---|---|---|
| `scheduler.c:1114-1150` (called at `:1681`) | start, replay | At every suspend, starts each waker event; a closed one is replayed (`:1123-1145`, fix #103) | Removed. Arming by the owner (rule 2). A closed target is handled before subscribing, as `async.c:328`, `async_API.c:597`, `:1033` already do. Saves one indirect call per event edge per suspend, plus the loop |
| `scheduler.c:1509`, `:1701`; Fc:819-832; Fc:974 (destroy); Fc:1026 (last callback of a trigger removed) | stop | At wake, on suspend error, at waker destroy | Removed. A Timeout's timer runs until it fires or the object dies. A provider op is cancelled by `run()` after resume. External sources have nothing to stop: the wakeup op is always pending (PLAN S4.1). Saves one indirect call per event edge per wake |
| Fc:802 (`waker_events_dtor`), Fc:816, Fc:1126, Fc:1180, Fc:1232 (`trans_event` error paths) | dispose | `ZEND_ASYNC_EVENT_RELEASE` (F:1172-1188), which calls `dispose` for a non-object event | The generic caller releases the object it holds (`OBJ_RELEASE`). An internal event is released by its typed creator (`delay`, `pool.c:465-470`, the `network_async.c:590-614` pattern becomes the provider frame). For `await()` no reference is taken at all: one inc/dec pair fewer than the edge's owned reference (Fc:1240-1241) |
| Fc:1689, 1696-1739 (`zend_async_callbacks_notify`) | dispose | Self-protection reference around the notify | `GC_ADDREF`/`OBJ_RELEASE` on the object when `F_ZEND_OBJ`. Otherwise the typed fire path, which called notify, holds its own event. Cost neutral |
| Fc:1746 (`notify_and_close`) | stop | Generic stop before close | Typed callers only (`iterator.c:93`, `:195`, `:569`): direct call |
| `async.c:328-335`, `async.c:1404`, `async_API.c:609`, `:1040`, `:1267` | replay / extract | Result of a closed awaitable | Read the completion block (section 5); a coroutine target reads `zend_coroutine_t.result/exception` (R:104-132). The Thread's `CONSUMED` (`libuv_reactor.c:2526-2544`) becomes "`EXCEPTION_HANDLED` was set after close", the header bit the waker callback already writes (Fc:1266). Cost neutral |
| `async.c:1484` | start | `signal()` starts a generic Completable cancellation | Nothing to start; also removes bug 1 (section 7) |
| `scheduler.c:712`, `:733` | info | Deadlock dump, only with `async.debug_deadlock=1` | Class name and handle of the object; an internal target prints the RFC awaiting info its frame registered (R:60-68, 381-394). No test reads the text: the deadlock tests set `async.debug_deadlock=0` (`edge_cases/001:4`, `015:4`) |
| `coroutine.c:310-318` | (event→object) | `get_gc` of the coroutine (bug 2 of `structures-final.md` section 7) | Reports nothing from edges, which own nothing. The owner reports: frames through the RFC execute-data slot (R:363), the `await_*` context through its own walk |
| Fc:1171, Fc:796, Fc:1038-1039 | add/del | Subscription | Plain functions (agreed) |

Fork core only: `zend_fibers.c:784`, `:1344-1371`, `main/network_async.c`, `main/streams/streams.c:489-722`,
`ext/curl/curl_async.c`. The RFC core has no event type: R:25 says "no event";
`git grep -c zend_async_event_t 834811f2d88 -- Zend main ext` prints nothing. Its IO hooks run
synchronously in the provider (`run`, H:217; `php_io_run`, H:249). The op is caller-allocated and
carries its own deadline (H:80-127, `deadline` H:86, `queue` H:123), so no C-created event reaches
PHP code.

## 3. Event types

| Type | Reached generically? | Today (start/stop, dispose, replay, info) | Passive owner and cost |
|---|---|---|---|
| timer (internal) | No: `delay` (`async.c:691` → Fc:1303-1325, transferred), `pool.c:465-470` (transferred), channel deadlock timer (`channel.c:350-366`, typed), fs debounce (typed) | Lazy start at suspend; release by the waker | S4 Timer op owned by the frame. One allocation fewer per `delay` (`libuv_reactor.c:1238` `pecalloc`) when the op sits in the frame |
| poll/IO | No: fork `network_async.c:590-614`, `:809-834` (transferred), `:612` (stream proxy, reused) | start at suspend, stop at wake (`libuv_reactor.c:680-718`) | Op in `run()`. Its inline deadline (H:86) removes the separate timer per timed wait (Fc:1312-1323) |
| DNS | No | start/stop only count (`libuv_reactor.c:3549-3570`, `3685-3705`) | GetAddrInfo op in `run()` |
| exec/process | No: `libuv_reactor.c:4383` (typed wait) | count (`:4113-4140`) | WaitPid op in `run()` |
| signal | Only through the Future `signal()` returns | Typed (`async.c:1424-1514`; started `:1497`) | Typed Future extra (pattern `async.c:1247-1268`); `F_EXTERNAL` on that Future until it resolves |
| fs | Yes: `FileSystemWatcher` is Awaitable (`fs_watcher.c:731`); as await_any item or cancellation | Armed at construct (`fs_watcher.c:569-576`), closed typed (`:333-342`); a waiter's start/stop only moves `loop_ref_count` (`libuv_reactor.c:351-375`) | Nothing to do; `F_EXTERNAL` static |
| thread | Yes: `Thread` is Completable (`thread.stub.php:44`) | First start = spawn (`libuv_reactor.c:2380-2412`, called `async.c:215`); later starts arm (`:2414-2420`), stop disarms (`:2426-2440`); replay `:2513-2547` | Spawn in the constructor (typed). `F_EXTERNAL` while running. Completion block; `CONSUMED` as in section 2 |
| Future | Yes | start/stop no-op (`future.c:467-471`, `518-521`); a late add fires inline (`:527-530`); replay `:541-571`. The event is internal and refcounted (`:667`), shared by the FutureState and Future objects (`:703`, `:1157`) | Completion block: the fork's `zend_future_t` already has this layout (F:1919-1922). Generic code holds the Future object |
| remote Future | Yes | start/stop proxy the trigger (`future.c:2253-2278`); `observed` set by add (`:2288-2300`), read in dispose (`:2310-2321`) | `F_EXTERNAL` while `state->trigger != NULL`; `observed` = the agreed subscriber bit |
| channel | Yes (Awaitable, `channel.c:1353`) | start/stop no-op (`channel.c:815-823`); info debug (`:825-836`); waits typed (`:692-715`) | No `F_COMPLETION`: a closed channel as an await_any item is ignored, as today (`async_API.c:600-603`) |
| scope | No: only a cancellation is generic (`scope.c:306`) | start/stop no-op (`scope.c:1113-1123`); replay `:1137-1167` reached only through the generic start loop; waits typed and check completion first (`:344-346`, `:400-403`); refcounted, not `ZEND_OBJ` (`:1342`) | Typed; the scope object holds the event |
| task group | Yes (Awaitable, `task_group.c:1913`, `:1940`) | start/stop/dispose no-op (`task_group.c:345-358`); replay delivers UNDEF (`:360-381`) | `F_COMPLETION` with a block left UNDEF/NULL (24 B per group) |
| pool | No: `Pool` is not Awaitable (stub list) | Typed waits (`pool.c:450-470`) | Typed; its timer becomes an op in the frame |
| iterator completion | No: created `iterator.c:57-71` (`ecalloc`), awaited only by `async.c:1072-1075` | Generic start/stop/dispose | Embedded in the iterator, which outlives the wait (`async.c:1066`, microtask reference): one allocation fewer per `iterate()` |
| timeout | Yes (Completable) | Reference object over a timer (`async.c:1638-1679`), `OBJ_REF` (`:1666`), notify handler (`:1622-1636`), dispose chain (`:1601-1620`); not armed at creation (`libuv_reactor.c:1232-1268` inits, no start), armed at the first await | Eager arm at `timeout()`; the completion block holds the `TimeoutException`; `free_obj` cancels the op. Fallback in 4.4 |
| thread channel | Yes (Awaitable, `thread_channel.c:688`); the object resolves to its trigger (`thread_channel.h:61`, `:74`) | Trigger start/stop = `uv_ref` and count | `F_EXTERNAL` static; no completion |
| coroutine (not an event) | Yes | Type bit; replay `coroutine.c:1040-1069` | `zend_coroutine_t.result/exception` (R:104-132) |

## 4. Cases that could break the model

**4.1 A generic target that is not a PHP object.** Every entry takes an object:
`async.c:302-304` (Completable), `:390`, `:440`, `:497`, `:537`, `:588`, `:632` (Awaitable),
`async_API.c:310`, `scope.c:306`. One exception exists: `zval_to_event` accepts an `IS_PTR` raw event
(`async_API.c:312-313`). Its callers are `async.c:397-642` (PHP iterables) and `thread_pool.c:2071`
(an array of Future objects); no caller passes `IS_PTR`, so the branch is dead and goes. The
five reference-prefix classes resolve to internal events, which is why rule 1 names the object.

**4.2 Lazy start and loop liveness.** In the reference an unawaited Timeout never arms, and the
loop runs while `REACTOR_LOOP_ALIVE()` (`scheduler.c:2017-2018`). With an eager timer, S4.1 must
state that a pending op no coroutine waits on keeps nothing alive (the existing `HIDDEN` bit 10).
Otherwise an unawaited `timeout(60000)` holds the process until it fires. Test evidence:
`signal/003:14-15` creates `timeout(1)`, waits `delay(50)`, then passes the Timeout. Under eager
arming the Timeout is closed by then; without a stored exception `async.c:1404-1411` rejects with
"Signal wait cancelled" instead of the expected `TimeoutException`. With the completion block the
test passes. Computed by reading, not run.

**4.3 One event shared by several waiters.** `loop_ref_count` counts starts
(`libuv_reactor.c:351-375`). Passive: a Timeout has one timer however many wait; Thread, remote
Future, ThreadChannel and fs have nothing to arm.

**4.4 Stop at wake when the waiter set empties.** The reference stops a Timeout's timer when its
last waiter leaves and restarts it with the full timeout at the next await (`libuv_timer_start`,
`libuv_reactor.c:1075-1105`). An eager timer keeps running. Three tests store a Timeout
(`signal/003`, `common/timeout_class_methods`, `await/002`); none awaits it twice, and the stub says
"trips after $ms" (`async.stub.php:183`). **Fallback with reference semantics:** bit
`ASYNC_EVENT_F_ARM_ON_WAIT`; the generic subscribe calls `async_timeout_arm()` when the target's
`callbacks.length` goes 0 → 1, and unsubscribe calls `async_timeout_disarm()` at 1 → 0. That is one
flag test per subscribe and remove, on the header line already loaded; Timeout is the only type
that needs it. Recommended: eager (no flag, no hook).

**4.5 Events created by C code that PHP then awaits.** The fork has one: the Future `signal()`
returns; its internal signal event is typed. The RFC core has none (section 2). A background op
that resolves a returned Future (signal, a thread-pool result) is the Future's typed extra, and
it sets `F_EXTERNAL` until the Future resolves.

**4.6 The S7 collector.** Edges give the target. The object is `base + object_offset` for an
embedded event. A reference-backed internal event has no back pointer, except the Timeout through
`OBJ_REF` (`async.c:1666`). The new Timeout, Thread and Future embed their event where lifetime
allows. Completers cannot be walked: a signal callback keeps its Future in an opaque field
(`async.c:1232-1240`). The per-instance `F_EXTERNAL` bit tells S7 "an outside source completes
this", which is what S7.2 needs for "IO and timer waiters not reported".

**4.7 Replay of a completed awaitable through await_any/await_all.** `async_API.c:597-614` and
`:1033-1046` deliver through the context callback. Passive: the same callback reads the
completion block. The `in_scheduler_context` bracket (`:1036-1039`) stays, so the resume takes the
short path.

**4.8 An owner frame that never resumes.** At bailout the reference stops and releases
generically (`scheduler.c:993`, `WAKER_DESTROY`). The Poll queue's destroy cancels outstanding ops
and reads their memory (Q:717-722). An op in a frame therefore requires queue destroy before stack
free (rule 4). Waiters linked into extension structures stay on the heap, as now (`channel.c:692`,
`pool.c:453`).

**4.9 A wake before suspend** (the curl pattern, `scheduler.c:1661-1665`). Today the start loop's
replay absorbs it. `structures-final.md` 2.9 treats an enqueue of the RUNNING current coroutine
outside scheduler context as a yield and does not clean its edges. Without the start loop that
path must clean the edges, or a second target overwrites `waker.result` (Fc:1258). Reading.

## 5. Final layouts (computed by the probe; `scratchpad/passive/probe.c`)

```c
struct _async_event_s {                          /* 24 B (32 B with the methods pointer) */
	uint32_t flags;                              /*  0 4  bit 31 = 1 */
	union { uint32_t ref_count; uint32_t object_offset; };  /* 4 4  F_ZEND_OBJ selects */
	async_callbacks_vector_t callbacks;          /*  8 16 */
};
typedef struct {                                 /* 48 B: types with F_COMPLETION */
	async_event_t base;                          /*  0 24 */
	zval result;                                 /* 24 16 */
	zend_object *exception;                      /* 40  8 */
} async_completion_event_t;

struct _async_event_callback_s {                 /* 24 B, unchanged */
	uint32_t ref_count;                          /*  0 4 */
	uint32_t flags;                              /*  4 4  F_EDGE only; F_STARTED goes */
	async_event_callback_fn callback;            /*  8 8  per subscriber: resolve, cancel, timeout, await context */
	async_event_callback_dispose_fn dispose;     /* 16 8  target teardown detaches the subscriber */
};
typedef struct {                                 /* 40 B, unchanged: one passive edge */
	async_event_callback_t base;                 /*  0 24 */
	async_coroutine_t *coroutine;                /* 24  8  the waiter */
	async_awaitable_t *event;                    /* 32  8  the target; no reference owned */
} async_coroutine_event_callback_t;
/* async_waker_t 120 B unchanged: error 0, result 8, inline_callbacks[2] 24, callbacks 104 */
```

Event flag bits added: 5 `ASYNC_EVENT_F_EXTERNAL` (free since the `NO_FREE_MEMORY` cut), 12
`ASYNC_EVENT_F_COMPLETION` (spare). Bit 8 stays free. The `structures-final.md` 2.5 reason for that
is wrong (section 7, item 2); the decision holds because the new Timeout embeds its event.

## 6. Remaining methods

None on the event. Two function pointers survive on the **callback**, for per-subscriber reasons:
`callback`, because one target has subscribers that do different things (Fc:1250 resolve, Fc:1272
cancel, Fc:1288 timeout, `async_API.c:560` await context, `async.c:1284-1345` signal); and `dispose`,
because the target's teardown must detach each subscriber (Fc:1752-1790; the edge clears
`edge->event`). Neither depends on the event type.

## 7. Speed and allocations per operation (computed from the removed code; not measured)

| Operation | Instructions | Allocations |
|---|---|---|
| spawn (B1) | unchanged: no event code on the path | unchanged |
| suspend with no edges (B2/B3) | the start loop over 2 inline slots and the overflow length goes | unchanged |
| await, one event target (B4) | −1 indirect start, −1 indirect stop, −2 `F_STARTED` writes, −1 refcount inc/dec pair (the argument holds the object) | unchanged |
| await, coroutine target | −1 refcount pair (start/stop were already skipped by the type bit) | unchanged |
| await_any / await_all over N event targets (B5, S5) | −N start and −N stop indirect calls; refcount pairs move from the edge to the context (neutral) | unchanged |
| `timeout()` + await | same timer insert and remove as the reference, done at creation and release instead of suspend and wake | Timeout object with embedded event: 1 instead of 2 (`async.c:1640` + `:1652`), if the queue cancels a Timer op synchronously (assumption) |
| `delay()`, timed internal waits | as the reference minus the two indirect calls | −1 per call with the op in the frame (rule 4) |
| `iterate()` | — | −1 (`iterator.c:59`) |
| per event | header 32 → 24 B | the bin effect per type is unknown until the S4/S5 layouts exist |

The `structures-final.md` section 5 row "Methods table per event type" and open question 4 drop.

## 8. Edits this implies elsewhere (not made here)

`structures-final.md`: 2.1 debug assert "`methods != NULL`" becomes bit 31 plus a class check; 2.4
"start/stop once per edge, `F_STARTED`, one owned reference per edge" becomes "passive, the caller
owns the object"; 2.5 header and methods table become section 5 here; 2.6 drops `start`, `stop`,
`replay`, `info`, `dispose` dispatch; 2.9 suspend drops "start every edge". PLAN S4.1: the HIDDEN
rule for ops nobody waits on, queue destroy before stack free, `F_EXTERNAL`. S5.1: the completion
block, eager Timeout. S7.1: `F_EXTERNAL` as the external-completer root.

## 9. Bugs found (reading, not run)

1. `Async\signal(…, $t)` starts the cancellation generically (`async.c:1484`). When the signal
   arrives, the cancel callback is removed (`async.c:1336-1340`) without a stop. A held Timeout
   keeps its timer armed, and the reactor alive (`scheduler.c:2017-2018`), until the timer fires or
   `$t` is released.
2. `structures-final.md` 2.5 says no reference site sets or reads `OBJ_REF` and that `async.c:1666`
   does not exist. At `1fdacf8`, `async.c:1666` sets it (`git show 1fdacf8:async.c`) and
   `ZEND_ASYNC_EVENT_TO_OBJECT` (F:1151-1154) reads it at `coroutine.c:315`.
3. `zval_to_event` keeps a dead `IS_PTR` branch (`async_API.c:312-313`; no producer, section 4.1).
4. Wake before suspend without an edge clean (section 4.9), in the final design.

## 10. Decisions taken here and assumptions

- Eager Timeout recommended over the `ARM_ON_WAIT` fallback: a semantic change (4.4).
- The completion block is mandatory for `F_COMPLETION`; the task group pays 24 B instead of a
  second bit.
- `F_EXTERNAL` per instance, because one class (Future) is completed locally or from outside.
- `ref_count` stays in the callback base: dropping it saves no bytes (alignment of `callback`).
- Assumptions, not checked: the Poll queue cancels a Timer op synchronously (Q:703-704 states it
  for readiness ops only); S4 can exit with hidden pending ops; fiber stacks can be freed after the
  queue at bailout. The instruction effects are counts of removed calls, not measurements; D2
  is decided by B1-B5.
