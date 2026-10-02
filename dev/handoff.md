# Handoff

Where the work stopped and what the next session needs. Replaced whole at every stop; the plan
(`dev/PLAN.md`) outranks this file when they differ.

Written 2026-10-02. Active step: **S3.10** (not started); S3.9 (fibers) closed. S3.15-S3.18
(health check) run after S3.14 (Edmond).

## State

- Core pinned: `async-core-io-2026-10-02-2` (`82df2fc6ccc`). CI gates every lane on every list; a
  test that cannot pass yet carries `--XFAIL--` naming its step, and the commit that makes it pass
  removes the section. run-tests (`tools/run-tests.patch`) fails a test the timeout killed.
- S3.3-S3.6a: internal API, classes, the `Coroutine` object, the 21 slots, the FIFO run queue,
  pooled contexts, main adopted as a copy, the suspend slot with the tick, the hybrid scheduler
  coroutine and `scheduler_bailout_all` (main last, the core's test_scheduler.c order).
- S3.7: `Async\await()`, the await slot (one record in the waiter's waker), the GC waiting for its
  run, `getAwaitingInfo()`.
- S3.8: `async_coroutine_cancel` (`src/scheduler.c`, TrueAsync's `coroutine.c:900-1003`),
  `Async\protect()`, `Async\graceful_shutdown()` (once per request, `ASYNC_G(graceful_shutdown)`),
  `async_scheduler_exit_with` (an exit exception starts the shutdown or re-cancels after it),
  deadlock resolution (`scheduler_resolve_deadlock`, the report written once), unrun cancelled
  coroutines finished at pop (`coroutine_finish_unrun`), TrueAsync's waker error rules. The
  coroutine's values go in `dtor_obj` (`coroutine_object_destroy`), which throws an unobserved
  outcome; the observed mark is set when a waiter reads the outcome. Test hook
  `TrueAsync\Test\enqueue_with_error`. S3.md section 6 "As built" has the details.
- S3.9: every Fiber runs as a coroutine (`scheduler_intercept_fiber`); suspended fibers alone are
  closed with a graceful exit, no `DeadlockError`; the waker never chains an exit object; both
  context entries clear `EG(active_fiber)`; `extended_dispose` runs in `free_obj` only. S3.md
  section 8 "As built".
- Reviews: after the code, Critic and the Sage (`general-purpose`, model `fable`) compare it with
  TrueAsync (`/root/php-async` in the container) and hunt inventions; one plan step is one commit.
- Container notes: the ASAN lane needs `TRUE_ASYNC_CORE_SRC=/root/core-asan`; `gen_stub.php`
  cannot fetch PHP-Parser through the proxy, so `src/true_async_arginfo.h` was edited by hand
  (the stub hash is the stub's sha1); local `clang-format-18` (18.1.3) flags lines CI accepts, so
  check only changed lines (`git clang-format-18 --diff HEAD`).

## Later steps

- Core gaps found in S3.9 (test_scheduler.c has them too): its two context entries do not clear
  `EG(active_fiber)`; a Fiber whose coroutine was cancelled before it ran stays INIT, and a second
  `start()` overwrites `fiber->coroutine` and leaks the first; `resume()`/`throw()` refused by the
  park (a finished coroutine's release) leave the body queued with no caller. A core-side fix
  belongs on `async-core`.
- Legacy core path, async off: a Fiber that calls `Fiber::suspend()` from a destructor while an
  exception unwinds gets the graceful exit at shutdown with that exception chained under it, a
  "Creation of dynamic property GracefulExit::$previous" deprecation
  (`tests/internal/023-fiber_methods_in_tick.phpt` run with `-n` and no extension); not checked on
  upstream master.
- A kept Fiber closed by the exemption reports "The fiber threw an exception" from `getReturn()`
  (the core sets THREW for a graceful exit while the Fiber lives; Sage, no action).

- An exception pending when `async_await_coroutine` is entered makes the GC report 0 and defer
  after a completed wait (inherited from test_scheduler.c's `ts_await`; Critic in S3.7, minor);
  not touched in S3.8.
- `scheduler_cancel` ignores `is_safely` until the zombie state (S9).
- S3.10: `scheduler_cancel_all` also cancels the core's internal coroutines (the GC coroutine, the
  shutdown destructor iterators); a cancelled-before-run iterator reports "was not finished
  properly" and marks every object destructed (Critic, latent until the switch handlers run).
- Two coroutines that catch the deadlock's cancellation and await each other again loop, each
  round adding a `DeadlockError`; TrueAsync does the same (Critic, minor).
- D16's 5 s deadline after `exit()` needs a clock and a reactor timeout (S4).
- S3.10: a bailout inside the scheduler's catch; a scheduler parked at RSHUTDOWN.
- S5: a wait for several targets needs more than the one record in the waker (TrueAsync: two
  inline callbacks and a heap array). A multi-shot record (S4+) needs its own teardown rule.
- Spec gaps the test author named (S3.7): `await()` with `null`, an array or a second argument;
  the order woken waiters run in against coroutines already queued.
- Observers: every re-mint of main notifies a switch into a new context copy (as ts.c).
- Next core update: `ZEND_ASYNC_DEACTIVATE` also clears `in_scheduler_context`.
- Nested notifies recurse with no depth limit (S3.md 3.6 "As built").

## Next

1. S3.10.
