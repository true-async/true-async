# Handoff

Where the work stopped and what the next session needs. Replaced whole at every stop; the plan
(`dev/PLAN.md`) outranks this file when they differ.

Written 2026-10-05. S3.23 (`call_on_main_stack`) done; every S3 step is closed, the next stage waits for Edmond.

## State

- Core pinned: `async-core-io-2026-10-05-4` (`9531d5b0b1f`). CI gates every lane on every list; a
  test that cannot pass yet carries `--XFAIL--` naming its step, and the commit that makes it pass
  removes the section. run-tests (`tools/run-tests.patch`) fails a test the timeout killed.
- S3.3-S3.6a: internal API, classes, the `Coroutine` object, the core's slots, the FIFO run queue,
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
- S3.15: the finish and switch handler functions take `zend_coroutine_t *` and sit in the core's
  slots; `test_trace` left the module globals (a test finish handler's data carries the trace, a
  test switch handler finds it beside its coroutine); the class entries of `Awaitable` and
  `Completable` are static; `dev/INDEX.md` lists the sources, the build files and every tool.
  The deadlock report's output (`dev/SECURITY.md`, open finding) waits for Edmond's answer, asked
  in the S3.15 thread with three options: default off, keep as TrueAsync, report through the
  error system; owner S3.18 (moved from S3.16, still unanswered). A default of off also changes `module/002-info`'s INI line.
- S3.16: own tests of the refusals (`module/003`, `004`, `scheduler/079`-`082`) and of main's adopt
  clearing the scheduler-context flag (`internal/049`); the coverage lane fails when `lcov --summary`
  gives no number. Uncovered lines of `true_async.c` with reasons: S3.md section 14. Two test.py runs
  at once share `tests/` and break each other's `.php` files: run lanes one after another. CI was
  red on win and mutants-coverage at 8fa582c (a `const` coroutine in `getResult()`, fixed); look at
  CI of the previous push before closing a step, even without waiting for the current one.
  `module/004` skips on win: the snapshot build makes `php_test_scheduler.dll`, but the lane loads
  only true_async; loading that DLL first there would run the test (the Sage, not done).
- S3.17: the circular buffer holds only what S3 calls: ctor/dtor, the pushes (always growing by
  doubling, never shrinking), the pointer helpers, `count`, `clean`; request memory directly,
  `allocator.{c,h}` deleted. `internal/014-buffer_shrink.phpt` now tests growth from tail 0 (the
  frozen list keeps the name). A later port of channels brings back what it needs from the
  reference. Not touched: `count`/`is_empty`/`is_full` stay out of line (efficiency report of
  2026-10-03, waits for Edmond).
- S3.18: a finish handler fires at most once and a throwing one may end
  the notify. test_scheduler has a fault seam (`test_scheduler.fail_new_coroutine`,
  `test_scheduler.fail_enqueue`, tests `079`-`085`); a refused shutdown or GC iterator leaves no
  exception. Ours: the registry holds a coroutine from its creation, so a core coroutine whose
  enqueue fails is released at RSHUTDOWN; CREATED entries are no waiters (`scheduler/086`, `087`).
- S3.19: the circular buffer holds pointers only (`push`, `push_front` take the pointer, no item size,
  no `count` for a buffer never constructed; `internal/017` counts a wrapped buffer);
  `CompositeException`'s add and class entry static; `async_callbacks_add()` is
  `test_callbacks_add()` in `test_hooks.c`; `addException()` throws as `$array[] =` when the list's
  next key is taken (`classes/010`).
- S3.20 (core): `ZEND_ASYNC_DEACTIVATE` clears the scheduler-context flag; test_scheduler tests
  027, 037, 038, 040-045 say why they depart from upstream. Its API removals are undone by S3.21.
