# Handoff

Where the work stopped and what the next session needs. Replaced whole at every stop; the plan
(`dev/PLAN.md`) outranks this file when they differ.

Written 2026-10-02. Active step: **S3.8** (not started); S3.7 (await and GC) closed. S3.15-S3.18
(health check) run after S3.14 (Edmond).

## State

- Core pinned: `async-core-io-2026-10-02-2` (`82df2fc6ccc`). CI gates every lane on every list; a
  test that cannot pass yet carries `--XFAIL--` naming its step, and the commit that makes it pass
  removes the section. run-tests (`tools/run-tests.patch`) fails a test the timeout killed.
- S3.3-S3.6a: internal API, classes, the `Coroutine` object, the 21 slots, the FIFO run queue,
  pooled contexts, main adopted as a copy, the suspend slot with the tick, the hybrid scheduler
  coroutine and `scheduler_bailout_all` (main last, the core's test_scheduler.c order).
- S3.7: `Async\await()` (`src/true_async.c`), `async_await_coroutine` and the await slot
  (`src/scheduler.c`): one record in the waiter's waker (`waker.record`, linked while `event` is
  set), pushed into the target's callbacks; the finish's notify wakes the waiter through
  `await_record_wake` (an enqueue, U1 unlinks). The GC waits for its run in main and in coroutines;
  the slot returns false in scheduler context, inside an unadopted Fiber and while a finished
  coroutine is current (finalize's releases). `getAwaitingInfo()` gives `await: coroutine #N`.
  Coroutine 320 B. The outcome is marked observed when `await()` starts, as TrueAsync does.
- Blind tests: `dev/plans/S3.7-spec.md` and `tests/wait/` (27 files by `test-author`); the check
  (`claude-skills/hooks/blind-tests.py check dev/plans/S3.7-spec.md`) passes. `tools/test.py` adds
  a `file` attribute to run-tests' JUnit report for it. Delete the spec when S3 closes (plan.md
  23.1.3) or keep it until then; never edit the author's files by hand.
- Reviews: after the code, Critic and the Sage (`general-purpose`, model `fable`) compare it with
  TrueAsync (`/root/php-async` in the container) and hunt inventions; one plan step is one commit.

## Obligations for S3.8

- **First, a use-after-free since S3.5 (the Sage, S3.7):** the coroutine object releases its
  result, arguments and exception in `free_obj` (`coroutine_object_free`, `src/coroutine.c`), while
  the finished coroutine is still current. A destructor run there can resurrect the object
  (`$GLOBALS['c'] = Async\current_coroutine();`), and the engine frees the block after `free_obj`
  anyway (`zend_objects_API.c:301-308`). TrueAsync releases them in `dtor_obj`
  (`coroutine_object_destroy`, `coroutine.c:164-242`), where a resurrected object stays alive.
  Move the releases to a `dtor_obj` handler; it reverses DECISIONS "the arguments stay until the
  object dies", so tell Edmond. Also `await($finished)` from such a destructor is refused although
  no park is needed (TrueAsync replays a closed event first).
- A waiter cancelled while it waits: `await()` has already marked the target's outcome observed,
  so the target's exception is lost (Critic, S3.7). Decide with TrueAsync's cancel path.
- An exception pending when `async_await_coroutine` is entered makes the GC report 0 and defer
  after a completed wait (inherited from test_scheduler.c's `ts_await`; Critic, minor).
- The tick's exit exception does not start the graceful shutdown yet: `edge_cases/014` and `015`
  hang until it does. 4.2 step 4 skips FINISHED entries once a queued coroutine can be finalized.
  The rethrow of an unobserved exception from the coroutine's destructor (TrueAsync
  `coroutine.c:219-230`); `scheduler/006`'s fixture; `waker_apply_error` keeps the first of two
  errors, TrueAsync's resume chains them: pick one rule.
- Deadlock resolution replaces the fatal in `scheduler_loop` (TrueAsync `resolve_deadlocks`).

## Later steps

- S3.9: adopting every Fiber removes the unadopted-Fiber refusals of suspend, await and the await
  slot (`scheduler/013`, `scheduler/027`, `gc/013`, `gc/014`).
- S3.10: a bailout inside the scheduler's catch; a scheduler parked at RSHUTDOWN.
- S5: a wait for several targets (await_all/any, a cancellation token, a timeout) needs more than
  the one record in the waker: decide the storage there (TrueAsync: two inline callbacks and a heap
  array). The teardown fires a leftover record after detaching it; a multi-shot record (S4+) needs
  its own rule there.
- Spec gaps the test author named (S3.7): `await()` with `null`, an array or a second argument;
  the order woken waiters run in against coroutines already queued.
- Observers: every re-mint of main notifies a switch into a new context copy (as ts.c).
- Next core update: `ZEND_ASYNC_DEACTIVATE` also clears `in_scheduler_context`.
- Nested notifies recurse with no depth limit (S3.md 3.6 "As built").

## Next

1. S3.8.
