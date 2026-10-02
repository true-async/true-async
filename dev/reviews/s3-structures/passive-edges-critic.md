# Passive edges: Critic findings (2026-10-02)

Saved by the coordinator from the Critic's hand-back (the critic agent has no write tool). Keys: bare =
php-async 1fdacf8, F/Fc = fork Zend/zend_async_API.h/.c, Q = main/io/php_io_queue_poll.c, ring =
main/io/php_io_ring.c. Read, not run.

## Summary
1. Classless events (Edmond's point): seven kinds in php-async without a PHP class or reached without one:
   reactor timer, trigger, iterator completion, internal scope, task-group waiter, pool event, object-less
   zend_future_t. All are armed, released and replayed by typed code; nothing generic needs start, stop, replay
   or dispose for them.
2. Two generic readers need something per type and the report has nothing for them: the deadlock dump (on by
   default: async.c:1723, 1746) and the S7 walk from a wait to whoever can complete it. Both force back `info`
   (or an awaiting-info registration on every wait) plus a type tag. "info = class name" fails for every
   classless target and for Future (its event has no back pointer).
3. One C API waits on a classless event without knowing its type: thread_pool.c:386 passes a trigger as the
   cancellation of thread_channel_receive (thread_channel.c:228).
4. High: F_EXTERNAL liveness has two readings; the counting one needs a per-edge "counted" bit the report
   deletes; Thread, remote Future and signal Future clear F_EXTERNAL before waiters are cleaned -> the count
   leaks, DeadlockError never thrown, process blocks forever.
5. High: Timeout is not F_EXTERNAL: tests/channel/027 (recv(timeout(50)) with nothing else pending) hits
   deadlock resolution instead of OperationCanceledException.
6. High (speed): eager Timeout arms and cancels a timer on every timeout() even when the call never suspends;
   the reference does no timer work there; the report's cost row is false and unmeasured.
7. Recommend the ARM_ON_WAIT fallback as default: reference semantics, fixes bug 1 for free, signal/003 passes.
8. Medium: direct await of a closed Thread (async.c:328-337) sets no EXCEPTION_HANDLED; the Thread's free
   rethrows an exception already caught (libuv_reactor.c:2484-2491).
9. Medium (S9): TaskGroup readiness != CLOSED (task_group.c:367, 725): a settled unsealed group with new tasks
   hands a waiter UNDEF.
10. The rest holds: 24 B header, PHP entry points always receive an object, IS_PTR dead, bugs 1 and 2 real,
    iterator embedding safe.
11. Synchronous timer cancel holds on both queues (php_io_queue_poll.c:502-530, 261-266; php_io_ring.c:1437-1467).
12. Bug 4 misattributed: the reference does not absorb a wake-before-suspend in its start loop; the same result
    overwrite and leak exists in the reference (scheduler.c:1450-1510, Fc:1258).

## 0. Events without a PHP class, generic paths, what they force back

| # | Type | Created at | Waited on through the generic waker by | Generic touches in the reference |
|---|---|---|---|---|
| 1 | reactor timer | libuv_reactor.c:1232-1266 | delay async.c:691 (Fc:1318); await_* timeout async_API.c:945 -> Fc:1318; pool.c:465-470. Callback-only: scope.c:751-783, channel.c:370-376, fs_watcher.c:248-255, pool.c:784-794 | start at suspend (scheduler.c:1147); stop at wake (scheduler.c:1509, Fc:829), at last-callback removal (Fc:1026), at waker destroy (Fc:988); dispose at waker clean (Fc:802 via F:1172-1188) and resume_when errors (Fc:1180, 1232); notify self-ref dispose (Fc:1689/1739); info (scheduler.c:733) |
| 2 | reactor trigger | libuv_reactor.c:4548-4584 | thread_channel.c:150/269; thread_pool.c:384/574 (as a cancellation event, :386), :963, :1289; task_group.c:1462; fs_watcher.c:532; future.c:2100 | start = uv_ref + count (:4495-4505), stop (:4510-4520) at suspend/wake; info; dispose typed |
| 3 | iterator completion | iterator.c:58-71 | async.c:1072-1075 | start/stop no-ops via waker; edge ref released at clean (Fc:1241, 802); info |
| 4 | scope event (object optional) | scope.c:1304-1376; internal scopes async_API.c:1074, iterate, thread pool task_scope | Scope::awaitCompletion scope.c:353; async_scope_await_after_cancellation scope.c:378-441 (thread_pool.c:742); fork zend_gc.c:2185 | edge ADD_REF/RELEASE -> scope_dispose (scope.c:1184-1302); replay via start loop (scheduler.c:1135-1143 -> scope.c:1136-1166); info scope.c:1168 |
| 5 | task-group waiter event | task_group.c:145 | task_group.c:1846 | start/stop/release/info via waker |
| 6 | pool event | pool.c:628 (Pool not Awaitable) | pool.c:461 | start/stop/info via waker |
| 7 | zend_future_t before an object | channel.c:1255, task_group.c:161, async.c:1441, future.c:2398 | wrapped in a Future object before PHP sees it | replay future.c:541-572, info, self-ref dispose |
| 8 | signal event | libuv_reactor.c:1343 | none: owned by the Future extra (async.c:1255-1270) | typed only |
| 9 | Thread / fs / ThreadChannel / Timeout internal events | under a reference-prefix object | object entry points | edge target is the internal event; no back pointer except Timeout's OBJ_REF (async.c:1666) |
| 10 | poll, proxy, io, listen, dns, exec, process, task | libuv_reactor.c:997, 1038, 6142/7979, 7572/7643, 3635/3771, 4328, 2308, 2810 | fork-core glue and exec | replaced by S4/S6 ops; RFC tree has no zend_async_event_t |

Still forced per type under passive edges:
- (A) Deadlock dump, on by default (async.c:1746, 1723; scheduler.c:699-746 prints via php_printf). Types 1-6
  have no class; 7 and 9 have no way back to the object; a Future's event is shared by FutureState and Future.
  Failing case: `$s = new FutureState; await($s->getFuture());` while another coroutine is parked in iterate():
  the reference prints "FutureState(pending)" and "iterator-completion"; passive prints neither. Forces a
  per-type info (type id in the header + a static table) or an awaiting-info registration on every wait
  (R:60-68, 387-394; one vector push per wait on hot typed paths, unmeasured).
- (B) S7 completers (PLAN S7.1 "waiter -> awaitable -> completers"; S7.2 "unreachable Future", "channel
  without senders"): completers of classless targets are typed (a scope's coroutines, an iterator's
  coroutines, a task group's tasks, a Future's FutureState holders). Forces at least a type tag in the header
  (subtype bits 13-30, header stays 24 B) and a switch in S7, or a per-type descriptor pointer (32 B again).
- (C) thread_channel_receive(ch, result, zend_async_event_t *cancellation) (thread_channel.c:228) is called with
  a classless trigger (thread_pool.c:386), relying on the generic start (uv_ref). Fix: a typed
  internal-wakeup parameter instead of an event pointer.
- Not forced: start, stop, replay, dispose. Lost: generic cleanup at coroutine destroy (coroutine.c:201;
  scheduler.c:993, 1347 WAKER_DESTROY) for frames that never resume after a bailout; thread_channel.c:177-194
  and thread_pool.c:1299-1314 already wrap SUSPEND in zend_try for this; pool.c:472, async.c:1075,
  scope.c:370/439, task_group.c:1467 do not (debug-build leak; queue ops covered by rule 4).

## 1. High: F_EXTERNAL has two readings; the natural one leaks the count and hangs
(a) counter incremented at subscribe when the target has F_EXTERNAL, decremented at clean; (b) a scan of parked
edges at idle. (a) needs a per-edge "counted" bit (the target's bit changes during its life: Thread while
running, remote Future while state->trigger != NULL, signal Future until it resolves); the report deletes the
only per-edge bit (F_STARTED). Failing: `$t = spawn_thread(fn() => 1); await($t);` count 1; completion clears
F_EXTERNAL before/during notify (libuv_reactor.c:2349-2351 notifies before CLOSED); clean sees 0, no decrement;
later `await((new FutureState)->getFuture())` should raise DeadlockError (scheduler.c:1982-1985) but blocks in
queue->wait() forever. (b) is O(parked x edges) per idle point, uncosted. Fix: per-edge
ASYNC_CALLBACK_F_COUNTED set at subscribe, decrement by it at clean, never re-read the target.
Also: Timeout is not F_EXTERNAL; tests/channel/027 (`$ch->recv(timeout(50))`, empty unbuffered channel, main
awaits that coroutine): count 0 -> deadlock resolution (scheduler.c:749-757) instead of "Caught
OperationCanceledException" (reference counts the started timer, libuv_reactor.c:1104-1105, 543). Same for
scope/023. Fix: a Timeout carries F_EXTERNAL while armed, counted per edge.

## 2. High (speed): eager Timeout adds timer work where nothing suspends
Reference timeout() only allocates (async.c:1652, libuv_reactor.c:1232-1266); the timer is armed at suspend
(scheduler.c:1147). Early returns do no timer work: completed awaitable (async.c:328-340), fired cancellation
(async.c:342), buffered recv (channel.c:688 only when parking), finished scope (scope.c:323-346),
thread_channel/046:46-47. Eager: a Timer op submit + cancel each time (Ring: submit + async cancel; Poll: heap
insert + remove). A held, unawaited Timeout also fires and builds a TimeoutException with a backtrace. Fix:
ASYNC_EVENT_F_ARM_ON_WAIT default (arm at callbacks 0->1, disarm at 1->0): reference semantics incl. restart,
fixes bug 1 (async.c:1335 del becomes the disarm); signal/003 passes either way; create the TimeoutException
lazily if a completion block is kept. Costs: a failed arm adds an error path to subscribe; arm moves from
suspend to subscribe.

## 3. Medium: closed-target path sets no EXCEPTION_HANDLED -> Thread rethrows a caught exception
Reference sets CONSUMED inside replay (libuv_reactor.c:2536-2545); Thread dispose rethrows an unconsumed
exception (2484-2491). Scenario: `$t = spawn_thread(fn() => throw new Exception("x")); delay(100); try {
await($t); } catch (Exception $e) { echo "caught\n"; } unset($t);` -> "x" re-raised as uncaught. Fix: every
generic consumer of a completion block that delivers or throws its exception sets EXCEPTION_HANDLED.

## 4. Medium (S9, partly speculative): "CLOSED + F_COMPLETION" is not every type's readiness
TaskGroup readiness is all_settled && !has_pending (task_group.c:367, 1788); CLOSED set on every settle
(724-725), never cleared, while an unsealed group accepts spawns (seal check 1430/1478). Scenario: $g settles,
another task spawned, await_any([$g]) -> UNDEF at once while the task runs (reference's replay returns false
and leaks the callback, async_API.c:609/1040). Scope readiness is count-based too (scope.c:344, 400, 1142).
Fix: TaskGroup sets CLOSED only when sealed, or clears it on reopen.

## 5. Low
- Bug 4 misattributed: the start loop replays only CLOSED targets (scheduler.c:1135); wake-before-suspend is
  absorbed by IN_QUEUE (scheduler.c:1450, 1657, 1673); a second target's ZVAL_COPY over waker->result (Fc:1258)
  overwrites and leaks the first, in the reference too; the reference re-arms events of a queued coroutine
  (scheduler.c:1118, 1147).
- "info only with debug_deadlock=1": that is the default.
- Synchronous Timer cancel holds: Poll (Q:502-530, 261-266), Ring (ring.c:1437-1467); Poll destroy resets
  op->queue; the Ring case of a Timeout freed after the queue is not verified.

## Holds
24 B header (4 + 4 + 16); PHP entry points receive an object (async.c:302-304, 390-642; scope.c:306;
future.c:1348); bug 1 (async.c:1484 start; 1334-1338 delete without stop); bug 2 (async.c:1666 sets OBJ_REF;
coroutine.c:314-315 reads it); bug 3 (IS_PTR has no producer); signal/003 with eager Timeout + completion block;
iterator completion embedding; rule 4 (needed for RFC run() ops, H:217); callback dispose on target teardown.