- S3.21 (core, API version 1, 21 slots): every API S3.18 and S3.20 removed for having no caller is
  back, one revert per removing commit (P1.5, Edmond 2026-10-05); the API version is a counter.
  The bridge `true-async/ext-scheduler-hook` builds again and passes its 22 tests on dbg and ASAN
  (`92e14e7`, it fills `version`). Our scheduler fills `coroutine_from_object` and ignores
  `is_safely` until S9; `gc_new_coroutine` stays NULL.
- S3.22 (core, API version 2, 22 slots): the core launches only in a READY request; the bridge
  registers its C slots once per process and PHP calls `register()` in every request (bridge
  `a0fc2fd`, 23 tests); `extra_size`, `ZEND_ASYNC_NEW_COROUTINE_EX`, `active_coroutine_count` and
  the object-less coroutine are gone on Edmond's word; the `get_coroutine_count` slot is filled by
  ours (`scheduler_get_coroutine_count`) and test_scheduler, test `internal/050`.
- S3.23: `call_on_main_stack` is TrueAsync's `async_call_on_main_stack` with its naked asm (x86-64
  SysV and AArch64, where the compiler has `naked`; elsewhere fn runs on the caller's stack). It
  moves only the stack pointer below the OS stack's suspension point: main's context copy, or the
  engine's context while the queue drains after main. No current coroutine or main: a direct call,
  as TrueAsync; the running context decides around the drain. Test hook
  `TrueAsync\Test\call_on_main_stack()` returns the stack bases of caller and callback, test
  `internal/051` (Linux only: the core finds a stack by position there).
- Reviews: after the code, Critic and the Sage (`general-purpose`, model `fable`) compare it with
  TrueAsync (`/root/php-async` in the container) and hunt inventions; one plan step is one commit.
- Container notes: the ASAN lane needs `TRUE_ASYNC_CORE_SRC=/root/core-asan`; `gen_stub.php`
  cannot fetch PHP-Parser through the proxy, so `src/true_async_arginfo.h` was edited by hand
  (the stub hash is the stub's sha1); local `clang-format-18` (18.1.3) flags lines CI accepts, so
  check only changed lines (`git clang-format-18 --diff HEAD`).

## S4

S4.1 done 2026-10-05: `dev/plans/S4.md` and `tests/lists/S4.txt` (9 tests with `--XFAIL--` naming
S4.4). Next is S4.2, the wait-record layer of the note's section 2 with no reactor; S5.2 starts once
it is on `main`, and S5.4 once `delay()` (S4.4) is. S5.md section 6 still says `F_COUNTED` for the
TIMEOUT: the note's 2.5 answers N7 with the reactor's `waits` list instead, which S5.4 words in. The
S6 provider's op on `run()`'s frame under a bailout is S6.2's question for Edmond (S4.md 3.2: a
`zend_try`, or a heap op copied as the TIMER does). Full local runs need `mysql-server-core-8.0`
(S6.1's fixture).

## S5

S5.1 done 2026-10-05: `dev/plans/S5.md` and `tests/lists/S5.txt` (135 tests with `--XFAIL--`). Next
is S5.2, which waits for S4's wait-record layer on `main` (needs N1, N2, N9 of the note's section
7); `timeout()` (S5.4) waits for `delay()`. S4.1 answers N1-N9 in its note; a change S5 needs in the
layer goes through the coordinator. In a fresh container: build both cores with
`tools/ci/build-core.sh` (`RUNNER_TEMP=/root` for ASAN, then `TRUE_ASYNC_CORE_SRC=/root/core-asan/php-src`),
`git fetch --unshallow` before `check-lists.py` (a shallow clone takes its oldest commit as a list's
freeze), the reference clone at `REFERENCE` for `--reference`.

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
- S9's safe cancel (TrueAsync's `is_safely`, from its scopes) reaches our cancel through the core's
  slot; ours ignores it until then.
- Left from the Critic on S3.22 (all predate it): the bridge's `getModule()` reads the slots
  without its lock; `register()` inside a Fiber, during shutdown, and GC in a bridge request are
  untested; the `zend_fibers.c` line numbers in `core-integration.md` are stale. A provider that
  calls `ZEND_ASYNC_INITIALIZE` while its scheduler already runs gets a second launch (the bridge
  checks first; Critic, low).
