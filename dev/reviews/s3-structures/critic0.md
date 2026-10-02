# Critic round 0 on the first flags proposal (before the experts)

Proposal attacked: no embedded event; bit 31 of flags 0 = coroutine, 1 = other awaitable; core bits 0-15,
scheduler 16-31; F_STARTED (bit 8) core flag "has run", set at the first switch; F_CANCELLED = requested,
zend_fibers.c:1375 changed; extension bits F_YIELD (queued by own suspend), F_PROTECTED;
deferred_cancellation holds the cancel object until delivery.

Only core readers of ZEND_COROUTINE_* predicates: zend_fibers.c:770 (IS_STARTED), :1375 (IS_CANCELLED),
:1227 (IS_FINISHED), :1365 (IS_FIBER). zend_gc.c, zend_objects_API.c, zend_execute_API.c, main/main.c read none.

1. CRITICAL. Redefining IS_STARTED as "has run" breaks every caller; they mean "handed to the scheduler"
   (zend_fibers.c:770, ts:1451 ts_cancel, ts:1069/1083 ts_bailout_all, ts:1166 drain).
   Scenario A (crash under test_scheduler): A catches its own cancel (ts:1705), then
   (new Fiber(fn() => 1))->start(); F adopted and enqueued, QUEUED never run (zend_fibers.c:1038);
   zend_fiber_await -> ts_suspend refuses (A cancelled, ts:1558) -> start() throws; temp Fiber dies ->
   zend_fiber_release_coroutine nulls extended_data (:767), !IS_STARTED(F) -> no cancel (:770); F still
   queued, switched into, zend_fiber_coroutine_entry reads fiber = NULL (assert :786, NULL deref :791).
   Scenario B (silent): spawn then cancel before run: ts_cancel returns early (ts:1451-1457), f runs fully;
   ts:992 branch dead.
   Fix: two predicates. Keep STATUS != CREATED as e.g. ZEND_COROUTINE_IS_SCHEDULED and move :770, ts:1069,
   1083, 1166, 1451 to it; add F_STARTED/IS_STARTED only for "has executed" (isStarted, ts:1774).
2. HIGH. One flags word, two incompatible layouts: the ported await/waker/callbacks code applies
   ZEND_ASYNC_EVENT_* to the coroutine. Event CLOSED/RESULT_USED/EXC_CAUGHT/ZVAL_RESULT = bits 0-3 = status;
   ZEND_OBJ 4 = F_CANCELLED; NO_FREE_MEMORY 5 = F_MAIN; EXCEPTION_HANDLED 6 = F_FIBER; REFERENCE 7 = F_OBJ_REF;
   OBJ_REF 8 = proposed F_STARTED. Scenario: await($c) on a QUEUED c: async.c:319-320 SET_RESULT_USED /
   SET_EXC_CAUGHT set status to 7; IS_CLOSED true (async.c:328) -> replay path returns NULL, expected 42.
   Also scheduler.c:1135-1147 (CLOSED, ->replay/->start at vtable offsets the coroutine lacks),
   coroutine.c:314 (GC treats F_CANCELLED as ZEND_OBJ), coroutine.c:666-740 (finalize writes event bits).
   The coroutine needs EXCEPTION_HANDLED, EXC_CAUGHT, RESULT_USED, ZVAL_RESULT, BAILOUT
   (scheduler.c:970-987, coroutine.c:1335): allocate them in 16-30 as ASYNC_COROUTINE_F_*; the coroutine's
   waiter API takes async_coroutine_t *, the event API the event type, so the compiler rejects an event
   macro on a coroutine; write down the per-type dispatch list.
3. HIGH from S4. "bit 31 = 0 means coroutine" makes zeroed memory a coroutine: channel.c:841-843,
   task_group.c:397-399 memset then flags = F_ZEND_OBJ; fork timer/poll/signal reached via
   zend_async_event_ref_t whose first word is 0x80 (F:1026-1041, 1075, 1142-1148). Scenario: await(timeout)
   -> timer event flags 0x10 -> read as coroutine -> waiters vector at coroutine offset -> heap corruption.
   Fix: reverse polarity, bit 31 = 1 means coroutine, set in the single coroutine allocator; debug assert
   at each generic entry that the class matches the decoded type.
4. MEDIUM-HIGH. F_STARTED "set at the first switch" misses no-switch paths: in-place run of a new coroutine
   (S3.md:72-74; reference scheduler.c:633-641) -> completed $b->isStarted() false (coroutine/005, 038);
   main coroutine (zend_async_API.c:737-738, ts:1522-1524; reference sets it scheduler.c:1246);
   cancelled-before-run is switched into only to unwind (ts:992; coroutine.c:466-498) and would report
   started (reference sets STARTED after the IGNORED check, coroutine.c:524).
   Fix: "set immediately before the body's first instruction, by whoever runs it"; the core sets it for
   main in zend_async_scheduler_launch.
5. MEDIUM. isSuspended via F_YIELD is wrong: reference isSuspended = waker.status < RESULT && !FINISHED
   (coroutine.c:1618; F:1733, 1796), true for any queued coroutine; info/002:75-79 expects true for five
   just-spawned never-run coroutines. Name clashes with the reference IS_YIELD (fiber did Fiber::suspend;
   deadlock exemption scheduler.c:785, 802; fiber/022).
   Fix: isSuspended = !FINISHED && (SUSPENDED || QUEUED) (passes coroutine/028:111-113, 038, info/002,
   fiber/019), no bit. Fiber exemption: ((zend_fiber *) extended_data)->context.status ==
   ZEND_FIBER_STATUS_SUSPENDED (zend_fibers.c:899, 920, comment :1242). Drop F_YIELD.
6. MEDIUM. One PROTECTED bit cannot represent nested protect(); the reference has the same bug
   (async.c:251, 276, 289-292): the inner protect's exit clears the bit and throws the deferred cancel while
   the outer protect is active. Fix: save was_protected on entry; on exit clear and deliver only if
   !was_protected.
7. LOW. deferred_cancellation has two readings once unprotected (waking a SUSPENDED coroutine enqueues with
   the error, R:248-254; ts_enqueue replaces e1 by e2, ts:1391-1395; reference keeps the first,
   coroutine.c:994-998). Fix: pending = deferred_cancellation != NULL or a cancellation error already pending
   in the waker; a new cancel while either holds is dropped; after the throw it is delivered again.
8. LOW. zend_fibers.c:1375: extended_data == NULL already detects every core force-close (:767 before
   :772; :749); dropping the F_CANCELLED term is enough. Behaviour change: a scheduler-cancelled fiber
   coroutine may call Fiber::suspend() from finally (legacy: FiberError); under ts the message changes
   (ts:1558). Speculative: F_CANCELLED set inside protect() makes C consumers that fail fast on
   IS_CANCELLED abort the operation protect() exists to finish.

Holds: no TrueAsync method becomes unanswerable with fixes 4 and 5: isStarted = F_STARTED; isQueued =
QUEUED; isRunning = RUNNING (reference: STARTED && !FINISHED, no test pins the difference); isSuspended per 5;
isCancelled = F_CANCELLED && FINISHED; isCancellationRequested = F_CANCELLED && !FINISHED (coroutine/005,
006, 028, 029, 038). Bits 16-31 are safe from the core: SET_STATUS masks 0-3 (R:139-141); the core never
assigns flags = on a coroutine (only SET_MAIN/SET_STATUS at zend_async_API.c:737-738).
