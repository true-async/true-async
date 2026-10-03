# Handoff

Where the work stopped and what the next session needs. Replaced whole at every stop; the plan
(`dev/PLAN.md`) outranks this file when they differ.

Written 2026-10-03. Active step: **S3.15** (not started); S3.14 (security pass) closed. S3.15-S3.18 (health check) run now (Edmond).

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
- S3.10: the core's switch handlers run (leave before the tick, as TrueAsync), so a shutdown
  destructor may wait; the bailout walk survives a bailout of its own; finalize leaves the registry
  as it sets FINISHED; RSHUTDOWN unmaps what a bailout out of the last from_main call leaves (U6).
  Test hook `add_throwing_finish_handler($coroutine, bailout: true)`. S3.md section 7 "As built".
- S3.11: `dev/BENCHMARKS.md`; D2 holds on B1-B5. O6 taken (`spawn_fcall`), the context pool floor
  1024 with TrueAsync's run-queue rule, every context's first VM stack page on its C stack
  (`context_vm_stack_start`; the context's stack is `fiber.stack_size` + 16 KiB), tests
  `scheduler/055`, `056`; the no-stack tests use `fiber.stack_size=64G`, which mmap refuses. Benchmarks: `bench/`, `tools/bench.py --count`
  (cachegrind; the container has no hardware counters) and `--wall`. The reference builds only into
  the fork core's tree (`ext/async` lacks `ZEND_TSRMLS_CACHE_DEFINE`); our in-tree build needs the
  configure header check removed.
- S3.12: `TrueAsync\Test\fail_at('enqueue'|'reserve'|'link')` arms a fault site (a fatal error, as out
  of memory); `internal/027`-`033` take the unlink sites U1-U6. The fuzz hook is TrueAsync's
  (`--enable-true-async-fuzz`, `TRUE_ASYNC_SCHED=random:<seed>`); `tools/test.py --lane L --seeds N`
  builds into `_build/<lane>-fuzz`, fails a seed on a crash, an assertion, a sanitizer or leak report,
  a timeout or a missing test, and lists the tests whose output gained a diagnostic. A suspender
  woken in its own tick by its pop runs on (`scheduler/057`, found by seed 37).
- S3.13: the cancelling walks over the registry hold a hash iterator; the scheduler coroutine is
  current for the tick and the pop after a body; a coroutine woken with any error before it ran
  finishes with it at the pop (the error thrown with a frame on the stack, stored without one, as
  `scheduler_suspend` does too); outside scheduler context the enqueue refuses a wake with an error
  of the running current coroutine, before the error is applied; each walk takes only the
  coroutines present at its start. Coverage and Mull results,
  the uncovered lines and the explained survivors: S3.md sections 9 and 14. New test hooks:
  `add_printing_switch_handler`, `add_clearing_finish_handler`, the `transfer` argument of
  `enqueue_with_error`, callbacks scenarios `remove-pending`, `remove-absent`, `free-disposes`,
  `switch-handlers`, `switch-handlers-running`, `finish-remove-last`, `remove-past-cursor`. Mull's stage run: a driver with
  three workers, each with its own copy of `tests/`, run-tests `-j1`, `--timeout 120000` (the
  default 3 s timed out the warm-up run); about 20 minutes on 4 cores.
- S3.14: the security pass by `dev/SECURITY.md` (journal entry of 2026-10-03 per checklist item).
  `spawn()` keeps its callable's cache with references (`spawn_fcall_cache_release` drops them with
  the callable); a suspend leaves its EH_THROW window and its `@` behind (`zend_replace_error_handling`,
  `ini_error_reporting`); a main
  parked at from_main ends as a bailout; `await()` and `getResult()` dereference. run-tests gets
  `TEST_ENV_NAMES` only. Open findings with owners are in `dev/SECURITY.md`.
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
- `scheduler_cancel_all` also cancels the core's internal coroutines. A shutdown iterator cancelled
  before it ran reports nothing (only the coroutine that starts a pass is recorded in
  `EG(shutdown_context)`; Critic in S3.10); a destructor that waits for a later destructor and
  catches a graceful shutdown's cancellation, then waits again, spins (user-dependent).
