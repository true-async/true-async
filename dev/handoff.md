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

S5.4 done 2026-10-06: `Async\timeout()`, `Timeout` and `TimeoutException` in `src/timeout.c` (S5.md
section 6), own tests `await/116`-`127`. Next is S5.5, the stage review (PLAN). Left open from the
Critic on S5.4: a Timer op that completes with an error status fires the `Timeout` as a deadline
would, where `delay()` throws an Error (not seen; the core's Timer has no error path we know of);
the wait for the rest of `await_*` does not link the token, as in TrueAsync (S5.md section 8). For
S5.5's benchmark of many waiters on one token: each subscribe to an armed `Timeout` pays a `getpid()`
for the fork check, as every submit does. Known,
not fixed: a bailout while a coroutine is parked in `async_future_await()` or an `await_*` wait leaks
the wait's references until the request ends, as `scheduler_await` leaks its target's (the Critic,
low); an enqueue that is not the wait's own ends an `await_*` wait with the results so far, as in
TrueAsync (S5.md section 5). A change S5 needs in the layer goes through the coordinator. In a fresh
container: build both cores with `tools/ci/build-core.sh` (`RUNNER_TEMP=/root` for ASAN, then
`TRUE_ASYNC_CORE_SRC=/root/core-asan/php-src`), `git fetch --unshallow` before `check-lists.py` (a
shallow clone takes its oldest commit as a list's freeze), the reference clone at `REFERENCE` for
`--reference`. `gen_stub.php` cannot download PHP-Parser through the proxy: `git clone --depth 1
--branch v5.6.1 https://github.com/nikic/PHP-Parser build/PHP-Parser-5.6.1` in the core checkout
first (the build script removes it). Run `tools/format.sh` before a commit.

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

S7.4 done 2026-10-07 (S7.2, S7.3 before it): the walk (`src/collector.c`) covers coroutines,
Futures, tokens and `await_*` items; future events are nodes of their own, counted by
`base.ref_count`, and `Future`/`FutureState` report through `async_future_collector_references()`;
S6.5's signal watch seeds its Futures live; nothing is found once the request shuts down. The
`cancel` policy warns once and cancels every parked coroutine but main; the back-off resets on a
first warning or a first cancel. B6 is in `dev/BENCHMARKS.md` (debug build). Tests `collector/001`-`047`
in `tests/lists/S7.txt`; the holders' table is S7.md 10. Next is S7.5, the stage review:
- the oracle (`async_collector_check_cancel()`, `async_collector_check_event_wake()`) aborts on a
  wake or cancel of a found coroutine not handed out; `registry_cancel()` and the `cancel` policy
  mark what they cancel as handed out, so the fuzz keeps `report` (under `cancel` it sees nothing);
  `TrueAsync\Test\mark_found()` with `collector/040` shows each rule in a child process;
- a new wait kind names its target through `collector_target` and reports it as owned only for a
  reference its wait took in C that no walked slot reports (collector.h); a source outside the walk
  that will complete an event seeds it with `async_collector_report_live_event()`.
Known miss: generator frames are not walked (`zend_generator_frame_gc` has no `ZEND_API`,
`collector/017`), an S8 change-request candidate. The TrueAsync reference clone is needed for
`check-lists.py --reference` (`/root/php-async` at `REFERENCE` in this container).

## S6

Written 2026-10-07. S6.6 (curl, mysqli, pdo_mysql) done; S6.7 (shutdown windows, every list run)
next; the Windows part is S6.10, waiting for a Windows agent (S1.5).

- `run()` parks on a heap copy of the op and keeps its own reference to the event; the result and
  `in_flight` are read after the suspend (note 3.2-3.3). A non-running coroutine is answered
  Unsupported; a wake that is neither the completion nor a cancellation answers Interrupted.
- An accept is `accept()` first, then a POLL READ: the provider masks `F_DIRECT_ACCEPT` and turns
  the copy of an ACCEPT op into a POLL (note section 4). The Ring's multishot accept hid pending
  connections from `stream_select()`; that Ring bug goes to bukka (a pull request is being
  prepared in the S6.4 thread, `dev/WORKFLOW.md` "Ownership").
- The core is `async-core-io-2026-10-06`. Edmond's branch for php-src bugs outside the RFCs is
  `php-src-fixes` (`dev/WORKFLOW.md`); its `io/094` fix is not in the pinned core yet.
- S6.10 takes the Windows lane's socket expectations: load `sockets` and `openssl` in `pocs-win`,
  then settle the `xfail-on:pocs-win(S6.10)` tags and `stream/001`, `002`, `046-…_win`, `exec/001`,
  `003`. The `skip-on:pocs-win(...-until-S6.4)` and `(...-until-S6.5)` tags are frozen text; they
  mean S6.10.
- `Async\signal()` is `src/os_signal.c` (note section 8): a watch per number with a SignalHandle in a
  Context the thread keeps while any number is watched, one SIGWAIT op on `waits`; a child renews
  handles and sources in `async_signal_rebuild()` and leaves no exception (S4.6's rule). The
  collector (S7.3) still has to treat a pending `signal()` Future as completable from outside:
  `ASYNC_G(signals)->watches[n]` holds its waits (`signal_wait_t`) with their Futures.
- `dns/003` fails about 4 in 60 runs under load: the Ring completes the lookup during the submit's
  flush, so the provider returns without suspending and the other coroutine prints second
  (`io_wait_submit()`, `src/io_provider.c`). TrueAsync always yields for DNS. Not fixed; S6.7's
  "every list run" decides (yield after an inline DNS completion, or a by-design tag).
- S6.6 changed no code: `curl/006` times out on `/very-slow` (1 s budget for the other request, so a
  loaded ASAN run may need its retry), `curl/025`, `054` expect the core's two send warnings,
  `curl/043` is by design, `pdo_mysql/029` waits for `RFC-CHANGES.md` 6 (it passes when no cancel
  lands mid-connect, so a run may see it pass on one attempt). Coverage lanes skip Windows-only tests.
- php-src bugs found on 2026-10-07 while answering devnexen on php/php-src#24168: `pclose()` from a
  filter and `zlib.inflate`'s notice, both on `php-src-fixes` with PR branches (WORKFLOW); the
  per-filter check for #24168 is `stream-filter-remove-per-filter`, waiting for Edmond. Open in the
  same family: `fclose()` from an error handler during an internal filter, `proc_close()` from a
  filter, and `NO_FCLOSE` saved and restored per callback (overlapping Fibers clear it early).
- `stream/030` (UDP receive timeout) is `RFC-CHANGES.md` 4; the pipe timeout is 3; `F_FILES` off.
- MySQL: `tools/test.py` starts a private `mysqld` when `MYSQL_TEST_HOST` is unset; a container
  needs `apt-get install mysql-server-core-8.0` (WORKFLOW "Test fixtures").
