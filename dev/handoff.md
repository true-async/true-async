# Handoff

Where the work stopped and what the next session needs. Replaced whole at every stop; the plan
(`dev/PLAN.md`) outranks this file when they differ.

Written 2026-10-06. S3 closed: S3.24 re-ran its Done when on the final core; S4, S5 and S6 run as tracks.

## State

- Core pinned: `async-core-io-2026-10-06` (`1ee473ff67b`), ior `2bfd2319896`. CI gates every lane on every list; a
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
- S3.24: the stage's Done when re-run on `9531d5b0b1f` (PLAN S3.24 has the numbers). New test hook
  `TrueAsync\Test\coroutine_from_object()` (`internal/062`); `scheduler/103`, `104` cover the
  deadlock report's skip of a refused core coroutine and an unobserved ParseError after a fatal
  error; `105` pins where such an exception is printed. `tools/mull.py --diff-ref` runs each
  mutant on its own copy of `tests/` with all CPUs (`--isolated-run`), run-tests `-j1`, and
  `--build-ref` builds only the mutants changed since a commit; the seed report matches diagnostics
  through EXPECTF placeholders; `check-lists.py` refuses a shallow clone.
- Reviews: after the code, Critic and the Sage (`general-purpose`, model `fable`) compare it with
  TrueAsync (`/root/php-async` in the container) and hunt inventions; one plan step is one commit.