- A fatal error raised by main in a shutdown function lands in the core's `zend_try` there; the
  last from_main call is then a plain one, and coroutines queued before the fatal run their bodies
  after it. TrueAsync's fork has the same structure; the fix is a core one (main.c passes
  `is_bailout` to the last call when `CG(unclean_shutdown)` flipped during the shutdown functions;
  the Sage). Kept as TrueAsync until Edmond wants the core change. S3.14: the same catch while
  main suspends left main queued and freed it (fixed in the extension, `internal/048`), and leaves
  the current coroutine at a finished one, whose switch handlers the shutdown destructors then fill
  (open in `dev/SECURITY.md`; reproducer: a shutdown function spawns a coroutine with an argument
  whose destructor bails out, cancels it, then `Async\await()`s another coroutine).
- Two coroutines that catch the deadlock's cancellation and await each other again loop, each
  round adding a `DeadlockError`; TrueAsync does the same (Critic, minor).
- D16's 5 s deadline after `exit()` needs a clock and a reactor timeout (S4).
- S5: a wait for several targets needs more than the one record in the waker (TrueAsync: two
  inline callbacks and a heap array). A multi-shot record (S4+) needs its own teardown rule.
- Spec gaps the test author named (S3.7): `await()` with `null`, an array or a second argument;
  the order woken waiters run in against coroutines already queued.
- Observers: every re-mint of main notifies a switch into a new context copy (as ts.c).
- Next core update: `ZEND_ASYNC_DEACTIVATE` also clears `in_scheduler_context`.
- Nested notifies recurse with no depth limit (S3.md 3.6 "As built").

- O6 costs 5.8 % wall time on the unbatched B1 (100 000 live coroutines, 16 B more fresh memory
  each) while it saves 4.9 % of instructions; D17's condition is the instruction count.
- The shared build pays 9 `__tls_get_addr` calls per suspend (B2 +40 % over the static build). The
  core declares the modules' TLS cache without a model; an initial-exec model for the module, as the
  core uses for itself, is a lever not tried (Edmond's call).

- The nightly `seeds` CI job (dbg 100, asan 20 seeds) was added without a run; its first nightly or
  dispatch run is its check.
- Two leaks found by the Sage in S3.13, for S3.18 or Edmond's call on ownership: a coroutine the
  core creates and fails to enqueue is never released. The GC coroutine (`gc_collect_cycles()` on
  a cycle under `-d fiber.stack_size=64G`: "Freeing ... (408 bytes)", the core's
  `new_gc_coroutine`, `zend_gc.c:2234`) and a Fiber's coroutine (`new Fiber` + `start()` under 64G,
  `zend_fiber_adopt`, `zend_fibers.c:1079`). Both core callers release only their own reference,
  as test_scheduler.c's rule expects: the live table owns a coroutine from its creation
  (`test_scheduler.c:810-818`). Ours inserts into the registry at the first enqueue and drops the
  birth reference in finalize. The fix by test_scheduler.c's rule must keep the deadlock count,
  which reads the registry. Reproducers: `/tmp/claude-0/sage/r6_gc_no_stack.php`, `r8_fiber_no_stack.php`
  (the container's scratch, gone with it).
- `ZEND_ASYNC_NEW_COROUTINE`'s `extra_size` is ignored, as `test_scheduler.c:1373` ignores it; no
  core caller passes it. S3.18 drops the parameter from the core API or defines it.
- Seeds 1-100 on dbg list 16 tests whose output gains a diagnostic under some order (their
  expectations assume FIFO); two were read, `scheduler/034` (the lost coroutine, fixed) and
  `scheduler/037` (an order artifact); the others are not read.

- The core's `ZEND_ASYNC_FCALL_DEFINE` and upstream `Fiber::__construct` keep the callable's cache
  without references (S3.14, open in `dev/SECURITY.md`): `async-core` in S3.18, the upstream report
  on Edmond's word.

## Next

1. S3.15-S3.18 (health fixes); S3.16's enqueue and finish-handler tests came in S3.13.
