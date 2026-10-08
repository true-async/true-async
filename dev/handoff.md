# Handoff

Where the work stopped and what the next session needs. Replaced whole at every stop; the plan
(`dev/PLAN.md`) outranks this file when they differ.

Written 2026-10-06. S3 closed: S3.24 re-ran its Done when on the final core; S4, S5 and S6 run as tracks.

## State

- Core pinned: `async-core-io-2026-10-08-2` (`bbbbe010dd4`), ior `e13c369400e` (2026-10-08, S6.10). CI gates every lane on every list; a
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
  `scheduler/055`, `056`; the no-stack tests use `fiber.stack_size=1048576G` (1 PiB), which every system refuses. Benchmarks: `bench/`, `tools/bench.py --count`
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
  `is_safely` until S9; `gc_new_coroutine` stayed NULL until S9.2 and S6.8 (the engine's scope,
  no IO provider).
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

## S1

S1.5 done 2026-10-07 on Edmond's Windows PC (folder `E:\php`, Remote Control): `tools/windows/`
builds ior and the core with this repository as `ext\true_async` and runs the core's suites and
`pocs-win`; the CI windows job builds Release_TS only. Left for S6.10 (the S6 track's step):
`xfail-on:pocs-win(S6.10)` on `stream/033`, `035`, `036` (`stream_select()` on `proc_open()` pipes
gives 0; `035` loops until run-tests' timeout, after which the runner now kills the process tree),
`dns/005`, `io/044`, and `--XFAIL--` in `stream/001`, `002`; Release_TS loads no shared extension,
so it skips the `sockets`, `openssl` and `curl` tests (157 SKIP against 78 on Debug_TS);
`socket_ext/006` gets WSAEINVAL (10022) from `socket_connect()` to `localhost:65000` on Windows,
outside a coroutine too, where the test accepts ECONNREFUSED and the like. For the next core update:
`ext/test_scheduler/tests/091_command_line_code.phpt` builds its path with `'/'` and misses the
path PHP prints on Windows; `__DIR__ . DIRECTORY_SEPARATOR` passes there. Open for the S5 rules: an
`await()` that finds a Timeout's deadline passed fires it in the waiter, so a throwing subscriber's
exception is chained under the waiter's `TimeoutException`, while the reactor's fire starts the
shutdown (`await/136` keeps a margin, DECISIONS 2026-10-07).

## S4

Stage closed 2026-10-07 with S4.7, the security pass (`dev/SECURITY.md`, the S4.7 entries; DECISIONS
of the day): D16's graceful exit reaching a coroutine woken in its own tick is thrown by its
`suspend()` (`reactor/043`), `delay()` and `timeout()` count their deadline in nanoseconds
(`async_reactor_deadline_from_ms()`, `reactor/042`), the poll's loop ends after the wakeup's
completion, and the wake pair is close-on-exec with `pipe2()` and not inheritable on Windows. As
built: `dev/plans/S4.md` 2-3 and its "As built (S4.6)" parts, with the recorded limits. Open for
Edmond: a pcntl handler that waits (`PLAN.md` "Open questions"); php-src's `socketpair_win32()`
binding `INADDR_ANY` (SECURITY, the php-src entry). `gc/020`, `023` stay XFAIL for the core's
awaited collection (S8). A test that needs spawned coroutines parked before main goes on yields with
`suspend()`: a short `delay()` may wake in main's own tick (U2). Full local runs need
`mysql-server-core-8.0`; the B1 count needs a release core and `bench/alloc_count.so`.

## S5

Stage closed 2026-10-07 after S4 (PLAN, Parallel tracks); its last step was S5.6, the security
pass. Known, not fixed (low): `await_*` refused for its token, or a Traversable whose `key()`
throws, warns "never used" for the Futures it was given; a Timer op that completes with an error
fires the `Timeout` as a deadline; a bailout while parked in `async_future_await()` or an `await_*`
wait leaks the wait's references until the request ends. Mull runs one S5 file at a time
(`tools/mull.py`'s `lane_for`, `run_mull` and `changed_lines`). In a fresh container: install the
packages of the "Packages" step of `.github/workflows/ci.yml` (`liburing-dev` among them), build
both cores with `tools/ci/build-core.sh` (`TRUE_ASYNC_CORE_SRC=/tmp/core-<tree>/php-src` then), `git
fetch --unshallow` and a clone of true-async/php-async at `REFERENCE` for `check-lists.py
--reference`; `gen_stub.php` needs `git clone --depth 1 --branch v5.6.1
https://github.com/nikic/PHP-Parser build/PHP-Parser-5.6.1` in the core checkout first.

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

S7.7 done 2026-10-07 (S7.2-S7.6 before it); no step left: the channel case waits for S9's
channels. S7.7 by Edmond's rule: a held `Scope` object keeps the coroutines of its scope and its
child scopes (one reach node per scope, S7.md 10); the error route and a SpawnStrategy with a null
`provideScope()` are left out and, in test-hook builds, hand out what was found in their subtree.
The SCOPE kind's `collector_target` stays NULL, so an `awaitCompletion()` waiter is never found.
The walk (`src/collector.c`) covers coroutines, Futures, tokens and `await_*` items; future events
are nodes of their own, counted by `base.ref_count`; a frame `zend_call_function()` pushed gives
its pinned `$this` (off for user frames under a replaced `zend_execute_ex`); the automatic run stops
before `memory_limit` and finds nothing (checked at every growth of the node table, its index and
the wake edges); `true_async.partial_deadlock_interval` is 5000 ms by default, at least 1000 or a
literal 0; S6.5's signal watch seeds its Futures live; nothing is found
once the request shuts down. `cancel` warns once and cancels every parked coroutine but main. Tests
`collector/001`-`072` in `tests/lists/S7.txt` (`064` skips on ASAN, DECISIONS); the holders' table is S7.md 10.
- the oracle (`async_collector_check_cancel()`, `async_collector_check_event_wake()`) aborts on a
  wake or cancel of a found coroutine not handed out; `registry_cancel()` and the `cancel` policy
  mark what they cancel as handed out, so the fuzz keeps `report`; `TrueAsync\Test\mark_found()`
  with `collector/040` shows each rule in a child process;
- a source that completes a counted awaitable without a counted reference seeds it from
  `async_collector_find()` (S7.md 3.4); S10's remote Future must add its seed (S7.md 10);
- `TrueAsync\Test\replace_execute_ex()` replaces `zend_execute_ex` for a test, as a profiler does.
Known misses: generator frames (`collector/017`, an S8 change-request candidate); a run stopped by
the ceiling raises nothing (DECISIONS). The TrueAsync reference clone is needed for
`check-lists.py --reference` (`/root/php-async`). Mull for the stage: `mull.py` builds every mutant
of `src/`; S7.5 scoped it to the collector tests with a scratch script (DECISIONS).

## S6

Written 2026-10-07, updated 2026-10-08. Stage closed with S6.10 (Windows): `proc_open()` pipes are
overlapped named pipes when the extension asks (S6.md 9.1), `pocs-win` loads `sockets`, `openssl`
and `curl`. A pipe handed to a child goes off the Ring's port through the queue op `release()` and
ior's `ior_release_handle()`; ior's filter still drops packets that are not its ops. Edmond opens the
PRs to bukka and libior/ior from `/mnt/project-files/notes/s6-10/`.

- Signal mask: `async_signal_reblock()` records in the registry's `reblocked` the watched numbers it
  blocked itself, and `signal_watch_free()` unblocks them (`signal/031`). `pcntl_signal_dispatch()`
  restores the whole mask it found, so a watch that goes inside a pcntl handler leaves its number
  blocked: fixed in php-src on `php-src-fixes` `74a581afc06` (branch `pcntl-dispatch-keeps-handler-mask`,
  PR text `notes/pcntl-dispatch-keeps-handler-mask-pr.md`, Edmond opens it); core -6 carries it and
  `signal/033` passes.
- The drain after a park skips an op with neither stream nor handle (`io_wait_drain()`): bukka's
  `php_io_ring_drain()` with a NULL owner matches every record, and `php_io_ring_deliver_one()` leaves
  such an op `in_flight` after an early Timeout (read, not reproduced; told to Edmond).

- IO chaos: `TRUE_ASYNC_SCHED=random:<seed>:io` on a fuzz build arms C1-C3 (S6.md 16);
  `tools/test.py --seeds N --io-chaos`. Under any seed about 40 tests of `S6.txt` change only their
  output's order and `io/100` (XFAIL) may pass: the scheduler's fuzz does that without `:io` too.
  Read a flagged seed's diffs before calling it a defect.
- A graceful shutdown with no coroutine left polls the reactor once without blocking, then ends;
  what still waits (a held `signal()` Future) is closed by the request's shutdown. A script ending
  by itself still waits. The async collector closing such a watch is PLAN "Open questions".
- `run()` drains a Ring op kept in flight after every park, completions included (S6.md 3.3).

- `run()` parks on a heap copy of the op and keeps its own reference to the event; the result and
  `in_flight` are read after the suspend (note 3.2-3.3). A non-running coroutine is answered
  Unsupported; a wake that is neither the completion nor a cancellation answers Interrupted.
- An accept is `accept()` first, then a POLL READ: the provider masks `F_DIRECT_ACCEPT` and turns
  the copy of an ACCEPT op into a POLL (note section 4). The Ring's multishot accept hid pending
  connections from `stream_select()`; that Ring bug goes to bukka (a pull request is being
  prepared in the S6.4 thread, `dev/WORKFLOW.md` "Ownership").
- The core is `async-core-io-2026-10-08-2` (`bbbbe010dd4`): `async-core-io-2026-10-08` with bukka's
  head `566a6833eb5` (a newer php-src master: `interface_gets_implemented` returns void; his
  `bdfa5fa7a12` keeps a watched signal blocked under pcntl), `io-hooks-fixes` `60ec85a2fb4` (the
  pipes) and `php-src-fixes` `acc6b34faa3` merged; ior `e13c369400e` (true-async/ior
  `release-handle`; CI fetches it by SHA through libior/ior's URL, which serves the fork's
  commits). `async-core-io-2026-10-08` (`662dfe91919`) was `async-core-io-2026-10-07-6` with
  `async-core` `b7c70909437` (a context value's replace and the context's destroy release the old
  value last, `dev/RFC-CHANGES.md` 16) merged. -6 is `async-core-io-2026-10-07-5` with
  `php-src-fixes` `74a581afc06` (the pcntl dispatch fix) and `async-core` `f6f3eb6e44b` (a test
  file only) merged. -5 is `async-core-io-2026-10-07-4` (the connect fix of
  `io-hooks-connect-started`, bukka/php-src#4) with `async-core` `50cd33b0eec` (the GC run first in
  the queue, API version 3, DECISIONS 2026-10-07); `io-hooks-fixes` `c43e1d5797a`. The
  seven tests that include a php-src helper need `TRUE_ASYNC_CORE_SRC` (the core's checkout);
  `tools/test.py` stops without it, so a Mull `--diff-ref` run needs it too.
- Windows: `stream/001`, `002` stay XFAIL by design (a socket write the kernel takes at once does
  not suspend); `dns/005` skips there (an empty host name resolves to the local addresses, as
  without the extension).
- `Async\signal()` is `src/os_signal.c` (note section 8): a watch per number with a SignalHandle in a
  Context the thread keeps while any number is watched, one SIGWAIT op on `waits`; a child renews
  handles and sources in `async_signal_rebuild()` and leaves no exception (S4.6's rule). The
  collector (S7.3) still has to treat a pending `signal()` Future as completable from outside:
  `ASYNC_G(signals)->watches[n]` holds its waits (`signal_wait_t`) with their Futures.
- A DNS lookup yields before its submit (`io_provider_run()`): the Ring could complete it inside the
  coroutine's own suspend tick, so `dns/003` saw the other coroutine print second.
- S6.6 changed no code: `curl/006` times out on `/very-slow` (1 s budget for the other request, so a
  loaded ASAN run may need its retry), `curl/025`, `054` expect the core's two send warnings,
  `curl/043` is by design, `pdo_mysql/029` waits for `RFC-CHANGES.md` 6 (it passes when no cancel
  lands mid-connect, so a run may see it pass on one attempt). Coverage lanes skip Windows-only tests.
- php-src bugs found on 2026-10-07 while answering devnexen on php/php-src#24168: `pclose()` from a
  filter and `zlib.inflate`'s notice, both on `php-src-fixes` with PR branches (WORKFLOW); the
  per-filter check for #24168 is in that PR and in `php-src-fixes`. Open in the same family:
  `fclose()` from an error handler during an internal filter, `proc_close()` from a filter, and
  `NO_FCLOSE` saved and restored per callback (overlapping Fibers clear it early).
- `stream/030` (UDP receive timeout) is `RFC-CHANGES.md` 4; the pipe timeout is 3; `F_FILES` off.
- MySQL: `tools/test.py` starts a private `mysqld` when `MYSQL_TEST_HOST` is unset; a container
  needs `apt-get install mysql-server-core-8.0` (WORKFLOW "Test fixtures").

## S9

S9.1 done 2026-10-07: `dev/plans/S9-scope.md` (layer 1, Scope) and `tests/lists/S9.txt` (91 tests with
`--XFAIL--` naming S9.2-S9.6; `scope/052` waits for Context in `S9.excluded`). Edmond chose
TrueAsync's behaviour for an unhandled error that reaches the global scope (note section 12).
The note cites the reference by `file:line` and the probes `p1.php`-`p9.php`
(`/mnt/project-files/s9/probes/`). A debug build of the reference to compare against: a worktree of
true-async/php-src branch `true-async` (`863f6dd9`) with `ext/async` copied from the `REFERENCE`
checkout, `./configure --disable-all --enable-debug --enable-async --enable-cli` (needs `libuv1-dev`,
`re2c`), about 3 minutes on 4 cores. A reference bug for Edmond: a `SpawnStrategy` resolving to the
global scope crashes TrueAsync (note section 9 item 8, `p9.php`); ours passes a stand-in `Scope`.

S9.2 done 2026-10-07: `src/scope.c`/`scope.h`/`scope.stub.php`. The global and the engine's scope
are made at RINIT and freed at RSHUTDOWN (`async_scope_request_shutdown`, before the registry's
release). `async_scope_spawn` is the body of `spawn()`, `Scope::spawn()` and `spawn_with()` and
returns an owned reference; the strategy path (`spawn_with_strategy`) holds the coroutine across the
hooks, which may suspend, and gives an objectless scope a stand-in object that stays attached while
anything holds it (`is_stand_in`: its destruction does not cancel).
Zombies: `ASYNC_COROUTINE_F_ZOMBIE`, `ASYNC_G(zombie_coroutines_count)`, the scope's two counts; a
zombie leaves both in finalize. `scope/053` (S9.5's) passes already. Own tests `internal/064`,
`065`, `scope/058` (GC off for the fuzz lane; `gc/025` runs the same load with GC on, DECISIONS
2026-10-07), `spawnWith/013`-`016`. Next is S9.3. S9.4 needs a non-owning edge reporter from S7
(note section 6); the S7 thread takes it after S7.6 and sends the signatures for review. Known, left
for S9.7: the collector's `cancel` mode does not reset its back-off for a parked zombie (its
cancelled bit was set without a wake).

S9.3 done 2026-10-07: the error route (note section 4) in `src/scope.c` (`async_scope_catch`,
`scope_handle_error`), called from `async_coroutine_finalize` after the notify and before the scope
removal, skipped when a wait record was called, for a cancellation, after a bailout, for a coroutine
already caught. The handlers are `zend_fcall_info_cache` fields; disposal releases them after the
scopes (`scope_handler_keep_back`). A handler runs in the finished coroutine and cannot park (note 9,
item 9; the Sage), `exit()` in it ends the request (item 10). Probes `s9.3/q1.php`-`q16.php`.
`scope/075` waits for php/php-src#24177 in the pinned core (the leak of a previous already in the
chain). Open for Edmond: handlers that park (TrueAsync's parked handler cannot be cancelled, `q15`,
`q16`). Next is S9.4; S7.7 brought the reach of a held Scope object, and a Scope object reports
nothing to the walk, so what its handlers capture counts as held from outside.

S9.4 done 2026-10-07: `Scope::awaitCompletion()` with the SCOPE wait kind (`async_wait_kind_scope`,
records[0] on the scope's event, the token in records[1]); `scope_notify_completion` at a member's
removal and zombie mark, `async_scope_cancel` and the route notify the waiters with the error. The
`await_*` iterator coroutine joins a child scope of the caller's (`async_scope_new`), which a finish
handler cancels when the iterator ends with an exception. Kept as TrueAsync on the Sage's word: with
safe disposal the cascade's zombie mark wakes a waiter as completed before the error (`scope/082`,
`083`); a second `cancel()` closes a scope whose members still run (probe `s9.4/w3.php`). The SCOPE
kind has no `collector_target` until S9.9 (the edges wait for S7.7, which Edmond questioned). Next
is S9.5; `awaitAfterCancellation()` is woken by the removal's notify (`with_zombies` true).

S9.9 done 2026-10-07: the SCOPE kind's `collector_target` reports a completion node per awaited
scope (a reach node keyed by `&scope->event`), live once a coroutine of its subtree is; new collector
reporters `async_collector_report_reach_target()` and `async_collector_report_reach_source()`. Two
cancels without an object: the route's `scope_hand_out_found()` also hands out found waiters, and the
`await_*` iterator coroutine is its scope's holder (`iterator_coroutine`). The oracle moved from the
record's wake to the notify sites, since a notify runs its callbacks in scheduler context. Own tests
`scope/084`-`091`. Next is S9.5: `awaitAfterCancellation()` is a second SCOPE-kind user, and the
`disposeAfterTimeout()` timer cancels without an object, so it must hold one or be reported.

S9.5 done 2026-10-07: `dispose()`/`disposeSafely()` are `cancel()` with no error, as TrueAsync's
`ZEND_ASYNC_SCOPE_CLOSE`; `disposeAfterTimeout()` arms one Timer op per scope
(`async_scope_t.dispose_timer`, earliest deadline wins) whose notify cancels the scope; the scope's
free and close withdraw it, a fork leaves it unarmed, and while armed on a scope that is not cancelled
it marks the reach node live (`async_collector_report_live_reach()`). `awaitAfterCancellation()`
waits for the whole subtree of a cancelled scope, zombies included, and returns at once for a closed
scope that is not cancelled, as TrueAsync's; its handler runs in the waiter, and an error that
comes while it runs climbs on (the Sage kept this over a scope-held intake). Own tests
`scope/094`-`110`. Next is S9.6: the iterator core and `finally()`, the bailout trace of
`bailout/013`-`015` first.

S9.6 done 2026-10-07: `src/iterator.c` ports TrueAsync's iterator core (workers in the iterator's
scope, the microtask that adds them, SAFE_MOVING); the last worker to leave ends with
`iterator->exception`, routed from its finish handler when it never ran. `Scope::finally()` and
`Coroutine::finally()` run on it (`async_finally_handlers_start`, `scope_finally_start`,
`ASYNC_SCOPE_F_DISPOSING`). Test hook `TrueAsync\Test\iterate()`. Probes in
`/mnt/project-files/s9/probes/s9.6/`. Own tests `internal/066`-`069`, `scope/111`-`117`,
`coroutine/040`, `bailout/016`, `017`. Next is S9.7, the layer review; the iterator is new code for its
coverage and Mull run.

S9.7 done 2026-10-07: the layer review (note 10, "The layer review"; DECISIONS, S9.7's backlog). The
oracle stamps a run id on each found coroutine (`found_run`, test hooks only) and checks a Future's
and a scope's waiters before the notify (`async_collector_check_records_wake`); the `cancel` policy
marks what it cancelled (`ASYNC_COROUTINE_F_DEADLOCK_CANCELLED`); a forked child rebuilds its reactor
at the idle point and in `get_deadlocked_coroutines()` (`async_reactor_check_fork()` returns true
when it rebuilt). A finish handler that replaces the exception gives the coroutine a new outcome
(the `async_finish_handler_add` contract). Benchmark `bench/b12.php` (B12). Mull: `tools/mull.py
--diff-ref d196cbd`; it builds the debug module, so ASAN-only mutants survive it. Probe of the deep
chain in `/mnt/project-files/s9/probes/s9.7/`. Next is S9.8, the security pass; the stack overflow
and the refused finally start in the cancel loops are its first items.

S9.8 done 2026-10-08: the security pass (`dev/SECURITY.md`, two entries of 2026-10-08).
`async_finally_handlers_start()` runs no PHP code and leaves refused handlers to its caller; a child
scope has `child_index`; `scope_is_completed()` takes the child scope a walk up came from. Own tests
`scope/123`-`125`. Layer 1 is done; layer 2 (Context) needs its plan agreed with Edmond first.

S9.10 done 2026-10-08: `dev/plans/S9-context.md` (layer 2, Context). The pinned core holds the
context's storage (`zend_async_context_t`, `zend_coroutine_t.context`, `zend_async_new_context_fn`),
so the layer wraps it in `Async\Context`, adds `async_scope_t.context` and the walk up the scope tree.
Probes `c1.php`-`c14.php` in `/mnt/project-files/s9/probes/s9.context/` ran on the debug build of the
reference that S9.1 above describes. Edmond approved the plan on 2026-10-08 and answered its question 1:
a Fiber's coroutine joins no scope. The core fix of `RFC-CHANGES.md` 16 is on `async-core`
`b7c70909437`, pinned in `async-core-io-2026-10-08` `662dfe91919`.

S9.11 done 2026-10-08: `src/context.c`/`context.h`/`context.stub.php`. `Async\Context` is the core's
`zend_async_context_t` itself (S9.12 puts the scope pointer in front, with the walk); `free_obj`
destroys the tables after `zend_object_std_dtor`, `get_gc` reports keys and values. MINIT sets
`zend_async_new_context_fn` once our scheduler registered, MSHUTDOWN resets it. `coroutine_context()`,
`current_coroutine()` and `Coroutine::getContext()` refuse a coroutine whose object is being freed
(`RFC-CHANGES.md` 17 asks the core for the same). The layer 2 block of `tests/lists/S9.txt`: 17
reference tests (11 with `--XFAIL--` naming S9.12), own `context/014`-`025`.

S9.12 done 2026-10-08: `Async\Context` is `async_context_t {scope, context}` in `src/context.c`; the
scope's context is lazy (`async_scope_t.context`, one reference), `find`/`get`/`has` walk up the
parent scopes, a freed scope detaches its context and releases it after the scope walk
(`released_values`), the global and engine scopes' at RSHUTDOWN. `get_gc` reports the handlers and
the context only under `scope_is_reached_only_by_object()`; `scope_objects_give_back_to_gc()` walks
up after a coroutine leaves or a child scope goes. `ASYNC_COROUTINE_F_LEFT_NON_GLOBAL_SCOPE` makes
`current_context()` refuse a finished coroutine's release window. `request_context()` returns null.
Open: the scope of Future callbacks (PLAN, Open questions).

S9.13 done 2026-10-08: `async_scheduler_request_shutdown()` and `async_scope_request_shutdown()` put
the user values they drop into one array that `PHP_RSHUTDOWN` releases after the reactor's teardown;
a throwing destructor there no longer skips the IO provider, signal and reactor teardown.
`future_drain_spawn()` and `Scope::disposeAfterTimeout()` do nothing while async is not active.
Test hook `TrueAsync\Test\print_at_teardown()`.

S9.14 done 2026-10-08: the layer review. Mull's one survivor (`scope.c:203`) is killed by
`scope/131`; four are explained in PLAN. `context/025` waits in a loop (the fuzz order). Benchmarks
B13 (`find()` up the scopes) and B14 (a coroutine's context) are in `dev/BENCHMARKS.md`.

S9.15 done 2026-10-08: the security pass, journal entries in `dev/SECURITY.md`; one recorded limit
(the scope object's `get_gc` recursion, now armed by a context alone). Layer 2 (Context) is done.