- Container notes: the ASAN lane needs `TRUE_ASYNC_CORE_SRC=/root/core-asan`; `gen_stub.php`
  cannot fetch PHP-Parser through the proxy, so `src/true_async_arginfo.h` was edited by hand
  (the stub hash is the stub's sha1); local `clang-format-18` (18.1.3) flags lines CI accepts, so
  check only changed lines (`git clang-format-18 --diff HEAD`).

## S4

S4.5 done 2026-10-06: triggers another thread fires (`async_trigger_*` in `src/reactor.c`, the
TRIGGER kind, test hooks `TrueAsync\Test\trigger_*()`), woken by a POLL op on wake descriptors the
reactor keeps per thread in the module globals (no `NotifyHandle`; `dev/plans/S4.md` 3.6 "As built
(S4.5)", `RFC-CHANGES.md` 1). The deadlock counts a trigger between its start and stop; a fork
rebuild cancels the parent's trigger waiters and makes the child's descriptors. S4.4 before it:
`delay()` and D16 (S4.md 3.7). Next is S4.6, the stage review: Critic over S4.2-S4.5, coverage,
Mull, fuzz over 100 seeds, and the Ring's lateness with many Timer ops (`dev/BENCHMARKS.md`, S4.4).
The S6 thread edits `src/reactor.c` for S6.3 (S6.md section 13) and changes `reactor/021`'s parent
line (its waitpid now parks). Open for Edmond: a pcntl handler that waits (`PLAN.md` "Open
questions"). `gc/020`, `023` stay XFAIL for the core's awaited collection (S8). A test that needs
spawned coroutines parked before main goes on yields with `suspend()`: a short `delay()` may wake in
main's own tick (U2). Full local runs need `mysql-server-core-8.0`; the B1 count needs a release core
and `bench/alloc_count.so`.

## S5

S5.3 done 2026-10-06: `$cancellation` on `await()` and `Future::await()`, the `await_*` family in
`src/await.c`, `OperationCanceledException`, own tests `await/100`-`115`. Next is S5.4: `timeout()`
and `TimeoutException` on S4.4's Timer ops (S5.md section 6). Trap for S5.4: every helper of
`src/await.c` that branches on the type bit (`await_outcome`, `async_awaitable_addref`/`release`,
`await_mark_observed`, `async_await_token_check`) reads an event as an `async_future_event_t`, and
`await_trigger_of` accepts only `Coroutine` and `Future`: the `Timeout` event needs its own branch in
each. Known, not fixed: a bailout while a coroutine is parked in `async_future_await()` or an
`await_*` wait leaks the wait's references until the request ends, as `scheduler_await` leaks its
target's (the Critic, low); an enqueue that is not the wait's own ends an `await_*` wait with the
results so far, as in TrueAsync (S5.md section 5). A change S5 needs in the layer goes through the
coordinator. In a fresh container: build both cores with `tools/ci/build-core.sh` (`RUNNER_TEMP=/root`
for ASAN, then `TRUE_ASYNC_CORE_SRC=/root/core-asan/php-src`), `git fetch --unshallow` before
`check-lists.py` (a shallow clone takes its oldest commit as a list's freeze), the reference clone at
`REFERENCE` for `--reference`. `gen_stub.php` cannot download PHP-Parser through the proxy: `git clone
--depth 1 --branch v5.6.1 https://github.com/nikic/PHP-Parser build/PHP-Parser-5.6.1` in the core
checkout first. Run `tools/format.sh` before a commit.

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
- Seeds 1-100 on dbg (2026-10-05, 333 tests) list 10 tests whose output gains a diagnostic under
  some order; each was read and replayed with its seed: order artifacts (expectations assume FIFO;
  a coroutine cancelled before it ran finishes without its body). `scheduler/034` earlier found a
  lost coroutine (fixed).

- The upstream report on `Fiber::__construct` keeping its callable's object without a reference
  (fixed on `async-core` by S3.14, `2cb30e538e4`) went upstream as php/php-src#24134 (Edmond,
  2026-10-05).

## Next

1. S3 is closed; S4, S5 and S6 run as parallel tracks (PLAN "Parallel tracks", their own sections
   here). PLAN "Open questions" holds the callbacks into PHP from `call_on_main_stack`.
2. `tools/check-lists.py` compares a frozen list only with its first commit, so a line added later
   and then deleted passes (S2.md section 3 says the same; Critic and Sage in S3.19): for the next
   health check. Next health check: passes 6 and 10; its S3.19 and S3.20 lines in "Open findings"
   are resolved there.

## S7

S7.2 done 2026-10-06: the walk (`src/collector.c`), `Async\get_deadlocked_coroutines()`, the
automatic run with `report` at the idle point, the fuzz oracle; `collector/001`-`025` in
`tests/lists/S7.txt`. Next is S7.3, once S5.3 is on `main`:
- give FUTURE, the token kinds, TIMEOUT and the `await_*` block records their `collector_target`,
  and in the same step seed the events on the reactor's `waits` list and the triggers on S4.5's
  `triggers` with `start_count > 0` as live (S7.md 3.4; the list holds unstarted ones too); the D16 exit Timer is on `own` and is not seeded;
- write the holders' table of S7.md 10 from S5's code first (S5.2: the FUTURE wait owns a
  reference, `src/future.c:703`); S6.5's `SignalHandle` must report its `signal()` Futures.
S7.4's `cancel` must cancel through `registry_cancel()` or mark the coroutine handed out, or the
oracle (`async_collector_check_cancel()`) aborts the seed. Known miss: generator frames are not
walked (`zend_generator_frame_gc` has no `ZEND_API`, `collector/017`), an S8 change-request
candidate. The TrueAsync reference clone is needed for `check-lists.py --reference`
(`/root/php-async` at `REFERENCE` in this container).

## S6

Written 2026-10-06. S6.3 (the IO provider, `src/io_provider.c`) done; S6.4 (sockets, DNS) next.

- `run()` parks on a heap copy of the op and keeps its own reference to the event; the result and
  `in_flight` are read after the suspend (note 3.2-3.3, the Sage's ruling). A non-running coroutine
  (a main a caught bailout left, a switch handler inside a suspend) is answered Unsupported; a wake
  that is neither the completion nor a cancellation answers Interrupted.
- The core is `async-core-io-2026-10-06`: bukka's head refuses `run()` under a pending exception
  and keeps a cancelled read's bytes; our `io-hooks-fixes` keeps `ECANCELED` under an exception
  from setting eof. A new bug in bukka's code goes by `dev/WORKFLOW.md` "Ownership".
- S4.6 is moving Timer events to a reactor heap; `run()` uses `async_io_event_try_submit()`, which
  S4 agreed to route through the heap too, and saturates an infinite Timer itself.
- Open for Edmond: where a php-src streams fix goes (`io/094`, `095`, TrueAsync F `bf6048d03c6`);
  asked on a card in the S6 thread 2026-10-06.
- Next steps keep the order of `dev/PLAN.md`: S6.4 sockets (several `stream/` and `socket_ext/`
  tests already pass), S6.5 children, signals and the Windows pipe commit, S6.6 curl and MySQL.
- `F_FILES` stays off; the pipe timeout (`io/039`, `040`, `042`, `043`) is `RFC-CHANGES.md` 3.
- MySQL: `tools/test.py` starts a private `mysqld` when `MYSQL_TEST_HOST` is unset; a container
  needs `apt-get install mysql-server-core-8.0` (WORKFLOW "Test fixtures").