- Left from the Critic on S3.23: a GCC-built AArch64 PHP gets no stack switch (GCC 13 has no
  `naked` there); a file-scope asm would cover it.
- `scheduler_cancel_all` also cancels the core's internal coroutines. A shutdown iterator cancelled
  before it ran reports nothing (only the coroutine that starts a pass is recorded in
  `EG(shutdown_context)`; Critic in S3.10); a destructor that waits for a later destructor and
  catches a graceful shutdown's cancellation, then waits again, spins (user-dependent).
- After a fatal error no queued coroutine runs, the shutdown phase included (DECISIONS
  2026-10-05). TrueAsync runs them on there.
- Two coroutines that catch the deadlock's cancellation and await each other again loop, each
  round adding a `DeadlockError`; TrueAsync does the same (Critic, minor).
- D16's 5 s deadline after `exit()` needs a clock and a reactor timeout (S4).
- S5: a wait for several targets needs more than the one record in the waker (TrueAsync: two
  inline callbacks and a heap array). A multi-shot record (S4+) needs its own teardown rule.
- Spec gaps the test author named (S3.7): `await()` with `null`, an array or a second argument;
  the order woken waiters run in against coroutines already queued.
- Observers: every re-mint of main notifies a switch into a new context copy (as ts.c).
- Nested notifies recurse with no depth limit (S3.md 3.6 "As built").

- O6 costs 5.8 % wall time on the unbatched B1 (100 000 live coroutines, 16 B more fresh memory
  each) while it saves 4.9 % of instructions; D17's condition is the instruction count.
- The shared build pays 9 `__tls_get_addr` calls per suspend (B2 +40 % over the static build). The
  core declares the modules' TLS cache without a model; an initial-exec model for the module, as the
  core uses for itself, is a lever not tried (Edmond's call).

- The nightly `seeds` CI job (dbg 100, asan 20 seeds) was added without a run; its first nightly or
  dispatch run is its check.
- Seeds 1-100 on dbg list 16 tests whose output gains a diagnostic under some order (their
  expectations assume FIFO); two were read, `scheduler/034` (the lost coroutine, fixed) and
  `scheduler/037` (an order artifact); the others are not read.

- The upstream report on `Fiber::__construct` keeping its callable's object without a reference
  (fixed on `async-core` by S3.14, `2cb30e538e4`) went upstream as php/php-src#24134 (Edmond,
  2026-10-05).

## Next

1. Ask Edmond what comes next (S4 is planned, not started on its own); PLAN "Open questions" holds
   the callbacks into PHP from `call_on_main_stack`.
2. `tools/check-lists.py` compares a frozen list only with its first commit, so a line added later
   and then deleted passes (S2.md section 3 says the same; Critic and Sage in S3.19): for the next
   health check. Next health check: passes 6 and 10; its S3.19 and S3.20 lines in "Open findings"
   are resolved there.

## S6

Written 2026-10-05. S6.1 (fixtures) done; S6.2 waits for S4's design note on `main`.

- MySQL: `tools/test.py` starts a private `mysqld` for a run with `mysqli` or `pdo_mysql` tests
  when `MYSQL_TEST_HOST` is unset; a container needs `apt-get install mysql-server-core-8.0`
  (WORKFLOW "Test fixtures"). CI sets `MYSQL_TEST_*` to its `mysql:8.3` service in the jobs that run
  the whole list (linux, seeds, mutants-coverage).
- HTTP: tests start TrueAsync's `common/http_server.php`; every test gets
  `PHP_CLI_SERVER_WORKERS=4`. The seven reference tests that include php-src's
  `sapi/cli/tests/php_cli_server.inc` by a relative path still need the core-tree path variable of
  `dev/plans/S2.md` section 1 when S6 ports them.
- `tests/lists/S6.txt` holds the two helpers and the three smoke tests; S6.2 adds the frozen rest
  and `S6.excluded`.
