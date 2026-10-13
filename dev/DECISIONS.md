# Decisions

Newest last. One line for the decision, one for the reason; more only for a rejected option
someone will propose again.

- 2026-10-01 Rebuild from scratch as a regular extension. Why: `ext/async` needs the fork's core
  (design round 1). P1.1, P1.3.
- 2026-10-01 Core = php-src branch `async-core-io`: master + #22561 + #23997 + ior, no upstream.
  Why: RFCs not merged; `async-core` is the head of #22561. P1.1.
- 2026-10-01 Reactor = one per-thread `php_io_queue` shared with the IO hooks provider. Why:
  orphans and drains reach a provider only through the queue (review M3). Rejected: libuv
  (embedding spun the loop). P1.2.
- 2026-10-01 Deadlock from the scheduler's own count of parked waits, not `EDEADLK`. Why: the
  queue's pending count is wrong both ways (review M5). Replaced by the 2026-10-05 entry on the
  reactor's lists and the 2026-10-06 S4.6 entry on `EDEADLK`.
- 2026-10-01 Every wait is a wait-graph edge from S3; the collector (S7) walks it. Why: Edmond.
- 2026-10-01 Fork with coroutines parked on IO: tests not ported. Why: Poll context unusable after
  fork. P2.1.
- 2026-10-01 Module `true_async`, `--enable-true-async`, `ext/true_async`.
- 2026-10-01 BSD-3-Clause. Why: as php-src.
- 2026-10-01 Windows pipes, console, processes via IOCP in the Ring. Why: WSAPoll is sockets only.
  P3.2.
- 2026-10-01 SKIP is not a pass; two core trees in CI; cumulative stage lists. Why: a Done-when
  passed on skipped tests (design round 3).
- 2026-10-01 Repository in `~/true-async`, outside the core tree. Why: Edmond. Rejected: symlink
  into `ext/` (breaks `__DIR__` tests).
- 2026-10-01 Initial stage: commits straight to `main`, no PRs, no issues. Why: Edmond.
- 2026-10-01 Scheduler PoC bugs are fixed on `async-core` directly; bukka's code through PRs to
  his repositories. Why: the scheduler RFC is ours, the IO RFCs are bukka's.
- 2026-10-01 S1.5 (Windows build of the core) deferred until a Windows agent exists. Why: Edmond;
  scripts that cannot be run are not written. Departs from P3.2 for S1 only.
- 2026-10-01 Linux build: `phpize` against an installed core prefix; Windows: a copy into
  `ext/true_async`. Why: Mull and coverage instrument the extension alone, the core trees stay
  merge-only; `php-windows-builder` cannot target a custom core. Rejected on Linux: the copy
  (needs a clang-built core for Mull, `buildconf` per `config.m4` change). Cost: seven tests reach
  php-src files through `../../../../`; S6 ports them with the path from the environment.
- 2026-10-01 `async-core-io` is published in true-async/php-src under its own name, pushed by
  explicit refspec, no tracking upstream. Why: CI needs it; Edmond.
- 2026-10-01 INI names take the module prefix `true_async.*`. Why: Edmond, the PHP convention.
  The five `edge_cases` tests that set `async.debug_deadlock` are ported with `changed:` tags.
- 2026-10-01 S2.4 includes a Windows CI job. Why: Edmond; CI runs Windows without a local agent.
- 2026-10-01 TSAN tree waits for threads (S9). Why: nothing to race before threads exist.
- 2026-10-01 The run verdict comes from `results.py` against expected statuses, WARN counted as
  FAIL. Why: run-tests ignores SKIP and WARN and passes a failing test on retry (Critic, S2.1).
- 2026-10-01 run-tests is patched by the runner (`tools/run-tests.patch`): the environment comes
  from `/proc/self/environ` and each test runs through `/bin/bash`. Why: Mull switches a mutant on
  by a variable named after its source path, which `$_ENV`, `getenv()` and dash drop.
- 2026-10-01 Mull's `gitDiffRef` is not used; `mull.py --diff-ref` builds every `src/` mutant and
  keeps the lines `git diff -U0` changed. Why: Mull 0.34.1 makes no mutant in a file added whole.
- 2026-10-01 Planted known-answer functions live in `src/known_answer.c`, built only with
  `--enable-true-async-known-answer`. Why: the check needs real module code; release builds must
  not carry it.
- 2026-10-01 S3 adapts code of `ext/async` `1fdacf8`: the circular buffer
  (`internal/circular_buffer`, `allocator`) and the scheduler; the fork's `Zend/zend_async_API.h`
  parts they really need move into the extension as `true_async_API.h`. Adapted, not copied: each
  part is reviewed with performance as the criterion (allocations, copies and pointer chasing on
  spawn, enqueue, switch, suspend, await), what S3 does not need is cut, defects found are fixed,
  and the S3 note lists what changed against the reference and why. Why: Edmond. Departs from
  P1.3 for these parts.
- 2026-10-01 Waiting keeps the reference's model (events, callbacks, the waker, `resume_when`).
  Replaced by the 2026-10-02 entry below.
- 2026-10-02 S3 design agreed (`dev/plans/S3.md`): a coroutine waits through wait records on its own
  frame's stack; the waker keeps the error, the result and a pointer to the records; events have no
  methods; `zend_coroutine_t.flags` bits 0-15 belong to the core, 16-31 to the scheduler, bit 31 = 0
  marks a coroutine. Why: Edmond, after two rounds of experts, Critic and Sage; fewer allocations and
  bytes than the reference. Edmond's 31 answers: `dev/reviews/s3-structures/EDMOND-DECISIONS.md`.
- 2026-10-02 Pushes to `main` no longer wait for Edmond's OK on the diff. Why: Edmond.
- 2026-10-02 An existing test is changed only with a reason (wrong test, contradicts the RFC or a decision)
  that Critic accepts; Critic's doubt goes to Edmond. Unfinished tests carry the standard `--XFAIL--` section, no ratchet. Why: Edmond.
- 2026-10-02 Tests changed for S3.4, each for a reason in dev/plans/S3.md: `module/001-registration.phpt`
  and `module/002-info.phpt` list the new INI `true_async.debug_deadlock` (section 1);
  `edge_cases/001-deadlock-basic-test.phpt`, `edge_cases/002-deadlock-with-catch.phpt` and
  `edge_cases/003-deadlock-with-zombie.phpt` set it under its new name instead of `async.debug_deadlock`;
  `info/001-info-getCoroutines-basic.phpt` counts the main coroutine from the start and
  `common/current_coroutine_not_in_coroutine.phpt` gets the main coroutine instead of an error, since the
  RFC core starts the scheduler with the script (section 9). Why: the reference's behaviour no longer
  applies; Critic accepted all seven on 2026-10-02 (the predictions for `info/001` and
  `current_coroutine` come from reading; S3.5 and S3.8 run them).
- 2026-10-02 `Async\Coroutine` is built only by `spawn`: `new` and `newInstanceWithoutConstructor()`
  throw, unlike the reference, where `new` gives a coroutine with no entry point that no registry
  holds. Why: Critic on S3.4; every later step would have to handle that coroutine, and no
  reference test builds one.
- 2026-10-02 `async_callbacks_notify()` runs its callbacks in scheduler context
  (`ZEND_ASYNC_IN_SCHEDULER_CONTEXT`, saved in the frame and put back at exit) instead of blocking
  fiber switching; `tests/internal/011-callbacks_bailout_caught.phpt` checks the flag instead of the
  switch block. Why: Edmond, the extension never calls `zend_fiber_switch_block()`; S3.md 4.6
  already closes such windows with the flag.
- 2026-10-02 The notify cursor is a field of `async_callbacks_vector_t` (vector 24 B, coroutine
  304 B, same 320 B bin); the global notify frame array, its lookup, its depth limit and
  `async_callbacks_bailout_reset()` are gone. `tests/internal/011-callbacks_bailout_caught.phpt`
  no longer prints the frame depth. (Its bailout handling by a `zend_try` is replaced by the entry
  "No `zend_try`" below.) Why: Edmond; saving 8 B had cost a chain, a global array and a switch block.
- 2026-10-02 `async_finish_handler_add/remove` take the coroutine, not a vector: a finish handler
  removes itself from its coroutine's vector, so a caller cannot hand it another one. Why: Critic on
  the cursor rework.
- 2026-10-02 No `zend_try` in `async_callbacks_notify()`: a bailout out of a callback leaves the
  vector marked and its later notifies refused, as in the fork. `tests/internal/011-callbacks_bailout_caught.phpt`
  expects the refusal; `internal/021`, added the same day for the recovery, is removed. Why: Edmond,
  one `setjmp` per notify (+3.7 to 6.7 ns measured) is too much for loops; TrueAsync had none, and
  its `bailout_all_coroutines()` unwinds every waiter directly.
- 2026-10-02 Review of the notify against TrueAsync (Code Reviewer, Critic, the Sage's verdicts):
  removed what TrueAsync lacks and nothing needs. The notify returns `void` (as the fork's) and no
  longer repairs the frame of an exception pending at entry; finish handlers fire once and lose
  `F_RUNNING`, `F_REMOVED` and the meaning of their `bool`; teardown no longer supports a free from
  the vector's own notify; `ASYNC_G(bailing_out)` and the callback base's unused `ref_count` (the
  fork's count for shared subscribers; nothing here shares one) are gone (`is_bailout` is bit 19,
  which `bailout_all_coroutines` will set on every coroutine, S3.md 4.5). Tests:
  `internal/009-finish_handler_keep` becomes `009-finish_handler_once`; `internal/019` and `020`
  (free during the notify) are removed. Why: Edmond, no mechanism without a counterpart in the
  reference or a recorded reason.
- 2026-10-02 The first callback of a notify that throws ends it; the rest stay uncalled and are
  disposed with the vector, as in TrueAsync (`zend_async_callbacks_notify()`, true-async/php-src
  `863f6dd90cf`). A finish handler behind a throwing one therefore never fires, against the core's
  "fires exactly once" (`zend_async_API.h:83`). Test `internal/010-callbacks_throw_all` becomes
  `010-callbacks_throw_stops`. Why: Edmond, "пока как в trueasync" (health check).
- 2026-10-02 Under a scheduler, a fiber adopted as a coroutine and parked at `Fiber::suspend()` is not
  collected by `gc_collect_cycles()` when the cycle runs through its body's closure: the closure sits
  in `coroutine->fcall`, a reference GC cannot see. It goes when the fiber finishes or is cancelled at
  the scheduler's shutdown. TrueAsync's core does the same (`zend_fiber_object_gc()`, `zend_fibers.c`
  at `863f6dd90cf`); it only shows here because the RFC core starts the scheduler with the script.
  The RFC lists it as a fourth behaviour change and core tests 034, 036, 055-059 say so (S3.18).
  Why: Edmond, health check.
- 2026-10-02 Principle P1.4 added: no mechanism without a counterpart in TrueAsync or a recorded
  reason. Why: Edmond, health check.
- 2026-10-02 S3.5 run model as built (`src/scheduler.c`): contexts are allocated apart from their
  stacks and freed in their cleanup, as TrueAsync's; the core read `context->stack` after that
  cleanup (heap-use-after-free under ASAN on the first spawn), so `async-core` `565f515df16`
  reads it first, as TrueAsync's core does; core branch `async-core-io-2026-10-02-2`. Why: Critic;
  freeing the context elsewhere would need a deferred-free list.
- 2026-10-02 A coroutine releases its callable right after the call, inside the body's try, as
  TrueAsync (`coroutine.c:534-535`); the arguments stay until the object dies. The callable is
  unset before its release, which runs destructors: a fatal error in one would otherwise free the
  closure twice, and what one throws is the coroutine's outcome. Tests `scheduler/004`, `008`,
  `009`. Why: Sage (a kept `Coroutine` held its closure), Critic (a release in finalize ran user
  code outside any try).
- 2026-10-02 `ZEND_ASYNC_IN_SCHEDULER_CONTEXT` is put back to false when main is adopted, as ts.c
  does, not in the context entry's catch as TrueAsync. Why: a bailout in call 1 reaches call 2 and
  its adopt, also one out of a notify on the OS stack, which the entry's catch misses. A bailout
  inside call 2 or 3 escapes before any adopt: S3.10's.
- 2026-10-02 A bailout drops the exit exception, as TrueAsync (`scheduler.c:1401-1409`): the
  bailout's error is what the request reports. Test `scheduler/006` pins the order before S3.8:
  with the graceful shutdown its second coroutine is cancelled before it runs, and S3.8 rebuilds
  the fixture. Why: Critic.
- 2026-10-02 `spawn`, `current_coroutine` and `get_coroutines` refuse while async is not active,
  which includes `php -r` (the core launches the scheduler only in `php_execute_script`).
  TrueAsync launches lazily on the first call. Why: under `php -r` no from_main call drains, so a
  lazily launched queue would never run.
- 2026-10-02 The unobservable-exception rule of S3.md section 6 (exit exception) is in finalize
  from S3.5, as in TrueAsync's finalize; the rethrow from the destructor and the graceful
  shutdown stay with S3.8. Why: the spawn tests of S3.5 print through it.
- 2026-10-02 The drain keeps a coroutine current until its body starts, and the coroutine that
  bails out clears the slot itself (TrueAsync, `coroutine.c:567-569`): a bailout before the body
  (no stack: on the OS stack the exception has no frame and is fatal; no VM stack page) leaves a
  never-started current coroutine, which the bailout's drop finishes with the queue. With a user
  exception handler the failed stack does not bail out: the coroutine finishes unrun. Tests
  `scheduler/005`, `007`. Why: Critic and Sage; the popped coroutine was lost, a NULL context was
  switched into, and the debug build aborted at RSHUTDOWN. Replaced by S3.6a: the drain is gone, and a stack
  that cannot be taken ends the request (entry below).
- 2026-10-02 `gc/013`, `gc/014` (S3.10) and `gc/022` (S3.7) carry `--XFAIL--` from S3.5: with the
  scheduler registered, the GC needs the await slot and `suspend()` (S3.md section 14). Until then
  an automatic collection over objects with `__destruct` does not end; nothing short of the
  parking `suspend()` and await fixes it. Why: Critic judged the reason real; the Sage found the
  hang and no S3.5 fix.
- 2026-10-02 S3.15-S3.18 (the health check's fixes) run after S3.14, at the end of S3. Why: Edmond.
- 2026-10-02 The suspend slot has no `zend_try` around the tick (4.2, U5): S3.6 links no wait
  record, and TrueAsync's tick (`scheduler.c:1517-1612`) has none. Why: P1.4; U5 comes back to
  Edmond with the records of S3.7 if it still needs a `zend_try`.
- 2026-10-02 A bailout in a coroutine while main is parked goes to main's stack with the bailout
  flag (ts.c); the from_main call drops the queue before main finishes, skipping a yielded main's
  own entry. Why: a stale entry would outlive the main it points to, and the context's catch has
  no bailout address left for a destructor that bails out again (Sage).
- 2026-10-02 `bailout/012` carries `--XFAIL--` for S3.10 again (S3.md section 14). Why: it passed
  in S3.5 only because `suspend()` threw; its yielded coroutine needs the bailout drain of S3.10.
  Withdrawn by S3.6a: the scheduler coroutine's bailout unwinds it, and the test passes.
- 2026-10-02 run-tests (`tools/run-tests.patch`) never passes a test the timeout killed. Why:
  `edge_cases/014` hangs until S3.8, and its trailing `%A` took the timeout as a pass.
- 2026-10-02 `Async\suspend()` and the suspend slot refuse inside a Fiber the scheduler did not
  adopt (`EG(current_fiber_context)` is not the coroutine's context), until S3.9 adopts every
  Fiber; test `scheduler/013`, which S3.9 changes. Why: Critic; the yield parked the Fiber's stack
  as main's, and main later resumed inside `Fiber::start()`.
- 2026-10-02 `Async\suspend()` does nothing while async is off, as TrueAsync (`async.c:227-229`);
  `spawn` and the others still refuse. Why: P2.2; a yield has nothing to give up there.
- 2026-10-02 A stack that cannot be taken in `suspend()` finishes the next coroutine unrun with
  that exception as its outcome, as the drain does, not 4.2 step 3's abort plus exit exception.
  Why: the suspender did nothing wrong; the coroutine that got no stack owns the failure.
  Replaced by S3.6a: a stack that cannot be taken ends the request (entry below).
- 2026-10-02 The extension never reads `zend_fiber_switch_blocked()` either: `Async\suspend()` and
  the suspend slot no longer refuse on it, and `tools/check-gates.py` forbids it in `src/`. A window
  the extension must close is closed by `ZEND_ASYNC_IN_SCHEDULER_CONTEXT`, as TrueAsync; the core's
  own switch-block windows (pcntl dispatch, ticks, IO-hooks lock) stay open, as in TrueAsync.
  Withdraws D14, which Edmond says was Claude's error, not his decision. Why: Edmond.
- 2026-10-02 The scheduler follows TrueAsync's hybrid algorithm: the tick and direct switches
  between coroutines, plus a scheduler coroutine on its own fiber for an empty queue, the drain
  after main and the bailout (S3.6a, S3.md section 5). Withdraws "no scheduler coroutine" of S3.md
  section 5 and the bailout to main's stack (U4 entry above). Why: Edmond; the drain on the OS stack
  had no frame and no place to wait.
- 2026-10-02 The scheduler coroutine is a Coroutine object, current while it runs, outside the
  registry, never queued; created by the first enqueue, defer or suspend that needs it, ended when it
  has drained the queue in a from_main call, or after a bailout. Why: the core expects a current coroutine while async is active
  (`zend_gc.c:2245` defers a GC only then); creating it at the end of main or in a bailout would
  allocate a stack where none can fail (Critic, Sage).
- 2026-10-02 A bailout unwinds every coroutine but main first and hands main the flag last, the
  core's ts.c order, not TrueAsync's main-first. Why: main's bailout may land in a `zend_try` that no
  from_main call follows, as the one around the last call (`main.c:1937-1945`), and the scheduler
  parked mid-walk is never resumed (Critic; restated in S3.19); not
  copied from TrueAsync either: the unchecked scheduler creation (`scheduler.c:1313`), the lost
  bailout result (`:1374`), no unwinding after a bailout on the scheduler's own stack (`:2026`).
- 2026-10-02 A stack that cannot be taken ends the request, as running out of memory does: the
  exception is reported as uncaught and the request bails out through the scheduler (`suspend()`
  hands it the bailout flag). It is the core's path for an exception thrown without a frame
  (`zend_throw_exception_internal`) minus its user handler, deliberately. `scheduler/005` keeps its
  fatal, `scheduler/007` expects it too; tests `scheduler/016`-`018`. Why: the Sage; finishing the
  coroutine unrun with that exception as its outcome, or a handler that lets the request go on,
  left a full GC root buffer full, and every GC coroutine started for it got no stack either: the
  request never ended. The Critic judged the 007 change justified for that reason.
- 2026-10-02 The record of a wait for one target lives in the waiting coroutine's waker
  (`waker.record`), as TrueAsync's inline callbacks of the waker, not on the waiting frame's stack
  (withdraws that part of D28). The record's `event` says whether it is linked: the waker's `wait`
  pointer and count and the coroutine's `awaiting_info` go (nothing in S3 used them; Critic), and the
  coroutine is 320 B, allocated as 304 in the 320 B bin, as before S3.7. A wait for several targets
  comes with S5, which decides its storage (TrueAsync's waker keeps two inline callbacks and a heap
  array). A bailout that unwinds the
  frame leaves the record intact and the coroutine's finish unlinks it, so no `zend_try` guards the
  window between the link and the switch (U5 of S3.md 4.4 goes). Why: P1.4; the stack record needed
  a `zend_try` around the tick and the context creation. Edmond agreed, 2026-10-02 ("Ок", after the
stack options were shown with the code).
- 2026-10-02 `async_wait_kind_t`, `ASYNC_CALLBACK_F_TYPED` and `F_COUNTED` go: no S3 wait has a
  typed unlink, an abort or an external count; the unlink removes a record from its coroutine
  target's vector, and the awaiting info is worded from the target. Events (S4) choose between kinds
  and TrueAsync's event methods (D25, open). Why: nothing in S3 reads them.
- 2026-10-02 A wait record that a throwing callback left in a finished coroutine's vector is
  detached and then fired by the teardown: the waiter wakes and reads the target's outcome, and the
  teardown loop ends whatever the wake does (Critic). TrueAsync's dispose detaches it,
  and the waiter stays parked until the deadlock report. Why: the target finished, so the outcome
  exists.
- 2026-10-02 The await slot returns false without an exception inside a Fiber the scheduler did not
  adopt (until S3.9), as in scheduler context: the GC collects later. The report of a stack that
  cannot be taken runs in scheduler context: its PHP code (`__toString`, a release that fills the GC
  buffer) ran a nested wait on a coroutine in the middle of its switch. Why: `gc/013`, `gc/014` and
  `scheduler/016` (an assertion) under the awaiting GC.
- 2026-10-02 `getAwaitingInfo()` lists the records of the coroutine's wait ("await: coroutine #N",
  the core's test_scheduler.c wording); the add and remove slots keep nothing (the RFC's 0, "the
  add did nothing"): nothing in the core adds one. TrueAsync returns nothing at all.
- 2026-10-02 `gc_collect_cycles()` in a coroutine waits for the whole run, destructors included,
  and a coroutine queued before the GC coroutine runs first: `gc/002-gc_destructor_spawn_coroutine.phpt`,
  `gc/007-gc_destructor_complex_async_ops.phpt`, `gc/011-gc_destructor_cycles_with_suspend.phpt` and
  `gc/012-gc_destructor_multiple_gc_cycles.phpt` expect that order (`changed:`; `gc/005` passes as
  written). `scheduler/016-no_stack_full_gc_buffer.phpt` expects the fatal on main's stack: main now
  parks at the collection, so its suspend starts the coroutine that gets no stack. Why: the RFC core collects in a coroutine and awaits it
  (`zend_gc.c:2245-2293`); the reference collected inline or returned 0 at once.
- 2026-10-02 A finished coroutine stays current while its finalize releases what it held, so a
  destructor run there could wait: `await()` refuses with "await() requires a running coroutine",
  `suspend()` refuses, and the GC's await slot returns false (the GC collects later). Not scheduler
  context for the whole finalize (the Critic's proposal): it would also refuse `spawn()` in such a
  destructor, which TrueAsync allows; TrueAsync's finalize sets no scheduler context. Test
  `scheduler/026`.
- 2026-10-02 `await()` marks the target's outcome observed before it waits, as TrueAsync does
  (`async.c:318-320`), not at delivery; the refusal inside a Fiber the scheduler did not adopt comes
  before the mark, so the target's exception still ends the request (`scheduler/027`). A waiter
  cancelled while it waits (S3.8) is that step's question.
- 2026-10-02 `await()` unlinks a record left linked before it links its own, as TrueAsync's
  `ZEND_ASYNC_WAKER_NEW` cleans a stale waker: a bailout that a shutdown function's `zend_try`
  caught can leave main's record linked (Sage). `Async\suspend()` refuses for a finished current
  coroutine before it reads the coroutine's context (Sage); `scheduler/026` covers it.
- 2026-10-02 The coroutine releases its arguments, result, outcome and contexts in `dtor_obj`, as
  TrueAsync's `coroutine_object_destroy` (`coroutine.c:164-242`), and throws an outcome nobody
  observed there when PHP code runs. This reverses "the arguments stay until the object dies"
  above, an S3.5 call of the Sage and the Critic: a destructor that the release ran in `free_obj`
  could take the finished coroutine again (`Async\current_coroutine()`), and the engine frees the
  block after `free_obj` whatever its refcount (use-after-free, the Sage in S3.7). `dtor_obj` has
  no FINISHED guard, as TrueAsync's: the store's destructor pass at shutdown skips coroutine
  objects. Tests `scheduler/032`, `033`, `038`.
- 2026-10-02 The outcome counts as observed when a waiter reads it (after the wait, or at once for
  a finished target), not when `await()` starts and not at the record's wake (TrueAsync's
  `zend_async_waker_callback_resolve` marks there). This replaces the entry "`await()` marks the
  target's outcome observed before it waits" above: a waiter cancelled before its target finished,
  or after the wake and before it ran, lost the target's exception, in TrueAsync too (Critic).
  Tests `scheduler/034`, `041`.
- 2026-10-02 One rule for a coroutine's pending error, TrueAsync's (`zend_async_API.c:1344-1367`
  and its resume): a cancellation keeps a pending cancellation and takes a pending plain error as
  its previous; a plain error drops a new cancellation and goes on top of the rest. Test
  `scheduler/037` through the hook `TrueAsync\Test\enqueue_with_error`.
- 2026-10-02 The graceful shutdown starts once per request (`ASYNC_G(graceful_shutdown)`; the core
  has no such flag) and its second call is ignored; an exit exception or an `exit()` after it
  re-cancels every coroutine, as TrueAsync's `finally_shutdown`. `exit()` in any coroutine starts
  it from finalize (D16); an `exit()` the tick folds is not added to the exit exception (it hid
  the one already there); the `shutdown` slot clears the exit, as TrueAsync's, since the pinned
  core handles that. The 5 s deadline stays S4's. Tests `scheduler/030`, `035`, `042`, `043`.
- 2026-10-02 A deadlock no longer ends the request with a fatal: as TrueAsync's
  `resolve_deadlocks`, a `DeadlockError` joins the exit exception and every coroutine is cancelled
  with its own "Deadlock detected" (a shared one would carry one coroutine's pending error into
  another's chain, Critic); it does not start the graceful shutdown. The report is composed and
  written once, so an output handler cannot run while the registry is walked; one that waits is
  refused (scheduler context). Test `scheduler/031`.
- 2026-10-02 A coroutine cancelled before it ran is finished where the run queue pops it, its
  cancellation as the outcome, instead of TrueAsync's skip at switch-in. Why: no stack is taken for
  a body that never runs. Its finalize runs outside scheduler context whoever pops it, and what its
  releases throw is folded there (Critic: it reached the next coroutine). Tests `scheduler/029`,
  `039`, `040`.
- 2026-10-02 The graceful shutdown that an exit exception starts cancels coroutines four own tests
  left yielding or parked to the end, so their fixtures changed with their expectations kept or
  extended: `scheduler/006-bailout_drops_exit_exception.phpt` and
  `scheduler/015-finalize_destructor_throws.phpt` spawn the coroutine that must still run first and
  catch the cancellation, `internal/019-microtask_tick.phpt` catches and prints it, and
  `internal/022-finish_handler_throws_before_waiter.phpt` parks two coroutines on each other so the
  teardown's wake shows ahead of the shutdown (a probe without the wake fails it).
- 2026-10-02 Two TrueAsync behaviours kept as they are (the Sage, S3.8): an unobserved exception of
  a coroutine that a global variable holds to the end is released silently, since its last
  release comes with no frame (`coroutine_object_destroy`'s `EG(current_execute_data)` test, as
  TrueAsync's `coroutine.c:219-230`); and the graceful shutdown hands one cancellation object to
  every coroutine, so pending errors of two coroutines share its previous chain
  (`cancel_queued_coroutines`), while the deadlock gives each its own. Edmond may want the first
  reported instead. (He did: the first is replaced by the entry of 2026-10-05.)
- 2026-10-02 Every Fiber is adopted (S3.9): `intercept_fiber` returns a new coroutine, as the
  core's test_scheduler.c. Why: a legacy fiber switches stacks the scheduler does not know about.
  The refusals of `suspend()` and `await()` inside an unadopted Fiber go, as TrueAsync has none;
  the own tests `scheduler/013-suspend_in_unadopted_fiber.phpt` and
  `scheduler/027-await_refused_in_fiber_keeps_exception.phpt` pinned that refusal and now pin the
  park and the await inside a Fiber (their names stay: the list is frozen).
- 2026-10-02 A coroutine's `extended_dispose` runs in `free_obj` only, as the core's
  test_scheduler.c, not in finalize as TrueAsync's. Why: the core's fiber dispose only forgets the
  coroutine and the Fiber holds a reference it releases when it goes; a call at the finish leaked
  every fiber coroutine.
- 2026-10-02 When every waiting coroutine is a fiber parked in `Fiber::suspend()`, each is closed
  with a graceful exit and no `DeadlockError` (TrueAsync's `resolve_deadlocks`); protection is
  cleared, as for a deadlock, which TrueAsync does not do there. Why: a protected fiber would defer
  the exit forever and the scheduler would loop.
- 2026-10-02 In the waker an exit object (an `exit()`, the graceful exit of a dropped Fiber) wins
  over any pending error and is never chained, unlike TrueAsync's
  `zend_async_waker_apply_error`. Why: `zend_exception_set_previous` adds a dynamic property to a
  graceful exit (a deprecation) or drops it, so a Fiber dropped while a `throw()` into it waited
  ran on (Critic, S3.8). Test `scheduler/046`.
- 2026-10-02 `fiber_entry` and `scheduler_fiber_entry` clear `EG(active_fiber)`, as they clear
  `EG(vm_stack)`. Why: a context entered for the first time inherits the switcher's active fiber
  (the core restores it only on a switch back), so a coroutine spawned in a Fiber's body saw that
  Fiber as `Fiber::getCurrent()`, and after a park a freed one (Critic). TrueAsync never sets the
  active fiber in coroutine mode; the core's test_scheduler.c has the same gap. Test
  `scheduler/047`.
- 2026-10-02 `Fiber::start()` in a destructor that a finished coroutine's release runs is refused
  in `intercept_fiber` with the core's FiberError "Cannot switch fibers in current execution
  context", as a wait there is since S3.7: the finished coroutine is still current and cannot park.
  Refused before a coroutine exists, so the Fiber stays unstarted (the Sage: a refusal at the park
  left the body queued with no caller, and a second `start()` ran it twice). Before S3.9 such a
  Fiber ran on the legacy path (Critic). `resume()` and `throw()` there are refused by the core's
  park and leave the body queued: a core gap (handoff). Test `scheduler/048`.
- 2026-10-02 A fiber left suspended when main ends is closed in the drain after main, before the
  shutdown functions, as in TrueAsync (the fork drains at the same place, main.c:2625 on
  `863f6dd90cf`); plain PHP leaves
  it for a shutdown function to resume (Critic). Test `scheduler/049`.
- 2026-10-02 The leave switch handlers run before the tick, as TrueAsync (`scheduler.c:1708-1711`),
  not at the switch as S3.md 4.2 drew them. Why: the core's shutdown destructors queue the coroutine
  that carries their pass on in the leave handler; at the switch, a destructor that only yields
  with nobody else queued never switches and yields to itself forever. Test `scheduler/050`.
- 2026-10-02 The bailout walk runs in a `zend_try` of its own and goes on after a bailout out of a
  finalize on the scheduler's stack; TrueAsync's walk gives up there and leaves the rest to its
  dtor. Why: our walk also runs after the scheduler's `zend_catch`, where a second bailout had no
  address and ended the process. Test `internal/025`.
- 2026-10-02 RSHUTDOWN destroys the context of a coroutine a bailout left parked without unwinding
  it, and releases the registry's reference, as TrueAsync's dtor releases its leftovers. Why: no
  code may run there, and the request reports no leaks after a bailout. Test `internal/024`.
- 2026-10-03 Finalize takes the coroutine out of the registry as it sets FINISHED, before its
  handlers; TrueAsync deletes it after them. Why: a bailout out of a handler left a finished
  coroutine in the registry, and a later drain in the request counted it as a waiter forever
  (Critic). A PHP-level finish handler (a later step) will not see its own coroutine in
  `Async\get_coroutines()`, unlike TrueAsync (the Sage). Test `internal/026`.
- 2026-10-03 O6 taken: spawn's `zend_fcall_t` lives in the coroutine (`spawn_fcall`; 424 B, the
  448 B bin), a Fiber's coroutine keeps the core's block. Why: D17's condition met, B1 -4.8 %
  instructions and one allocation less per spawn, the unbatched run -4.9 %; its wall time is 5.8 %
  slower (16 B more fresh memory per live coroutine), recorded in `dev/BENCHMARKS.md`. A Fiber's
  coroutine, main and the scheduler carry the unused block (+128 B each); a second layout for them
  would be machinery for bytes (the Sage).
- 2026-10-03 The context pool keeps TrueAsync's rule with a floor of 1024 for its 4 (D23 changed by
  its own condition). Why: B4 -20.7 % instructions per link at depth 100 and -2.2 % at 10 000, B5
  -11.5 % and a tenth of the wall time; a cap alone lost to the rule on B5 (+9.7 % at 128); the
  cost is about 20 KiB of resident stack per pooled context, kept until the scheduler ends; the
  Critic's trim when the loop goes idle was rejected as new machinery that re-maps every burst
  (the Sage). The pool buffer starts empty and never shrinks, as the run queue.
- 2026-10-03 Every context starts its VM stack with the first page on its C stack, as TrueAsync's
  `fiber_entry` (only the scheduler coroutine did). Why: the core's `zend_fiber_vm_stack_start`
  took 16 KiB of the request's memory per live coroutine (10 000 parked: 168 MiB, the reference
  14 MiB, and B4 at that depth hit the default memory_limit). Test `scheduler/055`.
- 2026-10-03 The callbacks vector keeps its inline element. Why: a plain heap vector costs one
  allocation and 1.9 % per B4 link, and on B5 it allocates more for 0.6 % fewer instructions.
- 2026-10-03 D31 unchanged, the hot state stays in extension globals. Why: the shared build's
  extra cost (9 `__tls_get_addr` calls per suspend) comes from the core's per-module TLS cache,
  through which the extension reaches the core's globals too.
- 2026-10-03 A coroutine context's stack is `fiber.stack_size` plus the 16 KiB VM page that
  `fiber_entry` keeps on it, so the ini value stays the C budget the core's stack limit measures.
  Why: with the page on the stack a size up to about 20 KiB overran into the guard page (SIGSEGV;
  TrueAsync crashes the same way) where a Fiber throws the stack-limit Error (Critic). Test
  `scheduler/056`. `fiber.stack_size=1` is now a valid size, so the no-stack tests take 64G, which
  mmap refuses, set after the spawn that creates the scheduler where the scheduler did not exist
  yet (Critic and Sage): `scheduler/005-no_stack_for_coroutine.phpt`,
  `scheduler/007-no_stack_with_exception_handler.phpt`, `scheduler/016-no_stack_full_gc_buffer.phpt`,
  `scheduler/017-no_stack_in_shutdown_suspend.phpt`, `scheduler/018-no_stack_in_coroutine_suspend.phpt`.
- 2026-10-03 The fuzz hook is TrueAsync's (`internal/fuzz.{h,c}`, a swap of the run queue's head
  before each pop) with its variable `TRUE_ASYNC_SCHED=random:<seed>`, not the plan's
  `TRUE_ASYNC_SEED`; only its FIFO and random modes, without the PCT placeholder, the second seed
  variable and the verbose line. Why: P1.4; one variable drives both extensions.
- 2026-10-03 Fault injection is three sites the test hook `fail_at()` arms (`enqueue`, `reserve`,
  `link`), each raising the fatal error running out of memory raises; a test per unlink site U1-U6
  picks the site and the moment. Why: with the record in the waker (S3.7) the sites of S3.md 4.4
  are the moments a bailout meets a half-made wait, and a hook needs no `memory_limit`, so the tests
  run under asan, where the `bailout/` tests skip.
- 2026-10-03 A seed run fails a test only by a crash, an assertion, a sanitizer report, a leak or a
  timeout; an output with the expected lines in another order passes, and one that differs
  otherwise is listed. Why: S3.12's Done when; a random order changes what order-dependent tests
  print (which coroutine reports first, whether a target finished).
- 2026-10-03 A coroutine woken in its own tick while its `suspend()` pops the next coroutine (U2:
  the pop finishes a coroutine cancelled before it ran, whose release starts the graceful shutdown)
  runs on, and the popped coroutine goes back to the front of the queue. Why: it switched into the
  popped one and stayed RUNNING with no context to come back to, and the deadlock resolution looped;
  found by seed 37, reproduced without the hook. Test `scheduler/057`.
- 2026-10-03 Outside scheduler context the enqueue refuses a wake with an error of the running
  current coroutine, before the error is applied; its own enqueue with no error is the yield of `Async\suspend()`. A coroutine
  woken in its own pop, RUNNING and not current, takes a wake as it takes a cancel: its `suspend()`
  throws the error. Why: TrueAsync's `async_coroutine_resume` refuses a coroutine that is not
  suspended and takes one after its short path; an accepted wake of the running coroutine left it in
  the queue after it finished, and the drain switched into it (a heap corruption, found in S3.13);
  refusing the woken one broke the GC's own wake (Critic). In scheduler context the short path takes a
  wake of the current coroutine and pushes nothing, as TrueAsync's refusal is also outside it (the
  Sage). Tests
  `internal/043`, `047`, `scheduler/066`.
- 2026-10-03 A coroutine woken with any error before it ran finishes with that error at the pop, the
  error thrown when the stack has a frame, so finalize's clear restores the opline of a frame parked
  mid-opline, and stored when it has none (shutdown destructors), where a throw ends the request; `scheduler_suspend`
  throws a waker's error by the same rule.
  Why: the core's STARTED contract, test_scheduler.c and TrueAsync skip the body; the stored error
  corrupted parked main, the thrown one bailed out with no frame (Critic in S3.13). Tests
  `scheduler/060`-`062`, `074`, `076`.
- 2026-10-03 The scheduler coroutine is current for the tick and the pop after a coroutine's body,
  and the cancelling walks over the registry hold a hash iterator and take at most the coroutines
  present at their start, as TrueAsync's foreach. Why: with no current coroutine a
  collection there did not defer; a cancel may start the GC coroutine, whose insert resized the
  table under the walk (Critic in S3.13, an ASAN use-after-free). Tests `scheduler/058`, `059`.
- 2026-10-03 Mull survivors on the stage diff are explained in `dev/plans/S3.md` section 9, not
  marked in the source with `mull-off`. Why: D34 asks for killed or explained, and
  the next Mull run mutates only the lines its stage changes, so a marker in the source has no reader;
  the circular buffer's survivors go with the code S3.17 deletes (the Sage).
- 2026-10-03 A suspend leaves its EH_THROW window behind for what it runs on its own stack (switch
  handlers, tick, pop) and takes it back on return. Why: the core's switch does the same, and a
  warning of another coroutine became this one's exception class (S3.14, `scheduler/077`). An `@`
  it suspends inside is left the same way: that code gets the INI error_reporting, read only when
  the mask is silenced (`scheduler/078`; the Sage).
- 2026-10-03 A main that calls from_main parked is ended as a bailout. Why: only a bailout on its
  stack that a core zend_try caught leaves it parked, and a plain end freed it in the queue (S3.14).
- 2026-10-03 `spawn()` keeps the references of its callable's cache and drops them with the
  callable after the run. Why: a class-string callable's `$this` lives only there, and a `__call`
  trampoline does not resolve again from the name (S3.14, `spawn/021`).
- 2026-10-03 `scheduler/065-no_stack_refusals.phpt` expects `Fiber stack allocate failed: %s`, as the
  other no-stack tests. Why: its "mmap failed" names the POSIX allocator, and Windows reports
  VirtualAlloc (S3.16, Critic).
- 2026-10-03 The circular buffer keeps only what S3 calls (S3.17): beyond the plan's list, `pop`,
  `capacity`, the refusing push, the ctor's count, shrinking and the request allocator's indirection
  (`allocator.{c,h}` deleted) go too, being of the same class (only the test hooks reached them; no
  queue ever shrank); `clean` stays, its caller is the bailout walk (`scheduler.c`, S3.6a). Tests
  `internal/012-buffer_wrap_grow.phpt`, `internal/013-buffer_push_front.phpt`,
  `internal/014-buffer_shrink.phpt` (now the growth from tail 0, name kept as the lists are frozen),
  `internal/015-buffer_ptr.phpt` (a second swap) and `internal/016-buffer_front_full.phpt` changed
  to pointer items and the scheduler's calls. Why: HEALTH 2026-10-02 finding 7 and DECISIONS
  2026-10-01 ("what S3 does not need is cut"); the Sage.
- 2026-10-03 The deadlock report prints only where the error it explains shows: `display_errors`
  on (Edmond), `error_reporting` with `E_ERROR`, stderr for `display_errors=stderr` as php_error_cb.
  Why: it names the script path of every coroutine, which a production response must not show
  (`scheduler/083`-`085`).
- 2026-10-03 A coroutine is in the registry from its creation, not from its first enqueue (S3.18).
  Why: the core releases only its own reference to a coroutine it creates and fails to enqueue (a
  Fiber's; the GC takes none), so ours leaked; test_scheduler.c's live table owns a coroutine from birth
  (`ts_coroutine_new`). The S3.14 thread's call, 2026-10-03. Such a refused coroutine is no waiter:
  the deadlock count and report and `get_coroutines()` skip CREATED entries, as test_scheduler.c
  counts only suspended ones; counted, it made a later drain report a false deadlock (Critic).
- 2026-10-03 The core's finish handler contract reads "fires at most once; a handler that throws may
  end the notify", so a handler that must fire belongs on a coroutine whose handlers its owner
  controls, as the GC's do (S3.18). Why: test_scheduler.c calls every handler and ours stops at the
  first throw (Edmond, "пока как в trueasync"); "may" covers both. The Critic asked for "exactly
  once" or "a handler must not throw"; the Sage kept "may": the first overturns Edmond's call, the
  second is a rule neither provider enforces.
- 2026-10-03 The new_coroutine slot loses `extra_size`, and `active_coroutine_count`,
  `call_on_main_stack`, `coroutine_from_object`, the OBJ_REF object model,
  `ZEND_ASYNC_GET_EXCEPTION_CE` and `zend_async_is_enabled()` go (S3.18; API version 20261003, moved
  the same day by the S3.14 Fiber fix). Why: no caller, no scheduler honoured them, no RFC text
  (HEALTH 2026-10-02, finding 8).
- 2026-10-03 test_scheduler gets a fault seam: INI `test_scheduler.fail_new_coroutine` and
  `test_scheduler.fail_enqueue` (n-th call from now fails) (S3.18). Why: the core's paths for a
  coroutine it cannot create or queue had no test (HEALTH finding 6); the seam found a crash
  (`ts_suspend` with no loop started), fixed in test_scheduler.c.
- 2026-10-05 After a fatal error (a bailout) no queued coroutine runs, in the script, a shutdown
  function or a shutdown destructor (`exit()` or an uncaught exception there bails out in plain PHP
  too). Core `ab94befe389`: `shutdown_destructors()` re-raises the bailout, ending its iterator
  coroutine, and `zend_call_destructors()` returns it to the last from_main call, as `c43060ea12d`
  does for shutdown functions (`scheduler/093`-`095`, RFC `22f5c49`). Why: Edmond, after the Critic:
  PHP runs no user code after a fatal error but shutdown functions, and every object is marked
  destructed first. An uncaught exception and `exit()` in the script keep the graceful shutdown.
  Rejected: `finally` blocks after a fatal error (the core's `fatal-error-with-multiple-fibers.phpt`,
  an OOM repeats); a cancellation rethrown at every later suspend (Edmond: "очень очень плохая
  идея"). TrueAsync runs the queue on in the shutdown phase: a departure from P1.4.
- 2026-10-05 An exception nobody observed, of a coroutine alive at the request's end, is printed as
  uncaught (Edmond: "да печатать", over TrueAsync's silence), once per object. After any bailout of
  the request (`CG(unclean_shutdown)`: a fatal error, an `exit()` in the shutdown phase) it is printed
  by the built-in `Exception::__toString()`: the class's own would run unbounded after a timeout
  (Edmond: "печатал хотя бы то, что они были но __string не вызывал"; `scheduler/096`-`102`;
  `error_get_last()` was rejected as the test, PHP code overwrites and clears it). Released without a
  frame, it goes into `ASYNC_G(unobserved_exceptions)` (by handle); the last from_main call, or
  RSHUTDOWN after a bailout in the shutdown phase, adds the rest by a walk of the object store and
  prints each in `zend_try`, `set_exception_handler()`'s handler set aside. Nothing is
  cancelled; exit status 255; `getException()` observes. Why: the error vanished with exit status 0
  (`scheduler/088`-`092`). Rejected: chaining into the exit exception (`zend_exception_set_previous`
  drops one already in the chain; waiters share one object); a table of the coroutines.
- 2026-10-05 Main is re-minted at every from_main call, as before. Rejected: keeping main when
  nothing ran (efficiency report 2026-10-03): the call after the destructors re-mints it anyway
  (the core's destructors give main a switch handler), and a shutdown function awaiting the
  script's main deadlocked, or not, by whether a GC run or a `defer` came first (the Critic).
- 2026-10-05 Health fixes of the extension (S3.19): the circular buffer holds pointers only, with no
  `count` for a buffer never constructed (every queue is constructed at RINIT);
  `internal/017-buffer_zeroed.phpt` keeps its name and counts a wrapped buffer (frozen lists, as
  `014` in S3.17); the `gc_new_coroutine`
  slot stays NULL and the core takes the GC's coroutines from `new_coroutine`, as our copy of it
  did; `CompositeException`'s add lost `transfer` and is static with its class entry;
  `async_callbacks_add()` is `test_callbacks_add()` in `test_hooks.c`. Why: every buffer held pointers, the slot copied
  the fallback, the rest had no caller outside (HEALTH 2026-10-05, pass 8).
- 2026-10-05 `CompositeException::addException()` throws `zend_cannot_add_element()`'s Error, as
  `$array[] =`, when the list's next key is taken (reflection or `unserialize()` can set
  `PHP_INT_MAX`); it asserted in a debug build and added a second bucket under the same key in a
  release one (`classes/010`). TrueAsync warns with E_CORE_WARNING
  and drops the exception: a departure (P1.4), since the user's call should fail as PHP's own does.
- 2026-10-05 P1.4 is departed from when the code gets better and is correct; the entry says how it
  is better and names the test or review that checked it; Edmond's word is needed only for a
  `zend_try` or a global counter on a hot path. Why: Edmond: "if the code got better and is
  correct, that is enough" (health check 2026-10-05, pass 9).
- 2026-10-05 The core loses the cancel slot's `is_safely`, the `gc_new_coroutine` slot, the
  object-less coroutine and the `IS_OFF`, `IS_READY`, `CLASS_NO` and context aliases (S3.20, API
  20261005). Why: no caller, no scheduler honoured them (HEALTH 2026-10-05, pass 8).
- 2026-10-05 The core's async objects-store pass is its own iterator entry, as
  `shutdown_destructors` is; TrueAsync has a forwarder (P1.4: one function less; the Critic, the
  Sage, both core trees equal per test). S9's scopes pass TrueAsync's `is_safely` to our own cancel.
- 2026-10-05 `ZEND_ASYNC_DEACTIVATE` clears the scheduler-context flag (S3.20). Why: a bailout left
  it set for the next request (HEALTH 2026-10-05, pass 9).
- 2026-10-05 Kept in the core for the bridge `true-async/ext-scheduler-hook`, which calls them:
  `zend_async_get_scheduler_module()`, `zend_async_scheduler_unregister()`,
  `ZEND_ASYNC_SCHEDULER_LAUNCH()`, the fiber VM-stack helpers (S3.20). Why: the health check counted
  two providers only; Edmond's answer is open (PLAN "Open questions").
- 2026-10-05 The core API removed in S3.18 and S3.20 for having no caller comes back (S3.21): one
  revert per removing commit on `async-core`; the fixes those steps carried stay (the NULL check of
  `ZEND_ASYNC_NEW_COROUTINE()`, the comment naming the bridge as a caller of
  `zend_async_scheduler_unregister()`). Why: Edmond, "RFC создаётся для многих API, функции в нём
  потенциально могут быть кем-то использованы" (P1.5). This supersedes the removals of the
  2026-10-03 (S3.18) and 2026-10-05 (S3.20) entries above.
- 2026-10-05 The core API the bridge `ext-scheduler-hook` calls stays (`zend_async_get_scheduler_module()`,
  `zend_async_scheduler_unregister()`, `ZEND_ASYNC_SCHEDULER_LAUNCH()`, the fiber VM-stack
  helpers). Why: Edmond's question was withdrawn: the core keeps them (P1.5).
- 2026-10-05 The bridge fills the API `version` field. Why: the core refuses a provider whose
  version differs, and the bridge left it 0.
- 2026-10-05 `ZEND_ASYNC_API_VERSION` is a counter raised by one at every incompatible change (1
  from S3.21), not a date. Why: S3.18 and S3.20 shipped different layouts under one date each, and
  the core would read an older provider's slots shifted; PHP and Python keep one number too
  (Edmond's "да делай"). Rejected: major.minor (the `size` check covers appended slots).
- 2026-10-05 The core's `new_coroutine` slot has no `extra_size` and `ZEND_ASYNC_NEW_COROUTINE_EX`
  is gone (S3.22, API version 2). Why: the caller asked for bytes it could not find (the API did not
  say where they live, and a provider places its own structure around the coroutine), no provider
  honoured the size and no caller passed one; TrueAsync's slot has no such parameter. Edmond's word
  on these exact names ("тогда удали его и у RFC в том числе"), as P1.5 requires.
- 2026-10-05 The core launches the scheduler only in a request its provider marked READY
  (`ZEND_ASYNC_INITIALIZE` in RINIT, or right before a launch at run time); registering no longer
  sets READY (S3.22). Why: TrueAsync's rule (fork core `zend_fibers.c:1296`); the bridge
  `ext-scheduler-hook` registers its C slots once per process and its PHP handlers per request, and
  failed from the second request on. Edmond: the bridge's registration lives for the process,
  PHP code registers again in every request.
- 2026-10-05 The cancel slot's `is_safely` is TrueAsync's: a started coroutine becomes a zombie and
  runs to its end, the error is released; `ZEND_ASYNC_CANCEL_EX` passes it (S3.22). Why: the core's
  comment called it a deferred delivery, which no scheduler implements (Edmond: "да это неточность").
- 2026-10-05 The core's `active_coroutine_count` global goes; the appended `get_coroutine_count`
  slot asks the scheduler instead, 0 when none provides it; ours and test_scheduler count every
  unfinished coroutine of the request, main included (S3.22). Why: nothing kept the global up to
  date, so it always read 0; Edmond: "убери, но тогда взамен добавь функцию-слот".
- 2026-10-05 Every coroutine has a zend_object: the core's object-less branches go and the fiber
  code takes its reference with `ZEND_COROUTINE_ADD_REF()` (S3.22). Why: Edmond, "корутины без
  объекта на самом деле быть не должно"; `ADD_REF`, which `new_coroutine` tells callers to take,
  asserted on such a coroutine.
- 2026-10-05 `call_on_main_stack` stays in the core, and our scheduler gets TrueAsync's
  implementation (S3.23). Why: Edmond, it exists for Java and mobile embeddings ("да оно нужно!").
- 2026-10-05 Our `call_on_main_stack` takes TrueAsync's early-out (no current coroutine, or main)
  and then compares the running fiber context with the OS stack's: main's context copy, or the
  engine's context after main finished (S3.23). Why: our main finishes before the queue drains and
  the scheduler coroutine is current while the switch into it still runs on the OS stack, where
  TrueAsync's test would hop onto a stale handle; after the core turns async off the context still
  names main's copy, so the early-out stays (Critic found the crash, Sage kept both).
- 2026-10-05 The stack switch is built only where the compiler has `naked` and the ABI is SysV
  x86-64 or AArch64, and not on a ucontext core (S3.23). Why: GCC 13 ignores `naked` on AArch64 and
  the asm returns with its prologue's stack; clang-built Android keeps the switch; a ucontext
  handle is no stack pointer.
- 2026-10-05 S4, S5 and S6 run as parallel threads, gated on results on `main`, with the rules of
  `dev/PLAN.md` "Parallel tracks". Why: tests are long (Edmond), and Futures and `await_*` need no
  reactor (87 of 97 `component:S5` tests call neither `delay()` nor `timeout()`).
- 2026-10-05 S4 owns the whole wait-record layer, the storage of a wait of several records included
  (moved from S5, entry of 2026-10-02); S5 states its needs and writes its own kinds. Why: D25
  (events) is S4's, and two owners of one struct merge without a conflict into a missed count.
- 2026-10-05 The HTTP fixture is TrueAsync's per-test server (`tests/common/http_server.php`), with
  `PHP_CLI_SERVER_WORKERS=4` that `tools/test.py` gives every test, not one server per run (S6.1).
  Why: the curl, cleanup and stream tests S6 ports start their own server through that helper or
  php-src's `php_cli_server.inc`; a server per run would serve the smoke test alone.
- 2026-10-05 MySQL in CI is TrueAsync's `mysql:8.3` service; locally `tools/test.py` starts a private
  `mysqld`, only for a run with `mysqli` or `pdo_mysql` tests and no `MYSQL_TEST_HOST` (S6.1). Why: a
  local run has no service, and a run of other groups should neither need MySQL installed nor wait
  for it.
- 2026-10-05 `iterate()` moves from S5 to S9 with Scope and the iterator. Why: `$cancelPending`
  cancels the coroutines its callable spawned, which needs a Scope, and `$concurrency` runs workers
  (`iterate/012`, `013`, `016`-`018`).
- 2026-10-05 Future chains run in one drain coroutine per completed source over a FIFO, with
  TrueAsync's helper microtask that adds a coroutine when a mapper waits (`dev/plans/S5.md` 3). Why:
  a notify never calls user code; one coroutine for any fan-out where one per child would allocate
  above the reference (D2); breadth-first order as TrueAsync's iterator (the Critic, the Sage).
- 2026-10-05 A mapper's error goes only into its child, and the child's release reports it when
  nobody observed it. Why: TrueAsync cancels the `map()` caller's scope on a pending source and ends
  the mapper coroutine uncaught on a completed one; no ported test covers either (the Sage).
- 2026-10-05 The future event's result, exception and chain are reported to the GC only by an object
  holding its only reference, and `new Future($state)` holds the `FutureState` object. Why:
  TrueAsync reports them from every `Future`, so two `Future` objects on one state make the collector
  free a live result (by reading); a user-made event then has one holder, which collects its cycles.
- 2026-10-05 A Future is marked used and its exception caught when `await()` or `await_*` takes it,
  as TrueAsync (`async.c:319-320`); a coroutine keeps S3's mark when its waiter reads the outcome.
  Why: `future/004`, `012`, `035` expect no "Unhandled exception in Future" warning (the Critic).
- 2026-10-05 D25 is decided as D28 for S4 and S5: per-type behaviour sits on the record's kind
  (`async_wait_kind_t`, a union with the record's unused `dispose`), events carry no methods
  (`dev/plans/S4.md` 2.1). Why: D28 (Edmond), and the code that links a wait knows its target's type.
- 2026-10-05 A wait keeps two records inline in the waker and points at a block its stage owns past
  two, unlinked through the block's `ops->unlink` and released after the waiter reads it (S4.md 2.2).
  Why: S5's N3-N5; one allocation per wait where TrueAsync makes one per record past two, and S5's
  `await_*` context lives in the same block (the Sage).
- 2026-10-05 Ops are armed after the last record of a wait is linked, and an op completed at submit
  is completed before the park (S4.md 2.3). Why: a completion inside the first arm would end the wait
  while later records were not linked (the Sage).
- 2026-10-05 Deadlock is decided from the reactor's `waits` and `triggers` lists, not from a counter
  like TrueAsync's `ZEND_ASYNC_ACTIVE_EVENT_COUNT`; `F_COUNTED` stays out (S4.md 3.4). Why: RSHUTDOWN
  and the fork rebuild need the waits themselves; a link costs what an increment costs and never runs
  on the notify path (the Sage).
- 2026-10-05 The reactor's IO events, the `delay()` Timer included, live on the heap, owned by their
  records, as TrueAsync's timer (S4.md 3.2). Why: a bailout unwinds a frame before U4 or U6 unlinks
  (the Critic).
- 2026-10-05 After `fork()` the reactor rebuilds its queue and `NotifyHandle` when the queue answers
  `EPERM`, and the parent's waits end in the child's deadlock report instead of being resubmitted
  (S4.md 3.1). Why: the RFC core has no fork hook to refuse the fork as TrueAsync does; a wait must
  not run twice, and the core's answer for a child is that nothing of the parent completes (the Sage).
  The `NotifyHandle` part replaced by the 2026-10-06 S4.5 entries: own descriptors, trigger waiters
  cancelled, `own` resubmitted.
- 2026-10-05 With `EG(vm_interrupt)` set and nothing runnable, the idle wait starts one internal
  coroutine that runs the VM's interrupt (S4.md 3.3). Why: the queue returns `EINTR` and no opcode
  would run the interrupt (review M5); a pcntl handler may then wait.
- 2026-10-05 Cross-thread wakeup uses the core's `Io\Poll\NotifyHandle`, its class found by name,
  one per thread (S4.md 3.6). Why: the core has no wakeup op; its class entry is static; a C
  constructor is the RFC request of S4.5 (the Critic). Replaced by the 2026-10-06 entry on the
  reactor's own descriptor pair.
- 2026-10-05 `delay()` with a negative value throws `ValueError` (S4.md 1). Why: TrueAsync casts it to
  an unsigned value, about 49 days; no test relies on it.
- 2026-10-05 `edge_cases/016` and `017` stay in `S3.excluded` until S4.4 builds the core with zlib.
  Why: they need zlib, and a listed test may not skip.
- 2026-10-05 The waiter takes its wait's block with `async_wait_take_block()` as its `suspend()`
  returns and releases it itself; `async_wait_end()` only ends a wait a bailout cut short (S4.2).
  Why: a wait started while the waiter reads its outcome (a destructor's `await()`) would release the
  block under it (the Critic).
- 2026-10-05 A coroutine that finishes with a record still linked aborts its wait first, as the
  bailout's transfer (U4) and the request's end do (S4.2). Why: its frame never runs again (the
  Critic).
- 2026-10-05 A target's teardown unlinks a record left in its vector through the record's kind (S4.2).
  Why: a typed kind's unlink is where a TIMEOUT disarms (the Critic).
- 2026-10-05 The layer is tested through `TrueAsync\Test\Event`, a one-shot event behind a reference
  prefix, in the test hooks (S4.2). Why: no event type of the extension exists before S4.4 and S5.2.
- 2026-10-05 The reactor keeps the pid that created its queue and rebuilds at the first submit in
  another process, besides the rebuild when the wait answers `EPERM` (S4.3). Why: the Poll queue
  takes a Timer op in a child without a complaint and answers `EPERM` only at the wait, whose
  rebuild would drop the child's own wait with the parent's; one `getpid()` per submit is what the
  core's own queue checks per call (`main/io/php_io_hooks.c:724-747`) and the Ring per submit and
  wait (the Critic, the Sage).
- 2026-10-05 The interrupt stays in a coroutine, not in the scheduler's loop (S4.3). Why: a handler
  run in scheduler context could neither `spawn()` nor start the graceful shutdown, so a SIGTERM
  handler would work while a coroutine runs and fail while all wait (the Sage); the masked-signal
  wait inside a handler is the same on both paths and goes to Edmond as an open question.
- 2026-10-05 An exception a pcntl handler throws while every coroutine waits ends the request as an
  unobserved coroutine outcome (S4.3, departure). Why: no frame of the script is running to take it;
  handing it to main's wait (the Critic's proposal) would pick one waiter by convention.
- 2026-10-06 The IO provider submits a heap copy of the core's op in an IO event and copies the
  result and `in_flight` back, instead of M12's `zend_try` around the park (S6.md 3.2). Why: a
  bailout out of a parked coroutine's tick unwinds `run()`'s frame before the finalize orphans the
  op; TrueAsync keeps everything its reactor touches in heap events and uses no `zend_try` there,
  and a hot-path `zend_try` needs Edmond's word.
- 2026-10-06 The provider is installed at the first coroutine other than main or at the reactor
  queue's creation, not at the scheduler's launch (S6.md 2). Why: the core launches the scheduler
  before every script, and TrueAsync turns its IO on lazily with the first spawn, await, delay,
  timeout or signal; installing at spawn only would leave `Async\signal()` followed by `fgets()` in
  main blocking (the Critic, the Sage).
- 2026-10-06 An op that completed before its coroutine was woken with an exception returns SUCCESS
  with its result and leaves the exception pending (S6.md 3.3, departure). Why: after delivery the
  result belongs to the provider and the core frees nothing on FAILURE, so a FAILURE would leak an
  accepted descriptor or an address list; every caller of the wrappers was checked safe with a
  pending exception. A core request makes `php_io_run_ex()` free it (the Sage).
- 2026-10-06 `PHP_IO_HOOKS_F_FILES` stays off: regular-file IO runs on the thread (S6.md 6,
  departure). Why: a cancelled Ring read loses the bytes the backend read until the core's
  commit-on-settle (review B3), which is worse than a coroutine holding the thread on a disk read (the
  Sage).
- 2026-10-06 `Async\signal()` works in the one PHP thread of the process; `signal/008`, `009` and
  `012` are excluded for S10 (S6.md 8). Why: the core's `SignalHandle` masks the signal per thread
  and is refused outside the CLI under ZTS; delivery to several PHP threads needs a process-wide
  owner the core does not have.
- 2026-10-06 Windows `proc_open()` pipes become overlapped named pipes in a core commit of S6.3,
  served with and without a provider (S6.md 9). Why: IOCP completes only overlapped handles, and the
  core's Poll queue cannot poll a Windows pipe, so the no-provider path needs the overlapped read as
  well (the Sage).
- 2026-10-06 `tools/test.py` runs the tests with `-d opcache.jit=off` (S6.2). Why: nine reference
  `exec/` tests skip unless the setting reads `0` or `off`, and the core's default `disable` is the
  same setting.
- 2026-10-06 The chain drain arms its helper microtask before each mapper call that leaves items
  behind it, instead of once per coroutine entry as TrueAsync's iterator (S5.md section 3). Why: a
  resumed mapper's completion queues items after the tick found the FIFO empty, and a mapper that
  then awaits a sibling behind it deadlocked (the Critic, `future/111`).
- 2026-10-06 An `exit()` in a mapper stops the drain: no further mapper runs and the child stays
  pending (S5.md section 3). Why: TrueAsync rejects the child with the exit object, and the
  scheduler's exit cancellation did not reach a helper spawned after it (the Critic, `future/107`).
- 2026-10-06 `Future` and `FutureState` refuse `clone`; a second `Future::__construct()` releases
  what the first gave (S5.md section 8, item 8). Why: TrueAsync's clone gives a `Future` with no
  event, and its second `__construct()` leaks a reference.
- 2026-10-06 The collector of S7 finds stuck coroutines by reachability, PHP's trial deletion without
  the freeing plus Go's rule that a parked stack counts once a wait target is live (S7.md 2-3). Why:
  completers cannot be enumerated (a `FutureState` does not know its holders), and PHP has no list of
  roots; an unknown holder then hides a finding and never invents one.
- 2026-10-06 The collector takes outside sources from the reactor's `waits` and `triggers` lists, which
  decide the global deadlock; a source completing another awaitable (`signal()`'s handle) reports
  it, and a scope holds or reports the coroutines it may cancel; no "completed from outside" flag
  (S7.md 3.4). Why: a flag kept only for the collector can be forgotten with nothing else failing,
  while a source missing from those lists already fails the deadlock tests (the Critic).
- 2026-10-06 A partial deadlock is reported with one warning per coroutine by default; the setting
  `cancel` cancels the stuck coroutines with `AsyncCancellation("Deadlock detected")`, not the plan's
  `DeadlockError` (S7.md 6, departure from the plan's goal text). Why: TrueAsync leaves them until the
  global deadlock and a server keeps serving (P2.2); an uncaught `DeadlockError` in a coroutine nobody
  holds becomes the exit exception and would stop the server, while the global deadlock already gives
  each waiter a cancellation (the Critic).
- 2026-10-06 The collector runs at the idle point every `true_async.partial_deadlock_interval` ms,
  doubling after empty runs up to 64 times, and on demand; not with PHP's collector, not at a park
  (S7.md 5). Why: PHP's collector runs on its root buffer's fill, which says nothing about waits; a
  park is the hot path; the back-off is PHP's own threshold rule.
- 2026-10-06 `delay($ms)` with `$ms < 0` throws `ValueError` (S4.4, departure). Why: TrueAsync casts
  it to an unsigned value and sleeps about 49 days; no reference test passes a negative value, and a
  negative sleep is a caller's bug. Checked by `reactor/015`.
- 2026-10-06 A delay past the clock's range waits on the latest finite deadline (S4.4). Why: the core
  saturates it to an infinite deadline, which the Ring refuses for a Timer; TrueAsync saturates its
  libuv timer too. Checked by `reactor/022`.
- 2026-10-06 `delay()` whose Timer the queue completes at the submit still yields (S4.4). Why:
  TrueAsync's `delay()` always parks, so the coroutines queued before it run first.
- 2026-10-06 `delay()` with async off (no current coroutine) returns at once (S4.4), as TrueAsync
  with no current coroutine. Where TrueAsync would start its scheduler (`php -r`) nothing can start
  ours. Checked by `reactor/024`.
- 2026-10-06 D16's Timer sits on the reactor's own list, is armed by every start of an exit's
  graceful shutdown when a coroutine that ran is left, fires again every 100 ms while coroutines
  remain, and is withdrawn when the drain ends (S4.4). Why: a Timer on the waits list would keep a
  deadlock unresolved for 5 s; the refire is TrueAsync's `finally_shutdown` shape and covers a
  finally that waits or spawns without a check on the suspend or spawn path (the Sage, over the
  Critic's refusal of the suspend); a shutdown function's wait after the drain is not the drain's.
  Checked by `reactor/018`-`021`, `025`.
- 2026-10-06 `protect()` drops a cancellation it deferred when an exit unwinds its closure (S4.4).
  Why: chained under the cancellation, the exit object is released and a `catch` around `protect()`
  stops D16's unwind (the Critic). Checked by `reactor/023`.
- 2026-10-06 `gc/020` and `gc/023` keep `--XFAIL--`, now naming the pinned core's awaited collection
  instead of `delay()` (S4.4). Why: both assume the fork's deferred collection and its recorded
  threshold adjustment; with `delay()` they fail on the threshold. An S8 change-request candidate.
- 2026-10-06 The Windows build needs no `--with-zlib` (S4.4). Why: zlib is enabled by default
  (`ext/zlib/config.w32:3`) and a snapshot build keeps an in-tree extension's default static
  (`win32/build/confutils.js:471-497`, every in-tree module is in `core_module_list`); not run here,
  the CI `windows` job shows it.
- 2026-10-06 The cross-thread wakeup polls a descriptor pair of the reactor's own (an eventfd, a pipe,
  a loopback socket pair on Windows), kept in the module globals for the thread's life and polled by
  a POLL op with no handle; no `Io\Poll\NotifyHandle` (S4.5, departure from S4.md 3.6). Why: the
  handle dies with the request while another thread may still hold a trigger, and a lock-free
  `trigger()` needs a descriptor that outlives it, as libuv's loop outlives its async handles (the
  Sage, over the Critic's mutex). `RFC-CHANGES.md` 1 asks the core to export the pair.
- 2026-10-06 Every live trigger is on the reactor's `triggers` list and the wakeup walks all of it;
  the deadlock counts a trigger between its start and stop, which a waiting record or a holder with
  a callback calls (S4.5). Why: TrueAsync's remote Future and thread-pool cancel trigger wait with a
  callback and no coroutine (the Critic). Checked by `reactor/026`, `029`-`031`.
- 2026-10-06 A fire that finds nobody waiting is dropped, as TrueAsync's (S4.5). Why: every holder
  checks its condition under its own lock before it links (`thread_channel.c:148-156`); keeping the
  fire would add a second contract (the Sage). Checked by `reactor/028`.
- 2026-10-06 A fork rebuild cancels every coroutine still waiting for a trigger (S4.5). Why: they are
  the parent's, and a thread the child starts could resume them in the child (the Sage). The child
  makes wake descriptors of its own and raises them once, for a fire made before the fork. Checked by
  `reactor/033`, `035`, `036`.
- 2026-10-06 The Windows lane gets its S6 expectations, which S6.2 took from the Linux lanes only
  (pocs-win red since `bfcb26f`: 7 FAIL, 76 unexpected SKIP). The Windows-only `stream/001`, `002`,
  `046-…_win` (S6.4) and `exec/001`, `003` (S6.5, `proc_close()` waits for the child) carry
  `--XFAIL--`; `io/044` (S6.3) and `dns/005` (S6.4) fail there only and take the new list tag
  `xfail-on:<lane>(<step>)`, the one tag a frozen line may lose; 76 tests the lane skips take
  `skip-on:pocs-win` naming the step that loads curl (S6.6), sockets or openssl (S6.4) there, and
  `dns/013` is Unix-only. Why: a lane red for a known reason hides the next regression; read from
  the CI artifact of `d2ff382`, not run here; the Critic's seven findings fixed.
- 2026-10-06 A bug in bukka's code is told to Edmond first, then goes to bukka as a pull request,
  and meanwhile our core carries the fix from the branch `io-hooks-fixes`, merged into
  `async-core-io` (Edmond: «если мы находим баг в коде от Буки, тогда мы 1. говорим мне об этом и
  рассказываем проблему 2. делаем PR в его код 3. у нас должна быть своя ветка свободная от бага»).
  Why: our tests cannot wait for bukka's merge; `dev/WORKFLOW.md`, "Ownership".
- 2026-10-06 `info/002-info_getCoroutines_integration.phpt` counts main from the request's start,
  as `info/001` does since S3.8 (S5.3). Why: the scheduler and main exist before the script
  (the 2026-10-02 entry on refusing while async is not active); TrueAsync creates main at the first
  spawn, so its counts are one lower before and one higher after.
- 2026-10-06 `await_*` does not count null triggers in `total`, refuses the waiting coroutine among
  its triggers, and its wait for the rest counts the coroutines it links (S5.md section 5, "As
  built in S5.3"). Why: TrueAsync's `total` counts nulls, so `await_all([$future, null])` never
  ended; a self-await deadlocks there while `await()` refuses it (S3.md 4.1, phase 0); and its wait
  for the rest waits for `resolved_count` to reach `total`, which a pending Future never lets it
  reach (`await/103`, `104`, `106`).
- 2026-10-06 The iterator coroutine of `await_*` goes on walking the Traversable after the wait is
  over, without linking or writing, until the waiter's exit cancels it (S5.md section 5). Why:
  TrueAsync's iterator does (`await/049`); the walk touches nothing of the departed waiter.
- 2026-10-06 `await_*` checks its token after the Traversable's `getIterator()`, holds the token's
  awaitable and every trigger's for the wait, and its wait for the rest goes over the triggers a
  parked wait did not take, keeping the errors of coroutines that finished since the wake (S5.md
  section 5, "As built in S5.3"). Why: `getIterator()` runs PHP code that may complete the token
  or construct a Future again (`await/111`, `112`); without the walk a coroutine's error was marked
  observed and never reported (`await/113`).
- 2026-10-06 A Traversable's repeated trigger is linked again, and its completed trigger may wake
  the waiter before the iteration ends (S5.md section 8, item 10). Why: the array path does the
  same; TrueAsync's skip of a trigger already in the waker and its wake only at the iterator's end
  need state S5 does not keep, and no test observes the difference.
- 2026-10-06 Core `async-core-io-2026-10-06` (`1ee473ff67b`): bukka's IO hooks head `608927ebe09`
  and our `io-hooks-fixes` (`189b408d583`, `dev/RFC-CHANGES.md` 2) merged into
  `async-core-io-2026-10-05-4`; ior `2bfd2319896`, the one bukka's head builds with. The head
  already fixes the mysqlnd orphan crash S6.3 met (`0ee980f0`) and refuses closing a MySQL
  connection a fiber is parked in (`b05a2fd6`); a finished read cancelled before its waiter resumed
  keeps its bytes in the stream (`d620a523`). Compared on 2026-10-06: `main` (`f313e28`) gives
  `pocs-dbg` 620 PASS, 8 SKIP, 209 XFAIL and `pocs-asan` 605 PASS, 25 SKIP, 207 XFAIL, nothing unexpected;
  `Zend/tests`, `ext/test_scheduler/tests` and the core's `streams/hooks` tests fail nothing on
  either core (5,604 PASS before, 5,613 after: the new hooks tests); the bridge passes its 23 tests on
  dbg and ASAN. Why: a fix of ours must sit on bukka's current head (`dev/WORKFLOW.md`,
  "Ownership"), and his head fixes a crash we met.
- 2026-10-06 `reactor/021-exit_deadline_in_forked_child.phpt` (S4.txt, `changed:2026-10-06`) no longer prints the parent's line after
  `pcntl_waitpid()`. Why: with the IO provider (S6.3) the wait parks the parent's coroutine, and the
  script's `exit(0)` cancels it at the exit deadline before the child exits; the child's line is
  what the test is about. Agreed with the S4 thread.
- 2026-10-06 The IO provider's `run()` parks on a heap copy of the core's op and keeps its own
  reference to that event across the park, reading the result after the suspend; the event holds
  no pointer into the caller's frame (dev/plans/S6.md 3.2-3.3). Why: TrueAsync's process wait reads
  its event after the suspend the same way (F `ext/standard/proc_open.c:1643-1646`); frame pointers
  written by the wake and the unlink let a stale wait write into a frame that was gone (Critic);
  the Sage's ruling.
- 2026-10-06 A read cancelled after it completed keeps its bytes in the stream (`io_provider/009`),
  where TrueAsync drops them with the cancelled call. Why: the pinned core does so (`d620a523`):
  with an exception pending it runs nothing more for the op and a finished read leaves its bytes to
  the stream.
- 2026-10-06 `pcntl_fork()` is refused while an op is parked in the IO provider ("Cannot fork while
  IO operations are in flight"). Why: the core's own guard (`FG(io_ops_in_flight)`); narrower than
  TrueAsync, which refuses a fork while any coroutine but main exists; a child cannot complete the
  parent's parked ops (the Sage).
- 2026-10-06 `io/039`, `040`, `042`, `043` (`stream_set_timeout()` on a pipe) and `io/100` (a
  cancelled file read keeps the position) carry `--XFAIL--` naming S8; `io/094`, `095` (a stream
  changed while a read is suspended in a user filter) name S6.7; `io/035`-`037` (Async in a
  `php -r` child) name S6.7. Why: the pipe timeout is a feature vanilla PHP lacks (`dev/RFC-CHANGES.md`
  3); files run on the thread until `F_FILES`; `io/094` corrupts the heap with plain Fibers on the
  pinned core, a php-src streams bug TrueAsync fixed in its core (F `bf6048d03c6`), whose branch is
  Edmond's call; the core launches no scheduler under `php -r`.
- 2026-10-06 S6.3 removes the `--XFAIL--` of `stream/005`, `012`, `026`, `029`, `031`, `032`, `046`,
  `socket_ext/001`-`005` (named S6.4) and `curl/069` (named S6.6): with the provider and S5.3's
  `await_*` they pass on `pocs-dbg` and `pocs-asan`. `stream/005`, `012`, `029`, `031`, `032` carry
  `xfail-on:pocs-win(S6.4)`, as `stream/001`, `002` do, and `io/044`'s Windows tag names S6.5 (the
  Windows pipe commit). Why: a test that passes loses its section in the push that makes it pass;
  the Windows lane was not run here.
- 2026-10-06 The Windows `proc_open()` pipe core commit moves from S6.3 to S6.5, on `io-hooks-fixes`
  (S6.md 9), replacing the step the 2026-10-06 entry on overlapped pipes names. Why: it serves the
  children S6.5 brings, and a change to bukka's code goes through `io-hooks-fixes` and a pull
  request (`dev/WORKFLOW.md`, "Ownership").

- 2026-10-06 The automatic collector runs on `zend_hrtime()`, and the first idle point of a request
  starts its clock (S7.2, S7.md 5). Why: the idle point precedes a blocking wait, so the precise clock
  costs nothing there, and the reactor's coarse clock is private to it.
- 2026-10-06 The interval bounds how often an automatic run happens and schedules none: a loop
  blocked on one long wait runs the collector at its first idle point after the wait ends (S7.2,
  S7.md 5; the Critic asked for a wake). Why: a timer of the collector's own would wake an idle
  server for a run the back-off makes rarer each time, and would sit on the reactor's lists, which
  the walk reads.
- 2026-10-06 `ASYNC_COROUTINE_F_DEADLOCK_REPORTED` is set only when the warning was raised, and only
  a run that warned resets the back-off (S7.2, S7.md 6). Why: with `E_WARNING` out of
  `error_reporting` a coroutine marked unwarned would never be reported once it is back.
- 2026-10-06 In the warning's error handler an exception is released, `exit()` ends the request as
  in a coroutine (D16) and a fatal error ends it by the bailout (S7.2, `collector/024`, `025`). Why:
  no PHP code called what warned, so nothing could catch the exception; an exit is no exception to
  report.
- 2026-10-06 The fuzz oracle excuses what the registry's walks cancel: `registry_cancel()` marks the
  coroutine as handed out in test builds, and a wake by a handed-out or bailed-out target passes the
  excuse to its waiter; a cancel of a found coroutine from anywhere else aborts the seed (S7.2,
  S7.md 11). Why: those walks hold no reference, which the walk leaves out by design (S7.md 2).
- 2026-10-06 Generator frames are not walked, so a coroutine parked inside a generator is missed when
  only the generator holds its target (S7.2, `collector/017` expects 0). Why: `zend_generator_frame_gc`
  has no `ZEND_API`; an S8 change-request candidate. A miss is allowed, a false finding is not.
- 2026-10-06 Tests changed for S7.2: `module/001-registration.phpt` and `module/002-info.phpt` list
  the new INI entries `true_async.partial_deadlock` and `true_async.partial_deadlock_interval`
  (S7.md 6 and its INI table), as they did for `true_async.debug_deadlock` on 2026-10-02. The Critic
  judged the reason.
- 2026-10-07 The walk counts a future event as a node of its own, with `base.ref_count` as its count,
  and reads `Future` and `FutureState` through `async_future_collector_references()` instead of their
  `get_gc` (S7.3, S7.md 10). Why: `future_event_gc` folds the event into its holder only while it has
  one holder and reports nothing while a waiter holds it too, which is right for PHP's collector and
  would hide every awaited Future from this walk.
- 2026-10-07 Every record kind of S5 names its target as owned: FUTURE (`async_future_await()` takes
  the wait's reference), the token kinds (every caller holds the token for the call) and the `await_*`
  trigger (`await_trigger_add()`); a Timeout token makes its waiter live (S7.3). Why: each of these
  references sits in a C frame or a block the walk does not read, so only the record can report it,
  and a Timeout's timer may fire whatever the walk sees.
- 2026-10-07 The iterator kind of an `await_*` over a Traversable keeps `collector_target` NULL, so its
  waiter is never a candidate (S7.3). Why: the Traversable's own code decides when the iteration
  ends; a miss is allowed, a false finding is not.
- 2026-10-07 Of the outside sources of S7.md 3.4 only S6.5's signal watch is seeded: it reports the
  event of each `signal()` Future it will complete as live (`async_signal_collector_seed()` in
  `src/os_signal.c`, agreed with S6); the reactor's `waits` and `triggers` lists seed nothing (S7.3,
  `collector/032`). Why: no node of the walk is ever on those lists itself, while a `signal()` Future's
  event is borrowed by its wait and has only its Future object as a counted holder.
- 2026-10-07 The walk finds nothing once the request shuts down (`EG_FLAGS_IN_SHUTDOWN`), from the
  shutdown functions on (S7.3, S7.md 2, `collector/041`). Why: the engine's destructor pass calls every
  destructor not yet called whatever holds its object, a route into any subgraph that no reference
  counts, and no drain of the scheduler runs between the shutdown functions and that pass.
- 2026-10-07 The fuzz oracle checks the wakes of FUTURE, the tokens and the `await_*` triggers too; the
  completer, the running coroutine, is excused only in the bailout, and scheduler context gives no
  excuse (S7.3, S7.md 11, `collector/040` through `TrueAsync\Test\mark_found()`). Why: a handed-out
  completer was live at the run, so whatever it reaches was counted live, and everything that cancels
  found coroutines hands the waiter out itself; a wider excuse only hid bugs.
- 2026-10-07 A walk node keeps its event reporter as a pointer, 32 bytes a node instead of 24 (S7.3; the
  Critic asked for a type tag and a table). Why: simple code over saved bytes, with no lookup to save
  memory.
- 2026-10-07 `true_async.partial_deadlock=cancel` warns once, then cancels every coroutine still
  parked as `registry_cancel()` does (protection cleared, handed out for the oracle), main aside
  (S7.4, S7.md 6). Why: as the global deadlock cancels its waiters; main's uncaught cancellation would
  end the script silently with exit status 0, while parked main gets the global deadlock's
  `DeadlockError` (`collector/046`).
- 2026-10-07 Under `cancel` a run that cancels a coroutine never cancelled before resets the back-off,
  one that only cancels a coroutine again does not (S7.4, S7.md 5). Why: a coroutine that catches the
  cancellation and parks again would otherwise pay a full walk every interval forever; with
  `E_WARNING` off the first cancel is still the run's new finding.
- 2026-10-07 `tests/collector/020-automatic_off.phpt` checks the refusal of an unknown policy with
  `'kill'` instead of `'cancel'` (S7.4). Why: `cancel` is a valid value now; `kill` takes the same
  refused branch.
- 2026-10-07 The walk counts `$this` of a frame `zend_call_function()` pushed (`ZEND_CALL_TOP`, `$this`,
  no `ZEND_CALL_RELEASE_THIS`), off for user frames while `zend_execute_ex` is replaced (S7.5, S7.md
  3.2). Why: that call pins the object with no flag on the frame, so a coroutine whose body is
  `[$object, 'method']` was never found; a profiler's VM marks its own user calls so without a
  reference, which made a false report (`collector/054`).
- 2026-10-07 The automatic run stops before its tables would take the memory in use past
  `memory_limit` and finds nothing; `get_deadlocked_coroutines()` has no ceiling; the tables stay on
  the request heap (S7.5, S7.md 3.5, the Sage). Why: a fatal error from a warning that changes no
  outcome would end the request; PHP's collector also walks on the request heap; persistent tables
  would hide memory from the limit and leak on a bailout.
- 2026-10-07 The collector is not seeded from the reactor's `waits` or `triggers`; a source that
  completes a counted awaitable seeds it from `async_collector_find()`, and the fuzz oracle is the
  guard (S7.5, S7.md 3.4, the Sage). Why: S10's remote Future is on `triggers` and a foreign loop's
  bridge on neither list, and an event without a seed op would make every Future waiter live, a
  silent miss.
- 2026-10-07 S7.5's Mull runs the mutants of `src/collector.c` and of the S7 lines of the hook files
  (`git blame` names an S7 commit) against `tests/collector/` only. Why: a run of every list per
  mutant does not fit in hours (S3.24); the collector's mutants are killed by its own tests, and one
  only another list kills counts here as a survivor, never the other way.
- 2026-10-07 A run stopped by the ceiling raises nothing (S7.5; the Critic asked for a notice). Why:
  a new message is user-visible text; S8 or Edmond decides.
- 2026-10-06 `stream/004-stream_socket_client_server.phpt` and `stream/007-tcp_client_server_full.phpt`
  expect the worker's line after the server's accept line, and `stream/028-udp_basic_operations.phpt`
  no longer sets the shared address to null in its client (S6.4). Why: TrueAsync resolves a numeric
  host in libuv's pool, so `stream_socket_server("tcp://127.0.0.1:0")` parks the server; the core
  resolves it on the thread (`php_io_host_is_numeric()`). The server then parks first in the accept
  or the receive, which still shows the worker running while it waits; `028`'s client overwrote the
  address the server had set and waited for an address that never came. The fork no longer passes
  `004` and `007`: its worker prints before "Server: listening".
- 2026-10-06 `stream/017`, `018` stay XFAIL by design: `stream_select()` with non-streams or no
  streams throws PHP's TypeError per non-stream and then a ValueError, where TrueAsync's fork, in a
  coroutine, throws no ValueError and returns 0 for no stream at all (S6.md section 11). Why: the core's `stream_select()`
  is vanilla PHP's, the same with and without a coroutine; the fork answers differently in and out
  of one.
- 2026-10-06 `stream/030` (a UDP receive timeout) names S8 and `dev/RFC-CHANGES.md` 4; `io/096` (two
  coroutines on one socket) names S6.7 with B1, as `io/098` (S6.4). Why: the core's transport calls
  wait without the stream's timeout, as vanilla PHP's; B1 refuses a second user of a stream with a
  parked op.
- 2026-10-06 The Windows lane's S6.4 expectations move to S6.5: `xfail-on:pocs-win(S6.4)` becomes
  `S6.5` on `stream/005`, `012`, `029`, `031`, `032`, `dns/005`, and the Windows-only `stream/001`,
  `002`, `046-…_win` name S6.5. Why: loading `sockets` and `openssl` in `pocs-win` and reading its
  results needs a run of that lane, which S6.5 makes with its Windows pipe commit; S6.4 changed no
  Windows-only code path. The frozen `skip-on:pocs-win(sockets-not-loaded-until-S6.4)` and
  `openssl` tags keep their text and mean S6.5; S6.5's done line names the tags it must clear.
- 2026-10-06 S6.4's done line excepts the tests naming a core change or another track's step, as
  S6.3's does (`stream/030`: S8; `dns/006`: S5.4). Why: neither is the provider's to make pass.
- 2026-10-06 An accept under the provider is `accept()` first, then a readiness wait: the provider
  masks `PHP_IO_HOOKS_F_DIRECT_ACCEPT` and submits the copy of an ACCEPT op as a POLL READ on the
  listener, whose Done the core gets as Ready (S6.4, S6.md section 4). Why: the Ring's multishot
  accept took connections into a buffer `stream_select()` and `socket_select()` cannot see, so a
  select loop waited its whole timeout with a connection pending (the Critic; `io_provider/015`), and
  it closes a connection handed to a cancelled wait; TrueAsync polls and then accepts. The Ring's
  behaviour goes to bukka as a bug (`dev/WORKFLOW.md`, "Ownership"); the non-blocking accept that
  asked the Ring for its buffer (S6.3) is gone with it.
- 2026-10-06 `Async\timeout()` takes its absolute deadline when it returns, arms S4's Timer op only
  while a wait is parked on it, and stays fired once fired; `Timeout::cancel($cancellation)` is
  terminal and wakes its waiters with `OperationCanceledException` whose `previous` is
  `$cancellation` (D32, `dev/reviews/s3-structures/timeout-semantics.md` option B; S5.md section 6).
  Why: TrueAsync restarts the timer for each wait, so a loop of waits never times out, loses the
  outcome after the fire and ignores `cancel()`'s argument (S5.md section 8, item 11).
- 2026-10-06 Each wait a fired `Timeout` ends gets a `TimeoutException` of its own as the
  `previous`; the `Timeout` keeps only `cancel()`'s argument. Why: the engine's chaining onto a
  thrown exception writes into its tail, so one kept exception carried what one wait chained to every
  later use (`await/117`); TrueAsync makes one per fire.
- 2026-10-06 `await($timeout)` and a `Timeout` among `await_*` triggers throw `Error("Async\Timeout
  can only be used as a cancellation token")`. Why: TrueAsync accepts both untested, and every use
  of `timeout()` in its tests is a token (S5.md section 6, point 6).
- 2026-10-06 A wait subscribes to its `Timeout` after its reservations and right before its first
  link, and in a forked child the subscribe runs the reactor's fork check before it reads the timer's
  place on the lists (`async_reactor_check_fork()`, the one public addition to S4's reactor). Why: a
  bailout in a reservation left an armed timer nothing unsubscribed (`await/127`), and a child whose
  first wait came before any submit kept the parent's dropped op and never timed out (`await/124`;
  the Critic on S5.4).
- 2026-10-06 `Async\signal()` watches a number through an `Io\Poll\SignalHandle` added to an
  `Io\Poll\Context` the thread keeps while any number is watched and never waits on, and one SIGWAIT
  op per number on the reactor; a delivery completes every Future waiting for that number and goes to
  the Zend handler too (`dev/plans/S6.md` section 8). Why: the handle is how the core blocks a signal
  and keeps it unblocked in `proc_open()` and `exec()` children; TrueAsync's libuv handle and
  forward do the same jobs.
- 2026-10-06 What a handle recorded between the last delivery and the removal of its number is
  raised again with `raise()`, so the process's own action applies (a pcntl handler, the default,
  or none). Why: nothing waited for it; S6.md first said to forward it or raise it by hand per
  action, which is what `raise()` does through `zend_signal_handler_defer()`.
- 2026-10-06 `Signal::SIGBREAK` and `Signal::SIGABRT2` throw a ValueError outside Windows, and the
  enum's Linux numbers map to the platform's constants. Why: TrueAsync passes 21 and 22 through, so
  on Linux it watches `SIGTTIN` and `SIGTTOU` (`async.c:1203-1217`).
- 2026-10-06 `Async\signal()` throws on Windows, after the check of a completed token, and
  `signal/001` skips on `*-win`. Why: the core has no signal source there (S6.md section 9);
  `signal/003` and `004` pass on Windows through the token check.
- 2026-10-06 `exec/012` and `exec/025` keep `--XFAIL--` by design: `proc_close()` returns PHP's wait
  status for a child a signal killed (the signal number) where the fork returns its negation, and a
  pipe closed under a parked reader gives it `''` where the fork gives `false` (B1; S6.7 tags it
  `core:`).
- 2026-10-06 The Windows part of S6.5 moves to S6.10: the `proc_open()` pipe core commit and the
  Windows lane's socket expectations; the `xfail-on:pocs-win(S6.5)` tags and the Windows-only
  `--XFAIL--` sections name S6.10, and the frozen `skip-on:pocs-win(...-until-S6.4)` and
  `(...-until-S6.5)` texts mean S6.10. Why: a core commit for Windows needs a build and runs on
  Windows, and S1.5 (a Windows agent) is deferred by Edmond; S6's done line asks for debug and ASAN
  only.
- 2026-10-07 Tests changed for S6.6: `curl/006-timeout_handling.phpt` times out on a local listener
  that never accepts instead of connecting to `192.0.2.1`; `curl/025-write_file_broken_pipe.phpt`
  and `curl/054-multi_write_file_broken_pipe.phpt` expect the send warning twice;
  `curl/043-multi_write_user_exception.phpt` expects `curl_errno()` 0. Why: where no route to
  `192.0.2.1` exists the connect fails at once (error 7, no timeout), so `006` passed only on
  networks that drop the packets; a listener of its own keeps the timeout independent of the other
  request, which the test server's slow route did not (one server process can take both). The
  core's curl writes `CURLOPT_FILE` through a stdio `FILE` over the stream, which reports the failed
  send twice, and sets a multi handle's error only in `curl_multi_info_read()`, which `043` never
  calls; php/php-src without the extension prints the same. TrueAsync's fork writes and records the
  error in its own curl event (`curl_async_write_file()`). The write error `025` and `054` check is
  the same.
- 2026-10-07 `pdo_mysql/029` is tagged `core:6`, left out of the `pocs` lanes: a driver error raised
  under a pending cancellation replaces it (`RFC-CHANGES.md` 6), where the fork's `pdo_dbh.c` keeps
  the cancellation on top. An `--XFAIL--` section would not do: the test passes whenever no cancel
  lands mid-connect.
- 2026-10-07 The Windows part of S6.6 (curl in `pocs-win`) goes to S6.10 with the rest; the frozen
  `skip-on:pocs-win(curl-not-loaded-until-S6.6)` texts mean S6.10. The Windows-only tests skip on the
  coverage lane too (`skip-on:*-cov`), which counted them as unexpected skips (S5's report).
- 2026-10-07 Core `async-core-io-2026-10-07` (`8f89755d2b1`): `php-src-fixes` `6e9d801dcc5` (the
  php-src streams fixes, the per-filter check of php/php-src#24168 among them), `io-hooks-fixes`
  `c43e1d5797a` and `async-core` `ae85ef88d00` merged onto `async-core-io-2026-10-07`, branched from
  `async-core-io-2026-10-06`; ior unchanged. The per-filter check counts a user filter's calls in `Z_EXTRA(filter->abstract)`, so
  `PHP_STREAM_FLAG_USER_FILTER_RUNNING` (`0x800`, which clashed with the hooks' `IN_USE`) is gone.
  Merge conflicts with master, which `php-src-fixes` (on 8.4's merge base) does not carry:
  `pclose()` checks `NO_FCLOSE` before master's context code; `userfilter_filter()` undoes its call
  count when master's `userfilter_assign_stream()` fails; `_php_stream_copy_to_stream_ex()` sets
  `NO_FCLOSE` on both streams around master's body, now `php_stream_copy_to_stream_impl()`; the two
  php-src tests expect master's message. Master's `userfilter_seek()` does not count its call (a gap
  of master, not of the merge). Why: PLAN S6.7; one core update at a time.
- 2026-10-07 The scheduler launches for command line code (`async-core` `ae85ef88d00`,
  `sapi/cli/php_cli.c`): `-r` code runs as `php_execute_script_ex()` runs a file, between
  `ZEND_ASYNC_SCHEDULER_LAUNCH()` and `ZEND_ASYNC_RUN_SCHEDULER_AFTER_MAIN()`, and an exception it
  leaves belongs to the main coroutine; `-B`, every `-R` line and `-E` share one main coroutine and
  the scheduler runs once, after `-E`, so a coroutine `-B` spawns runs while the lines are read (the
  Critic: a drain per segment never read stdin past an endless `-B` coroutine). `-B`, `-R` and `-E`
  keep PHP's reporting of an uncaught exception per segment; `-F` still runs each line through
  `php_execute_script()`, which drains per line, as before. Test
  `ext/test_scheduler/tests/091_command_line_code.phpt`. Why: the scheduler RFC launches the
  scheduler before the script's first line with no lazy start, and `io/035`-`037` spawn coroutines
  in a `php -r` child; TrueAsync builds `ext/async` into the binary and starts its scheduler lazily.
- 2026-10-07 `io/035-stdin_read_in_coroutine.phpt`, `io/036-tty_stderr_write_async.phpt` and
  `io/037-tty_concurrent_stdout_stderr_async.phpt` start the child with
  `TEST_PHP_EXECUTABLE_ESCAPED` and `TEST_PHP_EXTRA_ARGS`, and pass the child code through a file
  next to the test (`-r require '<file>';`). Why: the extension is a shared module, loaded through
  run-tests' `-d` settings, which a bare `TEST_PHP_EXECUTABLE` does not get; on Windows
  `escapeshellarg()` drops the `"` of the code.
- 2026-10-07 The seven tests that include a php-src test helper take it from the core checkout named
  by `$TRUE_ASYNC_CORE_SRC`: `io/082-http_negative_timeout_poll_leak.phpt`,
  `stream/003-file_get_contents_http.phpt`, `curl/063-readdata_no_callback.phpt`,
  `curl/064-stderr_file_reuse.phpt`, `curl/070-read_takes_only_its_completion.phpt`,
  `curl/071-upload_stream_closed_mid_read.phpt`, `curl/072-fnmatch_exception.phpt`;
  `stream/003-file_get_contents_http.phpt` loses its SKIPIF, which tested a function the include
  defines and so always skipped. `tools/test.py` passes the variable to tests and stops a run that
  includes one of them when it does not name a checkout with the helpers; CI's coverage job checks
  out the core's sources. Why: the reference reaches the helpers by `__DIR__ . '/../../../../'`, the
  layout of `ext/async` inside the core tree (`dev/plans/S2.md` section 1). `curl/071` skips while
  libcurl is below 8.11.1 (8.5.0 here and on CI's Ubuntu 24.04).
- 2026-10-07 A DNS lookup (GETADDRINFO, GETNAMEINFO) yields to the queued coroutines before its op
  is submitted. Why: `dns/003` failed in about 4 of 60 runs under load (S6.6); TrueAsync's lookups
  always complete in a later pass of its loop (libuv's thread pool), while the Ring can complete one
  inside the coroutine's own suspend tick, which resumes it without a switch. A yield after an
  inline completion did not help (11 of 80 runs misordered); with the yield first, 120 of 120 runs
  are in order.
- 2026-10-07 The by-design failures of review B1 are tagged `core:12` (`dev/RFC-CHANGES.md` 12) and
  leave the `pocs` lanes: `io/096`, `io/098`, `exec/025`. `io/081` and `084` (no Flock op, M10) move
  from `S6.excluded` to the list as `core:11`; `io/101` joins it, the core having zlib since S4.4.
  Why: PLAN S6.7; a `core:` tag names the request that makes the test pass.
- 2026-10-07 Shutdown windows (S6.md section 10): own tests `io_provider/016` (a session handler's
  write at the request's end: the session module's RSHUTDOWN runs after ours, which removed the
  provider, so the write is synchronous and no coroutine starts), `017` (a fatal error raised in the
  tick of a coroutine parked in `fread()`: no wait stays linked, the stream stays frozen as
  `RFC-CHANGES.md` 8 describes) and `018` (a destructor's IO after `exit()` in a shutdown function
  parks through the queue). A destructor's IO after a bailout caught in a shutdown function cannot
  happen: a fatal error marks every object destructed (`zend_objects_store_mark_destructed()`), so
  `018` uses `exit()`. The plan's `ts_suspend` NULL case, `run()` with no current coroutine while
  async is active, has no path: the launch installs main before the first line, and a NULL current
  occurs only inside the scheduler's own work, which `ZEND_ASYNC_IN_SCHEDULER_CONTEXT` answers first
  (the Sage); the check stays as a guard. A `run()` for a coroutine that is not running is
  `io_provider/011` (main left behind by a caught bailout). Why: PLAN S6.7.
- 2026-10-06 `await_*` marks every trigger it takes observed on entry, coroutines included, as
  TrueAsync's `async_await_futures` (`async_API.c:1007-1008`); `await()` keeps S3's mark of a
  coroutine when its waiter reads the outcome. So `await_any_or_fail([$a, $b])` reports no later
  error of `$b`. Why: P1.4; S5.md section 2 said otherwise while the code followed TrueAsync (the
  Critic on S5.5), and marking only the read outcomes would need a mark at every wake path.
- 2026-10-06 A scheduler the core refused leaves out `Future`, `FutureState` and `Timeout` with the
  `Async\` functions; `Coroutine` and the exceptions stay. Why: a `Future` needs no function to be
  made, and its callbacks crashed on the coroutines of a scheduler that never started
  (`module/005`; the Critic on S5.5).
- 2026-10-06 `await_*` keeps its context and record chunk on the heap at every N, with no inline
  records past the waker's two. Why: it costs fewer instructions and allocations than TrueAsync from
  N = 1 (`dev/BENCHMARKS.md`, S5.5).
- 2026-10-07 A coroutine refuses a `fiber.stack_size` below the core's minimum for a Fiber (a page
  and the guard pages, the guard six times under ASAN) with the core's text;
  `scheduler/056-small_fiber_stack_size_throws.phpt` takes 32 KiB instead of 16 KiB, above that
  minimum on ASAN builds. Why: the room our stacks add for the first VM page hid the core's refusal,
  and a coroutine on 4 KiB crashed (`scheduler/106`; the Critic on S5.5 for the ASAN factor).
- 2026-10-06 The reactor keeps its TIMER events in a heap of its own and passes the queue's wait the
  nearest deadline, completing the due timers after the poll, as libuv does (S4.6, S4.md 3.5). Why: a
  kernel timeout per Timer op made the Ring wake a burst of 10 000 `delay()`s 46-64 ms late, the Poll
  queue 0.1 ms; with the heap the Ring is at 0.02-0.05 ms (BENCHMARKS). The backlog walk S4.4 blamed
  took 0 steps when measured. Array heap, no tie sequence, a push during a run clamped past the
  run's clock, the run stopped at a notify's exception (the Sage). Checked by `reactor/039`, `040`.
- 2026-10-06 Departures that S4.3 and S4.4 built without a line here (S4.6, S4.md section 4): the
  idle wait blocks only in the scheduler coroutine's loop (one blocking site; TrueAsync also blocks
  in the suspending fiber's tick); `reactor_poll` takes one completion per wait call (a notify may
  withdraw an op whose completion would sit in a batch); the queue's `EDEADLK` resolves a deadlock as
  an empty `waits` does (the Poll queue's answer for waits with no descriptor and no deadline).
- 2026-10-06 A fork rebuild's resubmit error goes to the exit exception (D16) or is dropped (the
  wakeup, made again by the next trigger start), not to the submit that found the fork; the fork
  check also runs at the tick's poll and the idle wait (S4.6). Why: the error belonged to another op,
  and the Poll queue delivers its ready list before its own fork check (the Critic). So
  `async_reactor_check_fork()` (S5.4) and `async_io_event_try_submit()` (S6.3) report no rebuild
  error: the first returns nothing, the second no longer returns -1.
- 2026-10-06 S4.6 changed own tests of S4.txt (`changed:2026-10-06`). Why, per test:
  `reactor/002-waits_list_after_fire_and_cancel.phpt` and `reactor/016-cancelled_delay_orphans_op.phpt`
  read the heap's count of Timer ops, which no longer reach the queue;
  `reactor/011-fork_child_poll_rebuilds_queue.phpt` forks once the reactor holds its wait (fuzz seed
  74 forked first and hung the parent's `waitpid()`);
  `reactor/021-exit_deadline_in_forked_child.phpt` gains a lower bound (the Critic);
  `reactor/029-trigger_unstarted_keeps_nothing_alive.phpt` and
  `reactor/033-trigger_wait_started_before_fork.phpt` reach two paths no test reached (coverage);
  `reactor/035-trigger_fire_before_fork_wakes_child.phpt` links the child's waiter before any poll,
  whose walk drops a fire nobody waits for, as the parent's would.
- 2026-10-07 A callback keeps its index in the vector it is in (`slot`, in the padding after
  `flags`), set on push and on every move of a removal, so removing it searches nothing; only a
  callback marked `ASYNC_CALLBACK_F_SHARED` (the exit deadline's, one static for all threads) and
  one not in the vector are found by a search. The order rule stays: a removal moves the last
  element into the gap (`triggers_end_parent_waits()` relies on it). Why: two waits over the same
  array of N copies of a Future unlinked in O(N^2) (200 000 copies 31 s, 0.016 s after), and N
  `Async\signal()` Futures left their watch the same way (200 000 8.3 s, 0.27 s after); the S3.md
  section 12 row "Linear unlink under fan-in" left this for a measured O(N^2), for records; the
  Critic on S5.6 found the signal case, so every callback keeps a slot (`await/141`, `signal/024`,
  `internal/063`).
- 2026-10-07 The iterator coroutine of an `await_*` over a Traversable lets go of the wait's context
  when its walk ends, not when its object is freed, and the iterator record reads the context from
  the waiter. Why: an item that holds that coroutine, directly, as a result or as a Future's value,
  made a cycle through an edge the GC does not see; refusing the current coroutine as an item caught
  only the direct case and broke a program that worked (`await/140`; the Critic on S5.6).
- 2026-10-07 S9 layer 1 (Scope) follows TrueAsync's `scope.c` (`dev/plans/S9-scope.md`); Scope lives
  in the extension whole, since the scheduler RFC defines none. Why: the RFC leaves the user-facing
  API to the scheduler (`scheduler_rfc.md:42-61`); its only trace is the cancel slot's `is_safely`.
- 2026-10-07 An unhandled error of a coroutine with no waiter parked at its finish (held and awaited
  later counts as none) climbs its scopes; a scope with no parent (`new Scope()`) keeps it. Reaching
  the global scope, it cancels with the flag of the scope where the error started: from a safe
  origin the started coroutines become zombies and the unstarted are cancelled, from an
  `asNotSafely()` descendant of the global scope every coroutine of the request is cancelled. Why: Edmond, "сделай как в TrueAsync", over cancelling with
  the global scope's own flag (no reference test tells the two apart) and over leaving the global
  scope out (18 reference tests fail, `scope/017`, `018` among them) (S9-scope.md 12).
- 2026-10-07 The route of an unhandled error is skipped when the coroutine's notify woke a waiter
  record, not by a mark the waiter's wake sets. Why: TrueAsync's resolve callback marks the coroutine
  handled, which in ours means observed and would drop an exception the woken waiter never read
  (the Sage, S9-scope.md 9 item 6).
- 2026-10-07 A scope reports the coroutines it may cancel to the collector through non-owning wake
  edges, an S7 reporter to add; the membership vector keeps bare pointers. Why: counted references
  make every member of the global scope live, and an owned report of a bare pointer makes the run
  report nothing (the Critic and the Sage, S9-scope.md 6).
- 2026-10-07 The coroutines of the core's `gc_new_coroutine` slot join one private root scope of the
  request, and `spawn()` from a coroutine with no scope (a Fiber's) uses the global scope. Why: the
  fork keeps the GC's coroutines out of user scopes (`zend_gc.c:2243`), and the RFC core passes one
  slot for the GC and the shutdown destructors (the Sage, S9-scope.md 3).
- 2026-10-07 `Scope::inherit()` at the top level always makes a child of the global scope, so an
  unawaited error in `Scope::inherit()->asNotSafely()` made before any spawn cancels the whole
  request; TrueAsync's scope there has no parent and keeps it. Why: our global scope exists from the
  script's first opcode, the fork's only after the scheduler's launch (S9-scope.md 3, 9 item 7).
- 2026-10-07 Waits on a scope are S4 wait records, and the scope struct holds no function pointers.
  Why: TrueAsync's parks without `zend_try` leave a waiter in the vector after a bailout (reference
  bug 14), and S4 made every wait a record (S9-scope.md 9 items 2, 4).
- 2026-10-07 A zombie keeps the request running, following TrueAsync's code over its documentation
  (`zombie-coroutines.md` says the opposite). Why: probed (`p3.php`); the layer ports the code.
- 2026-10-07 A `SpawnStrategy` whose scope has no object (the global scope) gets a stand-in `Scope`
  in both hooks, as the route's handler call builds one. Why: TrueAsync passes `ZVAL_OBJ(NULL)`
  (`async_API.c:130, 189`) and crashes (probe `p9.php`, S9-scope.md 9 item 8).
- 2026-10-07 Kept as TrueAsync: the array `SpawnStrategy::beforeCoroutineEnqueue()` returns is
  released unread. Why: no reference test reads it (`spawnWith/007`-`009` return `[]`).
- 2026-10-07 A member leaves its scope in O(1) through its index in the scope's vector. Why: the
  reference's linear search made its 100 000-coroutine run 88 % scope bookkeeping (S3.md 12).
- 2026-10-07 `edge_cases/010-deadlock-after-cancel-with-zombie.phpt` sets `true_async.debug_deadlock`
  instead of `async.debug_deadlock`, as `edge_cases/001`-`003` did on 2026-10-02. Why: INI names take
  the module prefix (2026-10-01).
- 2026-10-07 `true_async.partial_deadlock_interval` is 5000 ms by default and refuses 1 to 999; a
  literal 0 still walks at every idle point, and an empty value is refused (S7.6, Edmond). Why: a
  run walks every parked stack, and a coroutine that can never wake loses nothing by waiting
  seconds; 0 is what the tests and the fuzz run on; the parser reads an empty value, which php.ini
  makes of a bare `off`, as 0 (the Critic). `collector/019-automatic_interval_and_backoff.phpt` and
  `collector/047-cancel_backoff.phpt` (`changed:2026-10-07`) ran at 200 ms, now refused: they run at
  1000 ms with every `collector_age()` step and its line scaled by five, same checks.
  `module/002-info.phpt` (`changed:2026-10-07`) prints the new default.
- 2026-10-07 `collector/064-automatic_run_stops_before_memory_limit_on_candidates.phpt` skips on
  `pocs-asan` (S7.6). Why: the node table and its index first double past the heap chunk the
  ceiling keeps spare at 16 384 candidates, and 10 000 parked coroutines already take over three
  minutes under ASAN, almost all of it system time (1.1 s of it to spawn them; 16 500 take 0.3 s on
  the debug build); `collector/063` runs the same check on ASAN.
- 2026-10-07 `delay()` and `timeout()` count their deadline in nanoseconds
  (`async_reactor_deadline_from_ms()`), not through the core's `php_io_deadline_from_ms()`. Why: its
  `timeval` seconds are 32-bit on Windows, so past 2^31 s a debug build aborted and a release one
  woke early (`reactor/042`); the core's own callers pass bounded values.
- 2026-10-07 An exit object that cancels the running current coroutine goes to its waker, which its
  `suspend()` throws, not into its outcome (S4.7). Why: D16 reaches such a coroutine only in its own
  suspend's tick, woken there (U2); as the outcome it ran on, and `getException()` and `await()`
  handed out the core's internal exit object, whose `serialize()` crashed (`reactor/043`).
- 2026-10-07 The reactor's poll ends its loop after the wakeup's completion (S4.7). Why: the wakeup
  arms itself again inside its completion, so a thread that fires without pause kept the loop going
  for as long as it won a race; libuv's poll takes one batch (the Critic on S4.7).
- 2026-10-07 An exception from a `SpawnStrategy` hook is thrown by `spawn_with()`, and the coroutine
  is cancelled, so one that has not run finishes unrun (`spawnWith/014`). Why: TrueAsync never sees
  the exception (`zend_call_method` returns the retval, `async_API.c:132-141, 179-183`) and leaves
  the coroutine with an ignored waker; a finish takes it out of its scope and the registry the usual
  way, and its holders see it end.
- 2026-10-07 The stand-in `Scope` of item 8 is the scope's object while anything holds it, the
  hooks of every spawn_with() running at once included; its methods act on the scope
  (`asNotSafely()` on the global scope's stand-in clears the global scope's safe disposal), and its
  destruction never cancels the scope. Why: the scope's disposal and the request's end detach it as
  any object, so a hook that suspends or ends in a fatal error leaves no pointer to a freed scope,
  and one hook's return cannot detach it under another (the Critic, S9.2; `spawnWith/013`, `015`,
  `016`).
- 2026-10-07 A hook may suspend: `spawn_with()` holds the coroutine across the hooks and returns it
  even when it finished meanwhile; a coroutine finished in `beforeCoroutineEnqueue()` is not queued.
  `spawn_with()` also holds the `Scope` the provider returned until the spawn is done, so a provider
  may return a temporary `new Scope()`. Why: hooks are PHP code (the Critic, S9.2).
- 2026-10-07 A `Scope` object freed without its destructor (after a bailout) cancels its scope only
  while async is active; afterwards it only detaches. Why: no scheduler is left to run the cancel.
- 2026-10-07 The future drain's coroutines and the interrupt coroutine (S4's pcntl handlers) join the
  global scope, so a handler's spawn lands there; a safe cancel of the global scope makes the
  interrupt coroutine a zombie, which runs its handler to its end. Why: neither belongs to a user
  scope, and the engine's scope is for the core's coroutines (S9-scope.md 3).
- 2026-10-07 `scope/058` runs with `zend.enable_gc=0`. Why: the core raises the GC threshold once per
  coroutine waiting for one run (`zend_gc.c:727-730`, 90 020 001 after one run of 12 000
  coroutines, TrueAsync 20 001), and on ASAN each growth of the root buffer copies it; reported for
  a core fix, the INI line goes with it.
- 2026-10-07 The coroutines of the `gc_new_coroutine` slot install no IO provider (S6.8; S9.2 filled
  the slot the same day for the engine's scope). Supersedes the 2026-10-05 entry "the
  `gc_new_coroutine` slot stays NULL". Why: taken like `new_coroutine`'s, they install the provider,
  so a script's first `gc_collect_cycles()` turned its IO asynchronous (the Critic; `io_provider/019`).
- 2026-10-07 Under a cancellation, a Done of a POLL or an ANY answers FAILURE, as a Ready does
  (`dev/plans/S6.md` 3.3 step 5). Why: `php_io_run_cancelled()` lets a Done POLL through, so a pipe
  `fwrite()` wrote its bytes while it threw (the Critic; `io_provider/020`); the core side is
  `RFC-CHANGES.md` 14.
- 2026-10-07 After its park, `run()` drains a copy the queue still keeps in flight (the queue's
  `drain()`, as `php_stream_free()` does) and reports it settled: a cancelled op, or one whose early
  Timeout the Ring delivered first (the Sage). Why: the Ring keeps a cancelled
  RECV until its cancel completes, the stream stayed frozen, and the next read of the coroutine that
  caught the cancellation threw "Concurrent access to a stream" (the Critic; `io_provider/023`).
  TrueAsync stops its read at the cancel (libuv's `uv_read_stop()`), so the stream is usable at once.
- 2026-10-07 IO chaos (`dev/plans/S6.md` section 16): three fault points C1-C3 in `run()`, armed
  by `TRUE_ASYNC_SCHED=random:<seed>:io` (`tools/test.py --seeds N --io-chaos`), drawing from the
  scheduler's own fuzz state; the drafted C4 (resubmit) dropped. Why: the Sage: one state per seed
  replays a seed; C4 reached no path the cancellation tests miss.
- 2026-10-07 `signal_forward()` runs the Zend handler with the other signals deferred
  (`ZEND_SIGNAL_BLOCK_INTERRUPTIONS()`), as Zend calls a handler with every signal masked. TrueAsync calls it unblocked: a
  departure (P1.4), better because pcntl queues a delivery without a lock and a signal arriving
  during the call would re-enter it (the Critic).
- 2026-10-07 A graceful shutdown (`exit()`, an uncaught exception, `Async\graceful_shutdown()`)
  with no coroutine left polls the reactor once without blocking (again after any coroutine ran,
  the Sage), then ends without waiting for what still waits; the request's shutdown closes the
  watches of `signal()` Futures no coroutine awaits. A script that ends by itself waits for such a
  signal, as TrueAsync does. Why: Edmond, 2026-10-07 ("нет это баг ... обязан погасить все сигналы
  что открыты"; "если сам то да ждёт"): a held Future kept the script alive for good after `exit()`
  (S6.8, found from Mull's survivors; `signal/027`, `028`, `030`). The one poll delivers a signal
  that already arrived, which the watch's free would otherwise raise again with its default action
  (the Critic, `signal/029`). TrueAsync breaks its loop there only in a debug build
  (`scheduler.c:1989-2017`, `#ifdef PHP_DEBUG`): a departure on Edmond's word.
- 2026-10-07 `tests/curl/010-multi_select_async.phpt` waits for the other coroutine through the
  server (S6.8): both handles ask `common/barrier_router.php`'s `/hold`, which answers once the
  other coroutine has touched `released`. Why: the test expects the other coroutine to print while
  the curl one waits, but a coroutine whose socket is ready when its suspend tick polls (every
  100 ms) runs on with no switch, ahead of a queued coroutine, as TrueAsync's fast return path
  (`scheduler.c:1578-1584`); under load every select can land so, and it failed on the coverage
  lane and in S9.2's dbg run. A slower reply only made that rarer (the Critic); with the barrier
  the curl coroutine cannot finish before the other runs, and a select that blocked the thread
  ends in curl's 5 s timeout (checked: run without the other coroutine, both replies are empty).
- 2026-10-07 S1.5 skips on Windows, by a SKIPIF and a `skip-on:*-win` tag, the tests whose subject
  Windows lacks by design: `signal/001-signal_basic_timeout.phpt` (`Async\signal()` throws there,
  S6.md section 9; its tag was there without the SKIPIF); `exec/004-exec_basic.phpt`,
  `exec/005-shell_exec_basic.phpt`, `exec/008-system_basic.phpt`, `exec/009-passthru_basic.phpt`
  (the output of `_popen()` is read synchronously, S6.md section 11, so the other coroutine prints
  last); `io_provider/014-registration_before_the_queue.phpt` (the IOCP Ring offers no Edge
  registrations, `main/io/php_io_ring.c:283-288`);
  `collector/064-automatic_run_stops_before_memory_limit_on_candidates.phpt` (Windows commits each
  2 MB fiber stack in full, `Zend/zend_fibers.c:234`, 33 GB for its 16 500 coroutines; in CI it timed
  out, its retry got "Permission denied" writing the test's `.php`, and run-tests' worker stopped).
- 2026-10-07 S1.5 changes two own tests that failed only under the load of a full Windows run
  (single runs passed 20 of 20). `collector/031-waiter_on_coroutine_parked_on_trigger.phpt`
  waits for the waiter's mark, not `delay(20)`: the trigger's completion and the delay's timer came
  in one poll, so main printed "end" first. `await/136-timeout_throwing_subscriber_wakes_waiter.phpt`
  takes `timeout(1000)`, not 10, a margin for the waiter to subscribe first: when the deadline has
  passed before the waiter's `await()`, that `await()` fires the Timeout in the waiter, and the
  subscriber's exception is chained under the waiter's `TimeoutException` and starts no shutdown
  (reproduced on Linux with a 50 ms busy wait after `timeout()`; open in `dev/handoff.md`).
- 2026-10-07 A scope's exception handler cannot park: it runs as its coroutine finishes, where
  `suspend()`, `await()` and `delay()` throw, and an uncaught throw is the handler's exception
  (S9.3, `scope/065`). Why: a finished coroutine has given back its fiber context and left the
  registry before its notify, so a parked handler would be invisible to `exit()`, the deadlock
  count and the bailout walk; TrueAsync parks it, and its parked handler cannot be cancelled
  (S9-scope.md 9 item 9). Rejected: a coroutine of its own per handler call (the Critic), which
  reorders the route against the cascade (the Sage). Edmond may want handlers that wait.
- 2026-10-07 `exit()` in a scope's exception handler ends the request as in a coroutine's body, and
  the route stops (S9.3, `scope/066`). Why: TrueAsync chains the exit object as the handler's
  exception and loses its status (`exit(3)` ends with 255) (S9-scope.md 9 item 10).
- 2026-10-07 The handlers are kept as `zend_fcall_info_cache` fields of the scope, released when the
  scope goes, and the object's `get_gc` reports them while the object has its scope (S9.3,
  `scope/069`, `070`). Why: the core's own idiom for a stored callable (`zend_fcc_dup`,
  `zend_call_known_fcc`); the destructor, which the GC runs first, detaches the scope, so a scope
  that outlives its object keeps its handler's closure alive and never under a freed object.
- 2026-10-07 `scheduler/009-callable_release_throws.phpt`, `scheduler/022-await_finished_outcome.phpt`,
  `scheduler/088-unobserved_exception_of_global_printed_at_end.phpt`,
  `scheduler/089-unobserved_exceptions_printed_at_end_whatever_holds_them.phpt`,
  `scheduler/091-unobserved_exception_throwing_tostring_does_not_stop_the_rest.phpt`,
  `scheduler/092-exit_exception_not_printed_again_for_a_holder.phpt` and
  `scheduler/102-unobserved_exception_tostring_not_called_after_one_bailed_out.phpt`
  (`changed:2026-10-07`) start every coroutine with `suspend()` before the first one fails, and main
  yields once more. Why: with S9.3's route an unawaited error in the global scope cancels the
  coroutines that have not started (Edmond's option 1 above), so their sibling or reader never ran;
  the reference build prints the same as ours for the changed tests, but for S3's report of the
  unobserved ones.
- 2026-10-07 A scope's disposal moves its handlers' closures and objects into an array released
  after the scope and its parents are freed (`scope_handler_keep_back`, S9.3, `scope/073`). Why:
  a closure's release may free the last holder of the parent scope while `scope_dispose` still
  reads it (the Critic; heap-use-after-free under ASAN at `scope_dispose`).
- 2026-10-07 A fatal error in a scope's exception handler takes the coroutine out of its scope
  before the bailout goes on (`zend_try` around the call in `async_scope_catch`, S9.3,
  `scope/072`). Why: the bailout skipped the scope removal that finalize does after the route, and
  the coroutine's free asserted on a scope still set (the Critic). The `zend_try` is on the error
  route only, not a hot path.
- 2026-10-07 Core `async-core-io-2026-10-07-2` (`8159f7baa5c`): `async-core` `6e43d6074e0` and
  `php-src-fixes` `cfa0923ac31` merged onto `async-core-io-2026-10-07`; ior unchanged. `async-core`
  fixes the GC threshold: every coroutine that found the root buffer full awaited the same GC
  coroutine run and then raised the threshold by a step (12000 coroutines, threshold 90020001); now
  the GC coroutine takes one step after its run, as TrueAsync's core does (`c8acbdccc14`, core tests
  `ext/test_scheduler/tests/092`-`094`). `php-src-fixes` brings php/php-src#24168's `running_calls`
  field and #24177 (`scope/075` loses its `--XFAIL--`). `scope/058` kept `zend.enable_gc=0` for the
  coroutines parked on a full buffer (the GC priority entry below). Compared on main `73a8469`:
  `pocs-dbg` 1049 PASS, 9 SKIP, 62 XFAIL, `pocs-asan` 1032 PASS, 31 SKIP, 58 XFAIL, nothing
  unexpected. Why: the threshold bug grew the root buffer to about 720 MB and made `scope/058`
  quadratic on ASAN.
- 2026-10-07 `awaitCompletion()` finds a waiter that belongs to the scope by walking up from the
  waiter's scope (S9.4, `scope/045`, `051`). Why: membership is the parent chain, so the answer is
  TrueAsync's, in the depth of the waiter's scope instead of the subtree's size, with no depth limit
  to refuse (S9-scope.md 9 item 12). `awaitCompletion()` in a finished coroutine throws, as
  `await()` does there (item 13).
- 2026-10-07 The `await_*` iterator coroutine's scope is cancelled from a finish handler when the
  coroutine ends with an exception (S9.4, `await/062`). Why: a cancel from inside the running
  coroutine makes the cancellation its outcome, and the Traversable's exception would carry it as
  its previous; TrueAsync's waiter sees no previous.
- 2026-10-07 Kept as TrueAsync (the Sage, S9.4): with safe disposal the cascade's first zombie
  mark wakes a waiter in `awaitCompletion()` as completed, before the cancellation's or the error's
  wake, so the waiter returns and the error goes on to the parent (`scope/082`, `083`); a second
  `cancel()` closes a scope whose coroutines still run, and `awaitCompletion()` then returns at
  once (probe `s9.4/w3.php`). Rejected: skipping the completion wake of a cancelled scope (the
  Critic), which never wakes S9.5's `awaitAfterCancellation()` (`scope/024`); closing on the counts
  only, which cancels the members again inside their cleanup. Edmond may want the safe case to
  throw as the unsafe one does.
- 2026-10-07 The collector's edges for scopes move from S9.4 to a step of their own, S9.9. Why:
  they need S7.7, which Edmond questioned on 2026-10-07; the SCOPE kind has no `collector_target`
  meanwhile, so its waiter is never reported, never wrongly.
- 2026-10-07 The collector leaves out the route of an unhandled error through the scopes, at every
  level, as it leaves out the registry's walks (S7.7, S7.md 2 and 10; reversible). Why: the route
  cancels each level's subtree with the origin's safe flag, read when the error happens, and any
  live coroutine of a tree can make an unsafe origin later, so counting it makes a tree all or
  nothing: a server's `new Scope()` with a live accept loop would never find a stuck handler (the
  Critic). Edmond agreed ("продолжай работу"). A `SpawnStrategy`'s hooks, which get the current
  scope's object, are left out the same way (the Critic, S7.7).
- 2026-10-07 A held `Scope` object makes the coroutines of its scope and of its child scopes
  reachable, through one reach node per scope that no count makes live (S7.7, S7.md 10). Edmond
  agreed ("продолжай работу") after explaining that a scope belongs to no one and a coroutine's +1
  belongs to the scheduler, so the walk never follows a scope's members as references. TrueAsync has
  no partial-deadlock walk to compare with (`resolve_deadlocks()`, php-async `scheduler.c:749`).
- 2026-10-07 Core `async-core-io-2026-10-07-4` (`a6fbd0776e9`): `8159f7baa5c` with the commit of
  `io-hooks-connect-started` (`dca62e39da4`, bukka/php-src#4, on bukka's `2bf149c2f50`) picked, not
  bukka's head merged, so the core takes only this fix. After its own `connect()` the core waits
  for write readiness from the kernel and reads `SO_ERROR`, as `php_network_connect_socket()`
  does without hooks, instead of submitting the Connect op again: IOCP's `ConnectEx()` on a
  socket with a connect under way fails with WSAEINVAL (`socket_ext/006` on Windows). Five Critic
  passes. `pocs-dbg` 1080 PASS, 0 unexpected; `pocs-asan` 1058 PASS, `io/046` failing under the
  full lane's load only (passes alone and with `io`, `exec`; its child gets 500 ms); `pocs-win`
  986 PASS, 0 unexpected from the fix. Why: the BUKKA rule, our core fixed while the PR waits.
- 2026-10-07 The collector's run goes to the front of the run queue (Edmond's idea, the Sage's
  form): the core's `gc_new_coroutine` slot takes `zend_coroutine_priority` (`async-core`
  `6f30767dd7c`, API version 3, RFC-CHANGES 15), HI for the run and NORMAL for the destructor
  iterators of the run and of the shutdown passes; `scheduler_gc_new_coroutine()` turns HI into
  `ASYNC_COROUTINE_F_HI_PRIORITY`. TrueAsync gives HI the other way round, to its destructor
  coroutines (`gc_ta.c:2137`): its callers never wait for the run, ours do. The waiter wakes at the
  tail, and a run that resumes after its destructor phase too, as in TrueAsync. Supersedes
  EDMOND-DECISIONS 20 for the GC coroutine. `gc/025` runs 100 000 coroutines with GC on, skipped
  on the fuzz lane, whose random pick ignores the priority; `scope/058` keeps GC off for that lane.
  Rejected: the waiter at the front as well (it breaks the FIFO of the shutdown passes and
  `scheduler/062`); TrueAsync's caller that does not wait (a synchronous loop of 3 000 000 cycles
  exhausts 128M). Why: 100 000 coroutines parked on a full buffer passed `vm.max_map_count`.
- 2026-10-07 Tests for the run at the front: `gc/007-gc_destructor_complex_async_ops.phpt` matches
  the reference again; `gc/002-gc_destructor_spawn_coroutine.phpt` (`changed:`) prints "After GC"
  between the two runs of the coroutine spawned in the destructor, because main wakes at the tail
  behind it (the reference prints it before both). Supersedes the 2026-10-02 entry's "a coroutine
  queued before the GC coroutine runs first"; `gc/011`, `gc/012` collect with nothing else queued
  and keep their order. `scheduler/062-collection_in_handler_while_unrun_coroutine_finishes.phpt`
  (`changed:`, title and comment): the cancelled coroutine is popped after the run, on the fiber
  that ran it, not on main's stack. `scheduler/107` pops one on main's stack from an `await()` in a
  destructor, with the call's internal frame current; the pop under main's own user frame in the
  middle of an opline is reachable only through the fuzz lane's random pick (the Critic).
- 2026-10-07 A waiter of the GC run returns that run's count (`async-core` `50cd33b0eec`,
  `gc/024`): the run stores it in its coroutine's result, and the waiter holds a reference to the
  run, released under `gc_active` (released outside it, the root it added to a full buffer started
  a run per release: `scheduler/058`, `061` crashed). The await slot's caller holds the reference
  and the slot takes none (`scheduler_await()`, test_scheduler's `ts_await()`), so no release on
  the waiter's return starts a run. Why: with the run at the front, a coroutine queued before the
  woken waiter can finish a second run first, and the global count gave the waiter its 0 (the
  Critic).
- 2026-10-07 Core `async-core-io-2026-10-07-5` (`3af71f889e6`): `async-core-io-2026-10-07-4` with
  `async-core` `50cd33b0eec` merged (the two GC entries above; API version 3, so each track rebuilds
  its core). `pocs-dbg` 1082 PASS, 9 SKIP, 48 XFAIL; `pocs-asan` 1063 PASS, 32 SKIP, 44 XFAIL; nothing
  unexpected; the fuzz lane, 3 to 5 seeds over `gc/024`, `gc/025`, `scope/058`, `scheduler/062`,
  `107`, finds nothing new. Probe `s9.4/gcmaps.php`: 10250 maps at 40 000 coroutines, 28251 at
  100 000 (TrueAsync 16053, 40054; before, 25254 and "Fiber stack protect failed").
- 2026-10-07 A waiter in `Scope::awaitCompletion()` is live once any coroutine of the awaited
  subtree is, zombies included, through one completion node per awaited scope and run (S9.9,
  S9-scope.md 6, `src/scope.c` `scope_record_collector_target`). Why: any member can wake it, by
  finishing or by an error whose route passes the scope; an edge per member per waiter, the first
  design, makes the run's edges the product of the two (the Critic: 10 million for 10 000 members and
  1 000 waiters), and a run that hits the memory ceiling finds nothing. Two reporters added to S7's
  `src/collector.h` for it: `async_collector_report_reach_target()` and
  `async_collector_report_reach_source()`; the S7 track is closed, S7.md 10 updated.
- 2026-10-07 Two cancels that hold no Scope object, found by S9.9's Critic: the route's
  `scope_hand_out_found()` also hands out the found waiters of every scope it visits, as the route
  is left out (`scope/090`, an abort of the oracle before); the `await_*` iterator coroutine is the
  holder of its scope's reach node until it finishes (`async_scope_t.iterator_coroutine`), since it
  cancels that scope when the walk throws (`scope/091`: S7.7 found the subtree's members, a false
  finding).
- 2026-10-07 The oracle for a scope's waiters runs at the notify sites, against the member that
  finished or became a zombie, or against the running code for a cancel (S9.9). Why: the notify runs
  its callbacks in scheduler context, so a record's wake cannot tell who woke it, and the bailout's
  excuse never applied there (the Critic). The FUTURE record's `async_collector_check_event_wake()`
  (`src/future.c`) runs inside a notify too, so its bailout excuse never applies there either; a
  probe of a fatal error after a run found a Future's waiter (`s9/probes/s9.9/fb.php`) did not abort,
  so it is left as is and named for S9.7's fuzz pass. The `cancel` policy now hands out main too,
  which it leaves uncancelled, when it cancelled another: a coroutine it cancels can wake main, found in `awaitCompletion()`,
  from its cleanup (`scope/093`, an abort before; S9.9's second Critic). Rejected: excusing every
  handed-out completer, since `get_coroutines()` hands out the caller too and would switch the
  oracle off for it (the third Critic). Left for S9.7: a coroutine the route's subtree hand-out
  woke may wake a found waiter outside that subtree.
- 2026-10-07 A `provideScope()` declared to return by reference works: `scope_provide()` unwraps the
  reference (S9.9, `spawnWith/017`, S9-scope.md 9 item 14). TrueAsync rejects a valid null or Scope
  there. Found by the S7 thread's Critic.
- 2026-10-07 `tools/check-gates.py`'s ban on "the event embedded in a coroutine" (`->event.`) no
  longer matches a scope's own event (`scope->event.`, `..._scope)->event.`), which `src/scope.h`
  embeds; the gate failed on main since S9.2 for that alone (S9.9).
- 2026-10-07 `tests/io_provider/009-cancel_after_completion.phpt` repeats an attempt whose reader
  resumed before the canceller, after checking that order too (the read returns the bytes, nothing
  stays behind), and shows the bytes in the stream's buffer at the cancellation. Why: the test
  relied on the Ring completing the canceller's socket before the reader's, an order the kernel does
  not promise; the S9.5 thread saw the reader first under load (core `3af71f889e6`). Judged by the
  Critic, who asked for the check of the other order and the buffer count.
- 2026-10-07 `Scope::dispose()` and `disposeSafely()` are `cancel()` with no error, the scope's flag
  or `is_safely` true, as TrueAsync's `ZEND_ASYNC_SCOPE_CLOSE` (S9.5): a scope whose members still run
  is cancelled, not closed, and still accepts a spawn (probe `s9/probes/s9.5/d6.php`, `scope/101`).
  Why: Edmond, "сделай как в TrueAsync" (S9-scope.md 12); no reference test tells it apart.
- 2026-10-07 `disposeAfterTimeout()` arms one S4 Timer op per scope (`async_scope_t.dispose_timer`,
  the earliest deadline wins), on the reactor's waits, and its fire cancels the scope in the
  timer's notify with "Scope has been disposed due to timeout" (S9.5, S9-scope.md 9 items 15, 16).
  TrueAsync spawns a coroutine of the global scope to cancel and arms one timer per call, each holding
  a reference to the scope. Why: the cancel only queues; a coroutine adds a registry entry, a holder
  for the collector, and a safe cancel of the global scope would cancel it unstarted and drop the
  timeout. The scope's free withdraws the timer, so a disposed scope no longer keeps the script
  waiting, where the reference waits and leaks 32 bytes (probe `d8.php`). While armed it marks the
  scope's reach node live, a reporter added to S7's `src/collector.h`
  (`async_collector_report_live_reach()`): it may cancel the subtree, and no walk reaches it
  (`scope/098`).
- 2026-10-07 `Scope::awaitAfterCancellation()` waits until no coroutine of the scope's subtree is
  left, zombies included (S9.5, S9-scope.md 9 item 17). TrueAsync returns at the first member's end
  (probe `d3.php`), against its own comment and the thread pool's use of it. Its error handler runs in
  the waiting coroutine after the wake, which may suspend there, not in the finishing coroutine inside
  the notify (D26: a record's wake only enqueues; a notify runs in scheduler context). The error
  reaches the waiter as its waker's error, as in `awaitCompletion()`: the route brings no
  cancellation of a member's own and a cancel always brings one, so the waiter tells them apart by
  class, needing no field, and its own cancellation keeps the error in its chain of previous
  (`scope/105`). A member's end wakes it only once the subtree is empty. An error that comes while the handler runs, before the wait is linked
  again, climbs on as if nobody waited (`scope/104`); the reference loses every error after the
  first that way (probes `d4.php`, `d5.php`). Why not a scope-held intake of errors (the Critic's
  proposal): the Sage's verdict, two new scope fields the reference lacks, and errors queued for a
  waiter that is then cancelled would vanish, where now they climb to a handler or the request's end;
  `setExceptionHandler()`, which the route calls first, sees every error. Known and kept (the third
  Critic): an AsyncCancellation that a scope's handler throws in place of the error is thrown from
  the wait as a cancellation, with the error in its chain; uncaught it is not reported, as no
  cancellation is, where without a waiter the error ends the request. A cancelled scope is waited on
  even once it is closed (`scope/107`), where the reference returns at once. A closed scope that is
  not cancelled returns at once, as on the reference, even while coroutines run in its child scopes: a
  cancel closes an idle scope without cancelling it (`scope/109`, `scope/110`). The awaited scope's own
  CANCELLED flag decides; below a cancelled one every coroutine counts, cancelled or not. Left for
  S9.7: the route marks a closed scope cancelled, which turns that at-once return into a wait. Why: waiting
  there needed either counting only cancelled subtrees, which a member's end below an open busy scope
  never notifies, or a second, ungated notify walk with a waiter flag (the third and fourth Critic);
  the reference's rule needs neither.
- 2026-10-07 Closing a scope withdraws its `disposeAfterTimeout()` timer, whose fire would find it
  closed, and a cancelled scope's timer no longer marks the reach node live, since its fire only
  closes the scope (S9.5, the Critic; `scope/107`, `scope/108`). After a fork the parent's timer counts as unarmed,
  as `timeout.c` checks it, so a later call arms its own (`scope/106`).
- 2026-10-07 `tools/test.py` refuses a core whose ior runs a backend other than io_uring, or other
  than the thread backend under `IOR_BACKEND=threads`. Why: CI runs io_uring, and a cloud container
  without `liburing-dev` built a core on the thread backend whose ASAN lane failed 10 fork tests for
  reasons outside the extension (`dev/WORKFLOW.md`, "Building the core"), the failures the S9.5 run
  saw on clean main. The check asks the core, so it also catches io_uring refused at run time and a
  mistyped `IOR_BACKEND`, after which the reactor takes the Poll queue.
- 2026-10-07 A coroutine's finally handlers start after its error's route and the unheld check, so
  they run when that error cancels the scope or ends the request; a run's collected error becomes
  its last worker's own and goes up from the run's child scope; `exit()` in a handler ends the
  request (S9.6, note section 9, items 18-20). Why: TrueAsync's order lets the cascade cancel the
  handlers unrun, and its two error paths (the target scope's catch, then a rethrow) pick a handler
  by accident. Rejected: a completion event for the run, which nothing in this layer waits on.
- 2026-10-07 After a fatal error a scope's finally handlers run in the scheduler's last run after the
  shutdown destructors, as TrueAsync's; a scope disposed while the bailout unwinds the coroutines drops
  them unrun, as the reference does; the bailout rule stays (S9.6, `bailout/013`-`017`).
- 2026-10-07 S9.7's backlog. The fuzz oracle excuses an event's wake when the completer was found by
  the run that found the waiter (a run id per find, test hooks only), and checks a Future's and a
  scope's waiters before the notify, which runs in scheduler context (`collector/073`, `077`). The
  `cancel` policy's back-off resets on a run that cancels a coroutine no run cancelled before, by a
  flag of its own: a safe cancel sets a zombie's cancelled bit without waking it (`collector/074`).
  A forked child rebuilds its reactor at the scheduler's idle point and in
  `get_deadlocked_coroutines()` before either reads the parent's waits; a rebuild inside the
  collector's run would leave `async_reactor_wait_idle()` with no waits (`collector/075`, `076`). A finish handler that replaces a
  coroutine's exception gives it a new outcome, which takes the route when no waiter was woken: the
  iterator's unrun last worker ends that way. Kept as TrueAsync: the route marks a closed scope
  cancelled (catch_or_cancel does the same), so `awaitAfterCancellation()` waits for its subtree as
  for any cancelled scope (`scope/118`); an AsyncCancellation a scope handler throws keeps the error
  in its chain (`zend_exception_merge`, `zend_common.c:109`). For S9.8: a chain of 50 000 nested
  scopes overflows the C stack on both; a refused finally start inside `async_scope_cancel()`'s loops
  releases handlers whose destructors run PHP code.
- 2026-10-08 S9.8's security pass. Finally handlers whose run the scheduler refuses stay with their
  scope: a closed scope's disposal starts them again, a disposal's own refusal releases them unrun
  after its walk. Why: released at the refusal, their destructors ran inside a cancel's cascade and
  could make it skip a child scope (`scope/123`). A child scope keeps its index in its parent's vector,
  and a member's finish skips the child scope its walk up came from (section 9, items 1 and 27).
  Accepted with TrueAsync: the recursive subtree walks, the per-level completion test of a cascade and
  the sibling rescans (`dev/SECURITY.md`, 2026-10-08); an iterative walk by the child index is noted,
  not built.
- 2026-10-08 `scope/058-scope_many_members_leave_out_of_order.phpt`: one coroutine in a hundred
  yields once, not one in ten; every tenth still runs until the cancel, and GC stays off.
  Reversible; the coordinator's call, not Edmond's. Why: Windows commits each 2 MB fiber stack in
  full (`Zend/zend_fibers.c:234` of async-core `f6f3eb6e44b`; TrueAsync's core the same, line 236
  of branch `true-async`), and 20 000 suspended coroutines, about 40 GB, passed the CI runner's
  commit limit ("VirtualAlloc failed: [0x000005af]") in each `pocs-win` run of main checked (CI
  runs 126, 130, 131, 132); a test run beside it died at the same limit (`scope/066` "Can't
  initialize heap", `scope/085` 0xC000012D). On the Linux debug build (`VmPeak`) the old version
  peaks at 40.6 GB in 20 013 mappings of 2 MB or more, the new one at 22.4 GB in 11 013;
  `gc/025`, the one-in-ten loop with GC on, peaks at 28.4 GB and passes on the runner. The
  expected output is unchanged. `scope/058` and
  `gc/025-gc_run_first_keeps_100000_coroutines_from_parking.phpt` take `--CONFLICTS--`
  with the key `fiber_stacks`, so run-tests never runs them together: the commit limit is the
  machine's. Rejected: GC on outside the fuzz lane, because main then resumes after each GC run of
  the spawn loop behind the coroutines spawned so far, and the scope holds 49 001 coroutines at the
  loop's end instead of 100 001; a SKIPIF on Windows, as `collector/064`, kept as
  the fallback; a smaller `fiber.stack_size` for every lane, because ASAN reserves 480 KB of it
  (`OnUpdateReservedStackSize`) and a smaller stack throws at once. Committing a fiber stack on
  demand is a core change (PLAN, Open questions).
- 2026-10-08 `scope/106` and `scope/107` take `skip-on:pocs-win(pcntl=no_Windows_build)`, as the
  fork tests of S4: they need pcntl, which the Windows build lacks.
- 2026-10-08 The scope object reports its handlers to the GC only while no coroutine is in the scope
  or in its child scopes, and goes back to the GC's root buffer when a coroutine leaves the scope
  without coroutines (S9-scope.md 9 item 28, `scope/126`). This replaces the entry of
  2026-10-07 that reported them while the object has its scope. Why: a coroutine reaches the
  handlers through the error route, which no object reports, so a handler's closure that holds the
  object let `gc_collect_cycles()` cancel the running scope of a `new Scope()` (the member got "Scope
  is being disposed due to object destruction"), or make the member of an inherited scope a zombie.
  `scope/070-handler_closure_cycle_with_zombie_member.phpt` (`changed:2026-10-08`) expected that
  zombie and a stand-in in the handler; now the object stays live and the handler gets it. Rejected
  (the Critic): no report while a child scope exists, which never collects a cycle through a child
  scope without coroutines. Accepted, as TrueAsync: a GC run while a held child scope is idle can still
  close the parent whose handler holds the parent's object (S9-scope.md 9 item 28).
- 2026-10-08 A Fiber's coroutine joins no scope, Edmond's answer to question 1 of
  `dev/plans/S9-context.md` (section 9, option 2): `spawn()` in a Fiber goes to the global scope and
  `current_context()` there is the root context. A departure from TrueAsync's fork, which puts it into
  the current scope at `new Fiber()` (`Zend/zend_fibers.c:1315-1318` of `863f6dd90cf`). Why: a Fiber
  runs code that does not know about scopes (an event loop), and on our core a cancelled Fiber may
  suspend again (D5), so a Fiber in a cancelled scope could hold its disposal for good; destroying a
  Scope object would end an event loop's Fiber first made inside it.
- 2026-10-08 S9.11: `Async\Context` is the core's `zend_async_context_t` itself, with no field of
  ours in front; S9.12 adds the scope pointer with the walk that reads it. The tables are destroyed
  in `free_obj`, after `zend_object_std_dtor` clears the WeakReferences, and `get_gc` reports every
  key object and value (note section 7, item 2; `context/014`-`016`). `coroutine_context()` with no
  current coroutine throws AsyncException "The current coroutine is not defined", as
  `current_coroutine()`; `Coroutine::getContext()` checks nothing, as TrueAsync's (but for the
  object being freed, next entry).
- 2026-10-08 `current_coroutine()` and `coroutine_context()` throw "The current coroutine is not
  defined" while the current coroutine's object is being freed (`IS_OBJ_FREE_CALLED`), and
  `Coroutine::getContext()` on that object throws "The coroutine is being freed". Why: finalize
  drops a finished coroutine's last reference while it is still current, its `free_obj` runs a WeakMap
  value's destructor, and the engine frees the object after `free_obj` whatever its refcount, so the
  object `current_coroutine()` returned there was freed under its holder, and a context made there
  leaked to the request's end (`context/020`). The core's own `zend_async_context_get()` has the same
  window for C callers: `dev/RFC-CHANGES.md` 17.
- 2026-10-08 `await/069-await_manual_vs_timeout_cancel.phpt`: the second await's token is
  `timeout(20)`, not `timeout(1)`; Edmond's word in the S9 Context thread. Why: `timeout()` takes its
  deadline when it returns (D32), so when more than 1 ms passes before the await subscribes (a loaded
  debug run, 1 in 200 with three busy cores) the await throws at once and the coroutine spawned before
  it is cancelled unstarted ("timeout coroutine started" missing); a 2 ms `usleep()` between
  `timeout(1)` and `await()` reproduces it every time. 20 ms stays below the coroutine's `delay(50)`;
  the expected output is unchanged.
- 2026-10-08 The provider answers a POLL whose deadline has passed (`feof()`'s zero timeval the common
  case) on the core's synchronous path and does not park. Why: on ior's IOCP backend the due link
  timeout can complete before the poller thread's WSAPoll sees the readiness, so `feof()` on Windows
  missed a peer reset (`stream/046-write_wakes_on_peer_reset_win`, CI run 37744080113); TrueAsync polls
  a zero timeout synchronously (`php_poll2_async()`). ior's IOCP backend still races for a deadline not
  yet passed: PLAN Open questions.
- 2026-10-08 S9.12: a scope object reports its handlers and its context to the GC only while nothing
  but the object reaches the scope: no coroutine in it, and every child scope can be disposed (no
  coroutine, and no object or cancelled). A freed child scope gives its parent's object back to the
  root buffer. Why: an idle child scope the script holds reaches the parent's context through its
  context's walk and the parent's handlers through its spawns, and a GC run then closed the parent,
  so a later spawn into it threw (the Critic; `context/037`, `scope/127`). This replaces "accepted, as
  TrueAsync" in the entry of 2026-10-08 on the handlers. Not collected: a parent whose own context or
  handler holds its child scope's object, as TrueAsync collects no cycle through a context: it lives,
  and its finally handlers wait, until the request ends. Reversible. Edmond (08:12,
  «если тест устарел - меняй его»): `tests/scope/126-handler_cycle_survives_gc_while_members_run.phpt`, last case, now
  expects "alive: true"; the other way, a child scope's object holding its parent's, would keep a parent
  object the script dropped from its destructor, unlike TrueAsync.
- 2026-10-08 S9.12: `current_context()` throws "The current scope is not defined" while a finished
  coroutine that left a scope other than the global one releases what it held, as TrueAsync on a NULL
  scope (`async.c:811-813`): the global scope's context would hand a `new Scope()`'s destructors the
  root values (`context/038`). One that left the global scope reads the root context; the flag
  `ASYNC_COROUTINE_F_LEFT_NON_GLOBAL_SCOPE` tells the two apart. `spawn()` and `Scope::inherit()` in
  that window use the global scope, as TrueAsync. `released_handlers` of `src/scope.c` is
  `released_values`: it carries the freed scopes' contexts too.
- 2026-10-08 S9.13: RSHUTDOWN releases the user values its teardown drops (scope handlers, contexts
  and objects, the registry's coroutine objects, the unobserved exceptions) as its last step, after
  the IO provider, signal and reactor teardown: a destructor that throws there bails out of the rest
  of RSHUTDOWN (`context/040`, `042`, `044`). The test hook `TrueAsync\Test\print_at_teardown()`
  prints "teardown: done" before that release. Not as the note's test: an output handler reaches no
  value the teardown releases (async is off there, and a Context it holds outlives RSHUTDOWN); a
  coroutine spawned from a shutdown destructor does. Found by the Critic, fixed here: a destructor in
  that release that maps a Future wrote into the freed registry (`future_drain_spawn` now refuses
  once async is off, `context/041`; before S9.13 it read a NULL global scope), and
  `Scope::disposeAfterTimeout()` there made a new reactor queue that nothing freed (it now does
  nothing once async is off, `context/043`).
- 2026-10-08 The tests that need a refused fiber stack ask for 1 PiB (`fiber.stack_size=1048576G`)
  instead of 64G: beyond the 128-256 TiB that mmap without a hint and VirtualAlloc can place, so it
  is refused on every system. Why: 64G was mapped with Linux `vm.overcommit_memory=1` (checked) or
  mode 0 on a host with more than 64 GB of RAM and swap, and on Windows with php/php-src#24190, which
  reserves a fiber stack and commits it on demand (10 failures on Edmond's PC; scope/123 was not in
  that run). Not reverted with #24190. Edmond (09:03, «тесты пока временно поправь»). Tests:
  scheduler/005-no_stack_for_coroutine.phpt, scheduler/007-no_stack_with_exception_handler.phpt,
  scheduler/016-no_stack_full_gc_buffer.phpt, scheduler/017-no_stack_in_shutdown_suspend.phpt,
  scheduler/018-no_stack_in_coroutine_suspend.phpt, scheduler/065-no_stack_refusals.phpt,
  scheduler/081-suspend_refused_without_stack.phpt, scheduler/086-core_coroutine_refused_released.phpt,
  scheduler/087-refused_core_coroutine_not_a_waiter.phpt,
  scheduler/103-deadlock_report_skips_refused_core_coroutine.phpt,
  scope/123-refused_finally_start_leaves_cancel_cascade_whole.phpt.
- 2026-10-08 `io/080-append_empty_file.phpt` and `curl/064-stderr_file_reuse.phpt` (S6.txt,
  `changed:2026-10-08`) use a file beside the test instead of a `tempnam()` name; the expected output
  is unchanged. Why: on Windows `tempnam()` keeps three characters of the prefix and a 16-bit number
  from the clock, and a name stays reserved only while its file exists; both tests deleted their file
  and used the path again, so a parallel test could take the same name (`io/074` did, on Edmond's PC).
  Reversible; the coordinator's call.
- 2026-10-08 S9.14: `current_context()`'s refusal in a finished coroutine's release window (S9.12)
  stays, as TrueAsync, whose reference build prints the same uncaught "The current scope is not
  defined" for a context value's destructor, which ends the request there too (probed). It covers
  only that window: a scope's values released by `dispose()` or `unset()` from main read the root
  context (probed on both builds), by a GC run the engine scope's, and a `spawn()` inside the window
  joins the global scope; a released value's destructor sees the context of whoever releases it (the
  layer review's Critic).
- 2026-10-08 S9.14: `tests/context/025-coroutine_context_made_while_values_released.phpt` (ours, S9.11)
  waits in a loop for the destructor instead of one `suspend()`: under the fuzz oracle's random order
  the coroutine had not run yet (the layer 1 test gotcha). Expected output unchanged; 100 of 100
  seeds print it.
- 2026-10-08 S9.16, layer 3: a channel bound to its owner scope (TrueAsync's binding) closes with
  `SCOPE_DISPOSED` when the scope is cancelled or destroyed, not when it completes: Edmond («делаем как
  в TrueAsync + закрываем канал когда Scope разрушается... но вот закончились корутины - нет... пусть
  сборщик мусора решает такой случай»). TrueAsync's code closes on every notify of the scope, its
  documentation promises "when that scope is disposed or cancelled". The close comes at the
  transition: the cancel's and the error route's notify, the branch where a completed or already
  cancelled scope is cancelled or disposed (`src/scope.c:547-570`, silent in both implementations),
  which closes the bound channels without a notify, and the scope's free.
  Accepted cost: a channel handed out of a completed scope, still held by live code, whose producer
  never closed it, keeps its receiver parked until the channel's timers or the global deadlock.
  Critic and Sage checked it; the Sage ran the rule on the reference: `channel/`, `edge_cases/015` and
  `scope/` pass. Details: `dev/plans/S9-channel.md` sections 5 and 8.
- 2026-10-08 S9.16, layer 3: a CHANNEL wait record stays linked from its wake until its frame takes it
  out, and a cancel's or a token's wake leaves it in the channel's queue, as TrueAsync's waiter (its
  close and its delivery acknowledgement take it out of the queue) and as the waits table of
  `dev/plans/S3.md` (690) planned; an
  exception to D26 for that kind, and `async_wait_record_unlink()`'s assert exempts it: Edmond («я думаю
  1»). Why: a scope's cancel queues its coroutines before its notify, so a close at the notify must still
  find them (`channel/048`, `050`-`053`, `063` fail otherwise, probed on the reference by the Sage), and
  `abort` reaches only linked records (S3 4.4's bailout case). Details: `dev/plans/S9-channel.md`
  section 3.
- 2026-10-08 S9.17: `edge_cases/015-deadlock-caught-still-terminates.phpt` sets `true_async.debug_deadlock`
  instead of `async.debug_deadlock`, as `edge_cases/001`-`003` and `010` did. Why: INI names take the
  module prefix (2026-10-01).
- 2026-10-08 S9.17: the CHANNEL record that stays linked for its frame (the entry above) is linked by
  `async_wait_link_outside()` with the record flag `ASYNC_CALLBACK_F_FRAME_UNLINKS`, which
  `async_wait_record_unlink()` leaves. The alternative was an `F_TYPED` kind whose unlink does nothing,
  plus an exemption in the assert that follows that unlink. Why: the record is in no vector, so
  `async_wait_link()`'s push could not be used either, and a flag of its own states the case in one test;
  the behaviour Edmond agreed to is the same. `async_wait_end()`, which ends the wait of a frame that
  never ran again, aborts such a record first, since the unlink leaves it (the S9.17 Critic).
- 2026-10-08 S6.10 builds S6.md 9.1 as agreed (Edmond «согласен», 07:25): the switch
  `php_io_overlapped_pipes`, set in MINIT when the extension is built with ior; STDIN and `exec`
  pipes unchanged. A pipe handed to a child is taken off the Ring's completion port right before
  `CreateProcessW()` through the new queue op `release()` and `ior_release_handle()`, and ior keeps
  dropping packets that are not its ops (Edmond's choice on the card, 14:31, after his objections to
  the filter alone: an address can match a slot by chance, and one process's I/O should not reach
  another's port). The filter does not compare the completion key: an op on a recycled handle value
  can complete under another key, and a key check would drop its packet and hang it (the Critic).
  The Ring ignores the release's answer (`-EBUSY`, `-ENOTSUP`: the filter covers those); a pipe with
  an op in flight fails `proc_open()` as a concurrent access, as the cast does. Not done: a free
  list that keeps a recycled op slot from matching a stale packet (the Critic), since with the
  release only a hand-out the core does not see posts there.
- 2026-10-08 `signal/031-reblock_unblocks_with_its_watch.phpt` and
  `signal/032-pcntl_signal_unblocks_until_the_next_poll.phpt` (ours, S6.9 and S6.5;
  `changed:2026-10-08`) expect a watched number to stay blocked under `pcntl_sigprocmask()` and
  `pcntl_signal()`: bukka's `bdfa5fa7a12`, in the core from `async-core-io-2026-10-08-2`, keeps it
  blocked in pcntl, the main part of `RFC-CHANGES.md` 5. `032`'s delivery now reaches the Future and
  not the handler. Why: the tests recorded the gap the core change closes (Edmond 08:12, «если тест
  устарел - меняй его»). `async_signal_reblock()` stays: other callers of `zend_sigaction()` and
  pcntl's request shutdown still unblock.
- 2026-10-08 `awaitable_gets_implemented()` returns void: `interface_gets_implemented` does since the
  php-src master that bukka's `io_hooks_poc` head merges.
- 2026-10-08 The `pocs-win` lane loads `php_sockets.dll`, `php_openssl.dll` and `php_curl.dll` from
  `TRUE_ASYNC_WIN_BUILD` when they exist (a snapshot build, CI's Release_TS, makes them shared; a
  Debug_TS build of `tools/windows` links them in), and the `skip-on:pocs-win(...-not-loaded-until-S6.x)`
  tags are dropped from the lists. Why: S6.10's done line.
- 2026-10-08 `dns/005-dns_error_handling.phpt` (reference; `changed:2026-10-08`) skips on Windows, with
  `skip-on:pocs-win`. Why: Windows resolves an empty host name to the local host's addresses, and
  PHP without the extension prints the same (`php -n`, Edmond's PC, S6.10); the test expects `''`
  and `false`. A tag alone only allows the skip, so the test's `--SKIPIF--` gives it.
- 2026-10-08 `io_provider/026-feof_zero_timeout_poll_does_not_park.phpt` (ours, S6.8;
  `changed:2026-10-08`): the client waits for the peer before it closes. Why: on Windows the peer's
  TLS accept failed in 20 of 20 runs when the client closed first, and passed in 5 of 5 with the
  socket kept open (Edmond's PC, S6.10); the test checks `feof()`, not the close.
- 2026-10-08 `stream/001-fread_fwrite_simple.phpt` and `stream/002-fwrite_simple.phpt` (Windows only)
  stay `--XFAIL--`, now by design: a socket write the kernel takes at once returns without
  suspending the coroutine, since the core sends first and submits an op only after `EAGAIN`
  (S6.md section 4); TrueAsync's fork waits for every libuv write on Windows.
- 2026-10-08 `collector/027-future_state_kept_by_running_coroutine.phpt` and
  `collector/036-waiter_with_timeout_keeps_state_of_other_waiter.phpt` (ours, S7;
  `changed:2026-10-08`): two `suspend()` calls after the main script's `delay()`, before it prints
  `end`. Why: under load on Windows the main script printed `end` before the waiter the first
  coroutine woke (each failed once in two `pocs-win` runs and passed 10 of 10 alone, Edmond's PC,
  S6.10); inferred cause: both timers fire in one tick, and the wake of the waiter queues behind
  the main script. The tests check what the collector reports, not the order of the two timers.
- 2026-10-08 `spawnWith/013-spawnWith_strategy_scope_without_object.phpt` (ours, S9;
  `changed:2026-10-08`): the two waiting hooks start their `delay(10)` only once both are in
  `afterCoroutineEnqueue()`. Why: on Release_TS under load the first hook's timer ended before the
  second hook began, which printed its `before:` line after the first `after the wait:` (once in a
  `pocs-win` run, 10 of 10 alone, Edmond's PC, S6.10); the test checks that the stand-in stays
  usable while two hooks wait on it, which needs both waiting at once.
- 2026-10-08 S10 outcomes (`dev/plans/S10.md`; Edmond 17:40, «да в целом согласен с планом»):
  per-coroutine output buffers are not built (Edmond: «можно не делать ваще»), `output_buffer/001`-`006`
  stay excluded; `Fiber::getCoroutine()` is added to `Fiber` by the extension at MINIT; no
  `zend_sigaction` hook in the scheduler API, a `zend_signal.c` hook that `ext/standard` fills is
  requested from bukka, and the signal handle count is fixed in the extension; the PDO pool, pgsql and
  threads are not in the first version (the pool later as `ext/pdo` hooks by a PDO RFC with the pool in
  the extension; pgsql later as our own php-src PR; threads later at extension level); Windows signals
  in the extension through `SetConsoleCtrlHandler()`; Windows exec output, console reads and the M9 call
  sites are requests to bukka; FileSystemWatcher is a new S9 layer. Why: the note's sections 2-9; P1.1
  rules out a private core patch.
- 2026-10-08 `socket/001-socket_connect_ipv6_async.phpt`, `socket/002-socket_bind_ipv6_async.phpt` and
  `socket/003-socket_sendto_ipv6_async.phpt` (reference, `changed:2026-10-08`) probe the IPv6 socket in
  SKIPIF with `@socket_create()`, and their lines take `skip-on:*(no-AF_INET6)`. Why: without IPv6
  the probe's warning comes before "skip" and run-tests reports BORK, and the reference's `CI_NO_IPV6`
  never reaches a test through `tools/test.py` (the Critic of S10.2).
- 2026-10-08 S9.18: `close()` wakes each waiting coroutine and rejects each pending `recvAsync()` Future
  with a `ChannelException` of its own; `close_exception`, the channel's outcome as an Awaitable, goes
  only to the `await_*` items and tokens. Why: the waker chains a cancellation already queued for the
  waiter under the error it is given, and on one shared object, as TrueAsync's, that cancellation showed
  as `previous` for every other waiter and the outcome (`channel/103`). Edmond approved it on 2026-10-08.
  The channel as an `await_*` item and token is replaced by the 2026-10-09 S9.26 entry (`Completable` only).
- 2026-10-08 S9.18: `foreach` over a channel ends quietly on an explicit `close()` only when the
  `ChannelException` carries no previous, so a cancellation queued before the close propagates
  (`channel/104`), as `dev/plans/S9-channel.md` section 4 states; TrueAsync's iterator clears it.
- 2026-10-08 S9.19: a channel's timer is armed at a park, a wake and a wait's end, as TrueAsync's
  refresh, but only the park's submit may throw (its wait then ends before it parks); elsewhere the arm
  takes the reactor's errno-returning submit (`async_io_event_try_submit()`,
  `async_reactor_try_submit_own()`) and a failure leaves the channel without a timer until the next
  refresh. Why: a wake or a wait's end comes after a value moved, where a pending Error failed a committed
  `send()` or dropped the Future `recvAsync()` had just filled (the Code Reviewer of S9.19); TrueAsync's
  start leaves its error pending at every refresh.
- 2026-10-08 S9.19: `dropped_value` also takes the rendezvous value a timer's or the global deadlock's
  close rolls back, not only the owner scope's (`dev/plans/S9-channel.md` section 3), and `free_obj`
  releases it (`channel/127`). Why: the timer's fire and the scheduler's deadlock run in scheduler
  context, where no destructor may run, as in the scope's walks.
- 2026-10-08 S9.19: the collector's frame walk counts the TMP operand of a frame parked at
  `FE_RESET_R`, which keeps it through a `rewind()` or `getIterator()` that suspends while
  `zend_unfinished_execution_gc_ex()` leaves it out at that opline. Why: `foreach (new Channel(0) as $v)`
  and `foreach (producer() as $v)` parked in their first receive were never found (`channel/125`). A user
  `Iterator` whose `rewind()` parks stays a miss: the engine keeps its `zend_user_iterator` in a C local,
  as it keeps the iterator of `f(...$channel)`, `[...$channel]` and `yield from $channel` (the re-check
  Critic); a miss only hides a finding.
- 2026-10-08 S9.19: for a channel bound to a cancelled scope or one without an object, the collector also
  counts the holders of its child scopes' objects, recursively, as closers: the scope's disposal waits
  for its last child scope, and releasing a child's object disposes it and then the scope, whose free
  closes the channel (`scope_dispose()`).
  Why: without it a receiver was reported never to wake and then woke with `SCOPE_DISPOSED`
  (`channel/129`, the re-check Critic); `dev/plans/S9-channel.md` section 6 named only the subtree's
  coroutines.
- 2026-10-08 S9.19, reversible, not asked: the cancel or dispose of a completed scope closes only the
  channels bound to that scope, not those of a child scope a held object keeps, since that branch reaches
  no child, as TrueAsync's silent branch; a running parent's cancel cancels the child and so closes them.
  Why: Edmond's rule names the channel's own scope; the other reading is in PLAN's open questions (the
  Critic of S9.19). Replaced by the S9.20 entry on a completed scope's cancel.
- 2026-10-08 S10.3a, signal masks during a watch (Edmond 19:22 «я бы шёл по малопу пути. пока вообще не
  трогать маску», 19:46 «ок сделай пока так»): `pcntl_sigprocmask()`'s `$old` stays the real mask; bukka's
  pcntl filter records the script's unblock of a number it blocked before the watch, the last removal
  does it, and a later request with the number in the blocked set takes it back
  (`signal-unblock-at-removal`, `RFC-CHANGES.md` 19). The extension's `reblocked` record is gone:
  `async_signal_reblock()` records a `zend_sigaction()` unblock through
  `php_io_poll_signal_reblocked()`, and the core's count decides the unblock, so a number another
  `SignalHandle` still watches stays blocked (section 4 (c) of `dev/plans/S10.md`, `signal/035`).
  Why: no script ends worse than before the change; an unblock followed by a save-and-restore, and a
  block during the watch of a number the handle blocked itself, need the script's mask in `$old`
  and are questions to bukka.
- 2026-10-08 `signal/031-reblock_unblocks_with_its_watch.phpt` (ours, S6.9; `changed:2026-10-08`)
  expects the number unblocked after the watch when the script unblocked it during it, and probes
  the mask without `SIG_SETMASK`. Why: S10.3a does the unblock the test recorded as lost; a
  `SIG_SETMASK` probe during the watch is a save-and-restore, which takes the unblock back.
- 2026-10-08 S9.20, a departure from TrueAsync's code (Edmond 22:06 «Это баг», 22:07 «Да»): the cancel or
  dispose of a completed scope reaches its child scopes too, recursively (`src/scope.c`,
  `async_scope_cancel()`): a child scope the script holds is closed, not cancelled (`isCancelled()` stays
  false), refuses `spawn()`, starts its finally handlers and closes the channels bound to it
  (`scope/132`-`136`, `139`, `channel/130`, `136`). Why: the TrueAsync docs say the cancel of a
  scope reaches the whole hierarchy (`concepts/scope.md`, `concepts/cancellation.md`), and TrueAsync's
  silent branch (`scope.c:964-971`) stops at the scope itself, a defect of TrueAsync. A child scope with a
  coroutine of its own left, zombies included, is skipped: a cancelled one stays open while its
  coroutines unwind (`scope/137`, `138`, the Critics of S9.20), and a finally handler's run scope, never
  cancelled itself, is left to finish (`scope/140`, the re-check Critic). The free of an idle parent's
  object is such a cancel (TrueAsync's `scope_destroy()`, `scope.c:1395-1416`, says it cancels "all its
  child Scopes"), so a child scope the script keeps alone is closed with it (`scope/141`).
- 2026-10-08 `scope/110-awaitAfterCancellation_idle_closed_scope_open_child.phpt` and
  `scope/116-child_scope_finally_after_parent_dispose.phpt` (ours, S9.5 and S9.6; `changed:2026-10-08`)
  expect the dispose of an idle parent to close its child scope: `110` no longer spawns into the child
  (its name, which says the child stays open, is kept: a list line is frozen), and `116` sees the
  child's finally handler run at the dispose with its scope instead of at the request's end with null.
  Why: the S9.20 entry above; both pinned TrueAsync's silent branch.
- 2026-10-08 S9.20, a departure from the agreed note (Edmond 21:56, option Б): a closed channel keeps only
  its close reason, no `close_exception`; `await_outcome()` builds a `ChannelException` from the reason
  for each `await_*` reader, and the close notifies its vector with no exception, so the item and token
  records read the outcome themselves (`src/await.c`). `await_outcome()` now hands its caller a
  reference to release, and nothing on false. Why: the held exception's trace held the arguments of the
  frames that called `close()`, so a channel closed by a function that took it waited for the cycle
  collector (`channel/131`); TrueAsync releases its close exception at the end of `close()`
  (`channel.c:572-575`). The readers no longer share one object (`channel/132`, `133`), and an error a
  wait chained under the exception no longer stays on the channel (`channel/134`). An already closed
  channel as an `await_*` item throws, as S9.18 built it; TrueAsync skips such an item, since its channel
  event sets no `replay` (`async_API.c:598-606`). The item and token parts are replaced by the
  2026-10-09 S9.26 entry (`Completable` only).
- 2026-10-08 S9.20: `foreach` over a channel receives nothing after the previous value's destructor
  threw in the iterator's release, so the next value stays in the channel (`channel/135`, the body
  unsets the value). Why: the receive took a value the loop then dropped with the exception, or parked
  with it pending; TrueAsync has the same gap (`channel.c:979-997`). A destructor run by the loop
  variable's reassignment runs after the receive, as with any iterator, so the next value is then in
  the variable, not in the channel.
- 2026-10-09 `channel/117-channel_second_dispose_of_cancelled_scope_wakes_only_channel.phpt` and
  `channel/129-channel_collector_spares_receiver_until_child_scope_released.phpt` (ours, S9.19;
  `changed:2026-10-09`) wait for the member to start before the cancel, and `117` for its waiter and
  receiver to park before the dispose. Why: under the S9.20 fuzz seeds the one `suspend()` returned
  before the member ran, the cancel stopped it unstarted, and the loop that waits for its channel never
  ended (seeds 4, 5, 6, 9 and more, on `ba6412c` too); a receiver starting after the dispose takes the
  closed channel's path instead of the wake.
- 2026-10-09 `bailout/017-scope_disposed_during_bailout_drops_finally.phpt` (ours, S9.6;
  `changed:2026-10-09`) has its zombie wait for the `disposeSafely()` instead of a `delay(10)`. Why: the
  S9.20 debug lane failed it once; under load the timer fired in the zombie's own first suspend tick,
  the zombie resumed there and failed before main disposed the scope, so the scope's finally ran at
  the object's free after the shutdown functions, as a live scope's does (1 of 120 runs with six busy
  CPUs printed no dispose; also on `ba6412c`).
- 2026-10-09 S9.21: a step of a channel's iterator receives into its own slot, then replaces the
  iterator's value, and takes a value out of the iterator before releasing it, so one iterator stepped
  by two coroutines at once frees each value it received once (`channel/139`, `140`). Why: both steps
  received straight into the iterator's value and the second overwrote the first without releasing it,
  and a value whose destructor suspended stayed in the iterator for the other step to release again
  (the security pass and a quality Critic); TrueAsync's step has the same gaps (`channel.c:974-997`).
- 2026-10-09 S9.22: a `recvAsync()` Future dropped while it waits (its last reference released) disarms the
  channel's timer when its side starves no more, and arms nothing; a parked waiter's abort does the same
  through one helper (`channel_timer_disarm_if_idle()`), which now also drops a timer of the side that stopped
  starving. Why: the timer stayed armed for the gone Future and `noProducerTimeout` closed an idle channel
  with `NO_PRODUCERS` (`channel/141`); Edmond, 06:38: «если это баг конечно нужно исправить». A departure from
  TrueAsync, whose Future dispose leaves the timer (`channel.c:297-320`), and from `S9-channel.md` section 5,
  which refreshed only at a park, a wake and a wait's end.
- 2026-10-09: a channel's queues stay arrays with linear remove, O(N^2) for N leaving waiters, as TrueAsync's
  (`channel.c:286-294`). Why: Edmond, 06:40, «нет не стоит, я думаю это очень редкий кейс»; slow only past
  tens of thousands of waiters on one channel (`SECURITY.md`, 2026-10-09 Accepted).
- 2026-10-09 S9.23: a finally handler that has started is stopped by no scope cancel, first, repeated or an
  ancestor's, nor by the error route of its target's scope; only a dispose timer's fire (and exit, graceful
  shutdown, the deadlock resolution, the collector's cancel policy, a `cancel()` of its worker's own object)
  stops it. Why: Edmond 06:50, «согласен именно так и нужно! отдельный флаг для особого Scope, который человек
  поставить не может»; before, whether the handler survived depended on whether its scope was cancelled before
  the handler started (a member that returned by itself, then `cancel()`: the handler's wait threw). A
  departure from TrueAsync (`scope.c:1021-1036`); `dev/plans/S9-scope.md` section 13.
- 2026-10-09 `scope/117-finally_error_with_cancelled_handler.phpt` (ours, S9.6; `changed:2026-10-09`) cancels
  its waiting handler with `disposeAfterTimeout(1)` instead of `cancel()`, and the handler waits 100 000 ms
  instead of 50. Why: since S9.23 `cancel()` no longer reaches a running handler; the deadline keeps the
  test's case, a cancellation among the handlers' errors, now "Scope has been disposed due to timeout" (the
  S9.23 code Critic: a 50 ms wait could end before the timer fired under load), and the main waits on a flag
  the throwing handler sets instead of `delay(20)`.
- 2026-10-09: a stream read or write cancelled while it waits gives no guarantee about the stream's data: the
  stream stays usable (`io_provider/009`, `010`, `023`), its data may have a gap, and the user documentation
  (true-async.github.io, `cancellation.md`) says to close it.
  `io_provider/032-cancelled_pipe_read_then_close.phpt` (`changed:2026-10-09`) checks the cancellation and the
  close, no longer the lost bytes. Why: Edmond, 07:37, «да снимаем»; keeping the bytes in php-src's
  `php_stream_read()` cost two `php_stream` bits, double memory at the throw and changes to `fpassthru()`
  (`6deca53e595`, reverted by `4c153497db3`).
- 2026-10-09 S10.4: when the core or an extension loaded earlier already defines
  `Fiber::getCoroutine()`, the extension leaves it and raises an E_NOTICE at startup, which every CLI
  run then prints. Why: a silent skip would hide a method whose meaning may differ from ours (null
  before `start()`, `fiber/032`), and the test suite shows the notice at once.
- 2026-10-09 S10.4: when a fiber's coroutine is cancelled and then its parent while the parent waits in
  `resume()`, the parent gets its own cancellation and the fiber's is dropped, not chained
  (`fiber/031`). Why: the fiber hands its exception to a waker that already holds the parent's
  cancellation, and a wake never brings a cancellation over a pending error (`scheduler.c:210-215`).
- 2026-10-09 S9.24: once a scope's dispose timer has fired and interrupted its members, no finally handler is
  called under that scope or any scope below it, nor under a finally run the fire's walk stopped: a run still
  starts and releases its handlers, but `finally_handler_call()` calls none. Why: Edmond 07:57, «да именно.. и
  видимо в этом случае флаг говорит никакие другие finallly больше не стартуют!»; a run that started after the
  fire had no deadline left and could hang the script. The check is per call, not a refused start, so the
  handlers are released where they were before (the code Critic: a refused start released them inside a
  finished coroutine). A departure from TrueAsync (`coroutine.c:1281`, `scope.c:1203-1236`);
  `dev/plans/S9-scope.md` section 14.
- 2026-10-09 S9.24: a safe scope's fire sets no flag on the scope. Why: Edmond, 10:04, «да»; its cancel leaves
  started members running as zombies, so a member that finished its work would lose its cleanup (the design
  Critic; probed on the debug build). For the same reason a fire whose cancel interrupts no member (members
  that all returned, only a finally run running below, a cancelled scope whose cascade reaches no coroutine)
  sets none; `scope_deadline_interrupts_member()` follows the cancel's branches (the re-check and quality
  Critics). The flag covers S's whole subtree, so a survivor below S (a member of a cancelled child that goes
  on) loses its handlers once another member was interrupted; a per-scope flag is Edmond's call. A
  request-lifetime scope never takes it: a SpawnStrategy's stand-in of the global scope can make it unsafe and
  arm a timer, and every handler of the request would be dropped (the code Critic).
- 2026-10-09 `scope/151-deadline_runs_finally_of_idle_child_its_cancel_closes.phpt` (ours, S9.23;
  `changed:2026-10-09`) expects the fire that closes an idle child scope not to call the child's
  `Scope::finally()` handler. Why: Edmond's S9.24 rule drops every finally handler after a deadline that
  interrupts a member, including the run the fire's cancel starts in an idle child; the walk's order it guarded
  is guarded by `scope/169` (a safe scope). The file keeps its name, which says "runs": a list line is frozen
  once pushed.
- 2026-10-09 `channel/101-channel_recvAsync_timeout_rounds_leave_nothing.phpt` (ours, S9.18;
  `changed:2026-10-09`) abandons one `recvAsync()` Future after `timeout(1)` and checks that it is freed
  and leaves the queue, instead of 10 100 rounds with `memory_get_usage()`: `freed:` shows the Future is
  released, ASAN catches a leaked or stale waiter. Lost, knowingly: growth that is freed only at shutdown. Why: Edmond, 09:36, «тест говно. такие тесты нельзя делать!»: a test
  must not repeat a timer wait thousands of times. Each round waited one timer tick, 1.1 ms on Linux
  (11 s in all) and by computation the 15.6 ms system tick on Windows (`ior_iocp.c:3028`), so pocs-win
  killed it at 60 s since S9.18.
- 2026-10-09: the global scope stays safe (`ASYNC_SCOPE_F_DISPOSE_SAFELY`, `src/scope.c:2148`), so a
  `Scope::inherit()` made from it is safe too, as TrueAsync's (`scope.c:186-189`, `scope.c:1331-1341`). Why:
  Edmond, 10:03, «да остаётся мягким»; a TaskGroup's own scope does not inherit the mode (his TaskGroup
  decision, 2026-10-09).
- 2026-10-09 `scope/170-deadline_of_idle_scope_runs_its_scope_finally.phpt` (ours, S9.24; `changed:2026-10-09`)
  awaits its member and arms the timer on a scope kept by an idle child scope, then awaits a Future its
  handler completes, instead of `delay(5)`, `disposeAfterTimeout(50)` and `delay(100)`. Why: under the
  parallel pocs-win run the member's 5 ms wait could outlast the 50 ms timer, the fire then interrupted it
  and the handler was rightly dropped (Edmond's PC, relayed by the coordinator); the test now waits on
  nothing but the fire.
- 2026-10-09 S10.5, replacing 2026-10-06 "`Async\signal()` throws on Windows": `Async\signal()` on
  Windows takes the console's control events, Ctrl+C, Ctrl+Break and the close, as SIGINT, SIGBREAK
  and SIGHUP, through a `SetConsoleCtrlHandler()` handler installed at MINIT that fires a trigger of
  the main thread. The handler takes an event only while a watch waits for its number, and holds the
  close until Windows ends the process; logoff and shutdown are left to the default. Why: TrueAsync's
  libuv does the same (`libuv/src/win/signal.c:116-144`), and Edmond approved `dev/plans/S10.md`
  section 8 (W1) on 2026-10-08.
- 2026-10-09 S10.5 (agent, not yet confirmed by Edmond): on Windows every `Signal` case is accepted.
  The Future of a case that no console event delivers completes only when its cancellation does
  (`signal/039`), and never without one. `SIGWINCH` is such a case (Edmond 2026-10-09: not now; PLAN
  Fog). Why: TrueAsync's libuv takes every number below its `NSIG`, 29
  (`libuv/include/uv/win.h:90-95`).
- 2026-10-09 S10.5 (agent, not yet confirmed by Edmond): on Windows `Async\signal()` throws `Error`
  outside the CLI (`signal/038`) and, in a thread-safe build, outside the main thread (no test: a
  thread needs ext/parallel). The CLI is what the core's `php_win32_console_is_cli_sapi()` takes: the
  `cli` and `cli-server` SAPIs, with no check for a console. The check runs after the token's, so a
  completed token still gives a failed Future, as in 2026-10-06. Why: the core's
  `sapi_windows_set_ctrl_handler()` refuses there (`win32/signal.c:114-130`); the console's events
  go to the process, and only the main thread owns the trigger.
- 2026-10-09 S10.5 (agent, not yet confirmed by Edmond): three limits of the console handler stay. A
  handler set later by `sapi_windows_set_ctrl_handler()` runs before ours, since Windows calls the
  last registered handler first. An event taken for a watch that is freed before the main thread
  wakes is dropped. After `sapi_windows_generate_ctrl_event()` or
  `sapi_windows_set_ctrl_handler(null)` (both call `SetConsoleCtrlHandler(NULL, TRUE)`,
  `win32/signal.c:136`, `169`), and in a process started with `CREATE_NEW_PROCESS_GROUP`, Ctrl+C is
  ignored and no SIGINT arrives; we do not re-enable it. Why: TrueAsync's libuv, registered at startup
  (`libuv/src/win/signal.c:42-46`), behaves the same.
- 2026-10-09 S10.5 (agent, not yet confirmed by Edmond), departing from `dev/plans/S10.md` section 9
  (M9): `php_pollfd_for()` and `php_pollfd_for_ms()` stay plain `poll()`, as the plan already kept
  `php_poll2()`; RFC-CHANGES 23 asks their callers in ext/ftp and pgsql to wait through
  `php_io_poll()` instead. Why: the core's own blocking path calls `php_pollfd_for_ms()`
  (`main/io/php_io_hooks.c:876`, `main/io/php_io.c:110`), and a provider wait inside the helper would
  send that path back into the provider.
- 2026-10-09 `signal/024-watch_in_static_outlives_shutdown.phpt`,
  `signal/027-exit_leaves_held_future.phpt` and `signal/028-uncaught_exception_leaves_held_future.phpt`
  (ours, S6.8; `changed:2026-10-09`) run on Windows: their SKIPIF is gone. Why: they call no posix
  function, and they cover the Windows watch's teardown at RSHUTDOWN, at `exit()` and at an uncaught
  exception (the S10.5 Critic).
- 2026-10-09 `signal/001-signal_basic_timeout.phpt` runs on Windows again and is back to the
  reference's text (its `changed:` and `skip-on:*-win` tags gone). Why: the 2026-10-06 reason, a
  `signal()` that throws on Windows, is replaced above.
- 2026-10-09 S9.26: S9 layer 4, `Async\TaskGroup` and `Async\TaskSet`, is TrueAsync's `task_group.c` on our wait
  model, one implementation for both classes, with the 30 departures of `dev/plans/S9-taskgroup.md` section 8.
  Why: Edmond answered the note's 38 questions on 2026-10-09 (quotes and times in
  `/mnt/project-files/s9/S9-taskgroup-answers.md`); the six internal departures, items 21-26, he accepted at
  07:26 («я выбираю то, что лучше»). No core change: the pinned core has no group type.
- 2026-10-09 S9.26, replacing the channel as an `await_*` item and token of the 2026-10-08 S9.18 and S9.20
  entries: `await_*` items and every cancellation token take `Completable` only, so a channel, a group or any
  other non-completable `Awaitable` is refused; `Async\select` is not added. Why: Edmond, 07:17-07:19 («мы
  тупанули», a channel as a token is a design error), 07:36 (`await_all()` over `recvAsync()` Futures covers
  `select`). Built in S9.27.
- 2026-10-09 S9.26: a task does not hold its group, so dropping the last reference cancels the running tasks
  at once, as our `Scope` (`src/scope.c:1206-1211`) and tokio's `JoinSet`; `foreach` and
  `TaskSet::joinNext()` follow completion order, `race()` stays TrueAsync's; `cancel()` always interrupts,
  starts no queued task and also cancels after `close()`; `dispose()` seals the group; the group's own scope
  does not inherit "dispose safely", and a passed `Scope` with `allowZombies()` draws a warning; the group owns
  a passed `Scope`. Why: Edmond 2026-10-09 (answers 1-34). Rejected: TrueAsync's task holding the group, under
  which `unset($g)` cancels nothing.
- 2026-10-09 S9.26: the destructor never waits, by one algorithm wherever it runs (PHP's GC, `unset()`, a
  finished coroutine): it takes the object again for the group's closing, seals the group and cancels its
  unfinished tasks; the finally handlers start once when the group completes (at once when it is settled, else
  at its last task's end), and the closing lets the object go after they end and the unhandled errors are
  reported (note section 5, steps 1-7). Why: Edmond 10:01 («единый алгоритм») and 10:03 («второй раз finally
  не должны вызываться»). Rejected: waiting in the destructor (TrueAsync, probed `d4.php`: a result-held object
  hangs), and a "called from the GC" check (Edmond 09:58).
- 2026-10-09 S9.26: an error nobody handled is printed at the group's closing, `Uncaught
  Async\CompositeException` when there is no handler; an error is handled once `all()`, `race()`, `any()`, a
  `TaskSet` read, `getErrors()`, `suppressErrors()`, `ignoreErrors: true`, a new iterator or a `foreach` step
  saw it, and after a successful
  `race()` or `any()` the other tasks' errors are handled too; no cancel marks an error handled, and a task's
  cancellation is never reported. Why: Edmond 2026-10-09 («если исключение никто не обработал - печатать»,
  answers 36 and 37). Rejected: TrueAsync's report of a caught `all()` rejection (probed `g10h.php`).
- 2026-10-09 S9.26: a cancel leaves alone a coroutine whose body has finished, returned or thrown, for every
  scope and group: it keeps its outcome and `isCancelled()` stays false. Built in S9.28. A cancel that came before the return is unchanged. Why:
  Edmond 10:39 («если корутина уже была выполнена - её нельзя по сути отменить», a bug in TrueAsync): a
  closure that holds the last reference to its `Scope` or `TaskGroup` is released after the body returns, and
  the destructor's cancel replaced the result (probed on TrueAsync, `r28_plain.php`).
- 2026-10-09 S9.26: a group's finally run that no coroutine can take (async no longer active at the request's
  end, or a scheduler that cannot make its own coroutine) releases its handlers unrun, as a coroutine's
  (`src/coroutine.c:513-518`) and a scope's, so every handler that runs is in the run's coroutine and its error
  takes the scope's route, never thrown out of `unset()`. Why: the self-decision listed to Edmond at 08:33;
  async is active from before the script's first line, so the refusal comes only when the request ends or
  fails (the Critic of the S9.26 commit found the earlier "call them in the caller" contradicting it).
- 2026-10-09 S9.25 (agent, not yet confirmed by Edmond), departing from TrueAsync: in its body, the last
  worker of a finally run that ran lets go of the handlers, the run's target and the rest of what the walk
  holds, before the run ends (`iterator_dispose()`); TrueAsync releases them at the microtask's last
  release, in scheduler context; after an `exit()` the tick still releases them. Why: a destructor there may
  suspend, as destructors do elsewhere, and after an exit its error must not replace the exit; our probe
  ended the request ("The operation cannot be executed in the scheduler context"), TrueAsync's with "The
  scheduler cannot be started when is already enabled".
- 2026-10-09 S9.27: `channel/100-channel_as_await_item.phpt` and `channel/111-channel_as_cancellation_token.phpt`
  (ours, S9.18; `changed:2026-10-09`) assert the refusal of a channel item and token;
  `channel/103-channel_close_gives_each_waiter_its_own_exception.phpt` drops its `await_*` outcome;
  `channel/132-channel_closed_item_exception_per_await.phpt`,
  `channel/133-channel_close_wakes_await_waiters_with_own_exceptions.phpt` and
  `channel/134-channel_closed_item_previous_not_kept_across_waits.phpt` (ours, S9.20) await `recvAsync()` Futures;
  `await/128-await_all_item_not_awaitable.phpt` (ours, S5) expects the message naming `Completable`. Why: the
  2026-10-09 S9.26 entry, `await_*` items and tokens `Completable` only. `channel/100`'s `await()` case moved to
  `channel/142`, one case per test.
- 2026-10-09 S9.27, own choices: the item check stays a class compare (Coroutine, Future), the set of
  `Completable` classes once `Timeout` is refused; the channel's type flag `ASYNC_CHANNEL_F_CHANNEL` and
  `ASYNC_AWAITABLE_IS_CHANNEL()` go with their last readers; `await_outcome()` keeps the owned exception of S9.20, since
  `async_await_token_completed()` hands out an owned one (a Timeout's is new per call);
  `async_channel_close_exception()` folds back into `channel_throw_closed()`, its only caller; debug asserts in
  `async_await_awaitable_of()` (a Coroutine, Future or Timeout) and in the channel's `free_obj` (an empty event
  vector) stand where the channel's branches were; the step's own tests open S9.txt's layer 4 block, which S9.28
  extends. Why: no code is left for a channel as an awaitable, and a non-Completable object that slipped past a
  parameter would be read as a Future's event. The refusal of a group comes in S9.28 (note section 9).
- 2026-10-09 `channel/134-channel_closed_item_previous_not_kept_across_waits.phpt` (ours, S9.20, changed in
  S9.27) is removed. Why: Edmond, 12:48 («если тест не нужен удали»); on `recvAsync()` Futures it checks only that
  each Future of a closed channel has an exception of its own, which `channel/132` checks, and the chaining of an
  earlier item's error, which `await/115` checks.
- 2026-10-09 S9.28: `task_set/011-task_set_join_any_first_success.phpt` (reference, `changed:2026-10-09`) ends with
  `Fatal error: Uncaught Async\CompositeException`: the error of its first task is taken by no read, and a
  successful `TaskSet::joinAny()` covers no other entry, since a `TaskSet` read delivers only the entry it takes
  and the others stay in the set for later reads (note section 3). Why: the 2026-10-09 S9.26 entry on unhandled
  errors; note section 9 names `task_set/011` among the tests whose report changes (item 15).
- 2026-10-09 S9.28 (a bug since S9.18; Edmond 13:49: TrueAsync's behaviour is right): a `map()`, `catch()` or
  `finally()` child holds its pending source Future while the source has no `FutureState` and is not a child itself
  (a `recvAsync()`, a signal wait's or a group read's Future), until the drain takes the child or the child is
  freed, as TrueAsync's strong reference (`future.c:1752-1756`).
  `channel/108-channel_dropped_future_with_child_served.phpt` (ours, S9.18; `changed:2026-10-09`; renamed from
  `108-channel_dying_future_not_served` for its new claim) now asserts that a dropped `recvAsync()` Future with a
  pending child stays queued and the child gets the value; `channel/145` takes over `108`'s destructor case, the
  cycle collected by PHP's GC. Why: S5 keeps the chain in the event so that a temporary source completes its chain
  (`future/038`), but these Futures' events died with their object, so `$group->all()->map(f)->await()` and
  `$channel->recvAsync()->map(f)->await()` deadlocked (probes `s928_*_map_temporary.php`). A chain dropped whole
  stays queued until PHP's GC collects it, as TrueAsync's.
- 2026-10-09 S9.28, own choices: the queued and the settled tasks are doubly linked lists of entries, not the
  note's key tables, since a `TaskSet` read takes an entry from the middle. A drain behind a running task finds the
  scope cancelled, never closed: `async_scope_cancel()` closes only a completed scope (`src/scope.c:163`, `673`)
  and the deadline only finally runs (`340`); the closed scope is tested through `spawn()` (`task_group/067`-
  `070`). A group dropped by an output handler after the coroutines calls no finally handler (`085`); one held at
  the request's end is destroyed by `zend_call_destructors()` while the scheduler runs and closes quietly (`081`).
  A closing that ends later makes its composite outside PHP code: `Uncaught Async\CompositeException in [no active
  file]:0` (PLAN, Open questions; one that ends at the drop names the dropping line, `103`, `104`). Queued tasks of
  a scope cancelled from outside end with a new `AsyncCancellation("TaskGroup cancelled")`. The closing cancels
  through `async_scope_cancel_remaining()`, which also reaches a coroutine spawned after the cancel and a safe
  scope's zombie (`093`, `095`). What the entries hold is released once they have their new state and the read is
  answered, so a destructor there finds the group consistent (`094`, `task_set/033`).
- 2026-10-09 S9.28 (Edmond 13:56, «если баг - фикси»; a departure from TrueAsync, `task_group.c:1499-1502`): a
  numeric string key of `spawnWithKey()` and `trySpawnWithKey()` is the integer key, as in a PHP array, so `"1"`
  and `1` are one key (`Duplicate key 1`) and the reads build arrays PHP can index (`task_group/096`). TrueAsync
  kept `"1"` as a string key, and `$results["1"]` found nothing.
- 2026-10-09 S9.28, the quality Critics: the bailout tests (`task_group/086`, `089`, `090`) keep
  `skip-on:*-asan(USE_ZEND_ALLOC=0)`, since with Zend MM off any fatal error leaks the core's request memory
  (LeakSanitizer reports it for a plain-PHP `Cannot redeclare function`, no async code involved); they run on dbg.
  The compile-error variant of `086` is dropped: it takes the same bailout path through `php_error_cb`. Its number
  goes to the closing of a group closed and then dropped while its finally run waits, which reports after the run
  (`task_group/087`). One case per test: `task_set/031` keeps completion order and `task_set/034` takes the two
  pending `joinNext()` calls; `task_group/053` keeps the spawned but unrun task (`092` has the queued one); `069`
  keeps the results (`070` has the closed scope's `spawn()`); `076` waits for the task's start, so the holder's end
  cannot cancel it unstarted under the S9.30 seeds; `095` checks the zombie with `isCompleted()`.
- 2026-10-09 S9.28, the re-check Critic: a `trySpawn()` whose next integer key a `spawnWithKey()` took throws
  `Duplicate key` and takes that integer, as `spawn()` takes one at its call, so the next call moves on instead of
  throwing on every call (`task_group/097`). The availability check of the spawn methods stays before the parameter
  parsing, as `Async\spawn()`'s (`src/true_async.c`): after it a refusal would leave the `__call` trampoline
  unreleased.
- 2026-10-09 S10.6 (Edmond, 10:53, option A): a fiber whose coroutine ends before its body runs (a
  cancel through `Fiber::getCoroutine()`, an `exit()` elsewhere between `start()` and the body) ends
  without running the body; `start()` throws the coroutine's error, `isTerminated()` is true and a
  second `start()` throws `FiberError`, as in TrueAsync. The core does it: `zend_fiber_coroutine_start()`
  adds a finish handler that ends the fiber and wakes the coroutine waiting in `start()` with the
  error, and the fiber's entry point removes it once the body runs (`async-core` `060bc7e104e`;
  `fiber/033`-`035`). Rejected (B): the scheduler calls the fiber's entry point with the error thrown,
  as TrueAsync (`coroutine.c:497-523`); it changes the core's `ZEND_COROUTINE_F_STARTED` contract, our
  `run_queue_pop()` and `test_scheduler`. Why: one place, in the core; the handler wakes the fiber's
  `caller_coroutine`, which `start()` waits in.
- 2026-10-09 S10.6 (Edmond, 11:30: «я бы не создавал главную корутину без нужды»): the from_main call
  after the destructors makes no new main coroutine and returns async to READY, as TrueAsync's
  `async_scheduler_main_coroutine_suspend()` (`ZEND_ASYNC_INITIALIZE`, `scheduler.c:1385`); in
  `src/scheduler.c` and in `test_scheduler` (`async-core` `5610980dc8f`), which also makes no loop when
  nothing is left to drain. It narrows 2026-10-05 ("Main is re-minted at every from_main call"): the
  calls after the script, after a shutdown function's bailout and the last call after a destructor's
  bailout still re-mint. Why: after the
  destructors the object store reuses no handle, and a fatal out-of-memory error could leave it full
  with no memory to grow, so the error was printed twice (`scheduler/108`, `test_scheduler/095`).
- 2026-10-09 S10.6 (Edmond, 12:59-13:09: «если код законный ... async там должен работать», «иметь
  планировщик под боком и планировщик не выключать до этой точки»): the request's last print of
  uncaught exceptions (the unobserved ones, then the exit exception) runs in a coroutine of the last
  call's pass (`scheduler_print_start()`); what its `__toString()` spawns runs in the same pass. A
  bailout out of `__toString()` is re-raised once the rest is printed (`scheduler/109`, `110`); D16's
  deadline is withdrawn when the print starts (`reactor/044`); after a bailout of the request the
  built-in `__toString()` prints outside a coroutine (`scheduler/112`). TrueAsync differs: there a
  `spawn()` in that print gets no pass. Consequences and probes: section 13 of `dev/plans/S10.md`.
  `context/044-teardown_releases_coroutine_values_last.phpt` (ours, S9.13; `changed:2026-10-09`)
  checked that coroutine's argument released at the teardown; it now checks that the coroutine runs
  and its argument is released when it ends, before the teardown (Edmond: «код меняй»).
- 2026-10-09 S8.1: no guard in `ZEND_ASYNC_SUSPEND()` after the deactivation and no rule for
  unwinding on a bailout (the review's scheduler items 1 and 2); `Fiber::resume()` and `throw()` of an
  adopted fiber refuse once async is off instead (`RFC-CHANGES.md` 24, step S8.1a). Why: our scheduler
  unwinds or detaches every parked wait and refuses a suspend without a current coroutine; only the
  fiber methods reach it after the deactivation, and they assert first (review section 9).
- 2026-10-09 `task_group/035-task_group_gc_traversal_all_states.phpt` (reference, `changed:2026-10-09`) ends with
  `Uncaught Async\CompositeException in [no active file]:0`. Why: S9.29 lets it run past its park on a full queue,
  it never reads its failing task's error, and an error no read took ends the request (note sections 8, item 15,
  and 9; S9.28 Q36).
- 2026-10-09 S9.29, own choices: the waiters' queue is the channel's, moved to `src/true_async_API.c` as
  `async_wait_queue_t`, and `async_wait_link_outside()` takes a kind with an `unlink` (the wake takes the record
  out, D26) or with an `abort` alone (its frame does, as the channel's). The TASK_GROUP kind has no `abort`, unlike
  the note's section 4: `async_wait_abort()` ends with `async_wait_unlink()`, which calls the kind's `unlink`
  (`task_group/112`-`114`). `async_collector_iterator_is_c_local()` moved from the channel to `src/collector.c` for
  both iterators. The finally run is no reach source of the group, unlike the note's section 6: the completion
  wakes every waiter before the run starts, and a closed group parks no spawner. `getAwaitingInfo()` reads
  `TaskGroup(total=2, active=1, queued=1): spawn() on a full queue` (`foreach`, `awaitCompletion()`; `TaskSet` for
  a set). `awaitCompletion()` refuses the scheduler's own work only when it would wait; a `foreach` refuses it at
  each step, as the channel's iterator.
- 2026-10-09 S9.29, own choices: a woken spawner checks the group again (closed, the key taken meanwhile, the scope
  stopped) and, leaving the park without a place (an exception, its cancellation), wakes the next one while a slot
  is free or the queue has room (section 8, item 19; `task_group/108`, `109`). A `spawn()` reached through `__call`
  keeps its trampoline across the park: `zend_get_call_trampoline_func()` does not reuse a busy one
  (`Zend/zend_object_handlers.c:1807`, `task_group/120`).
- 2026-10-09 S9.29, the Critic: a scope cancelled or closed from outside seals and cancels the group at the first
  task end that finds it so, with nothing queued as well, reading the note's "first drain" as every task end;
  before, a `foreach` over a group whose external scope was cancelled with nothing queued waited for good
  (`task_group/124`). A newcomer's `spawn()` or `trySpawn()` takes no room while a spawner is parked or woken to
  take the room and not yet run (`passed_spawners`): it parks behind, or gets `false`, so a `trySpawn()` never
  passes a waiting one (note section 2; `125`, `126`); a spawner leaving the park wakes the next ones only for the
  rooms no woken spawner has been promised (`task_group_free_room()` over `passed_spawners`), so each woken one
  finds its room and the spawners go in their order (`129`). A `foreach` takes responsibility for the errors
  present only once its first step is allowed, so a refused one leaves them reported (`128`).
- 2026-10-09 S9.29 (a bug TrueAsync also has, `task_group.c:1807-1858`): `awaitCompletion()` called in one of the
  group's own tasks throws `AsyncException("Cannot await completion of TaskGroup from one of its tasks")`, as
  `Scope::awaitCompletion()` refuses a coroutine of its scope; TrueAsync parks it until a cancel, since the
  completion waits for that task (`task_group/127`). A `foreach` there is not refused: it may stop early.
- 2026-10-09 S9.29, the re-check Critic: the group's cancel leaves a scope already cancelled or closed from outside
  as it is, as it found the tasks cancelled; a second `async_scope_cancel()` would close the scope
  (`src/scope.c:672`), so a task's catch could no longer spawn into it and the scope's finally handlers would start
  while the tasks unwind (`task_group/137`). Before S9.29 only a drain with tasks queued met this case.
- 2026-10-09 S9.29, kept as they are: a plain `spawn()` takes its integer key at its call, before a park, as
  TrueAsync's; a `spawnWithKey()` of that integer while it waits parks behind it and throws `Duplicate key` when it
  wakes (`task_group/132`). Waking every waiter of a group removes the records from the front of an array, O(N^2)
  for N waiters, as the channel's queues (2026-10-09 entry, `SECURITY.md` Accepted). The bailouts while parked
  (`task_group/112`-`114`) keep `skip-on:*-asan(USE_ZEND_ALLOC=0)` for the S9.28 reason (`086`), so they run on
  debug only, short of section 9's "on debug and ASAN". `g4.php`'s `race()` before the settle is `task_group/012`'s
  case.
- 2026-10-09 S9.30: six bailout tests (ours, `changed:2026-10-09`) lose their skip of Zend MM off and their
  `skip-on:*-asan(USE_ZEND_ALLOC=0)`, so they run on debug and ASAN as note section 9 asks:
  `task_group/086-fatal_error_finally_run_pending.phpt`, `task_group/089-fatal_error_with_queued_task.phpt`,
  `task_group/090-fatal_error_in_task_with_finally_pending.phpt`,
  `task_group/112-fatal_error_while_parked_in_foreach.phpt`,
  `task_group/113-fatal_error_while_parked_in_awaitCompletion.phpt`,
  `task_group/114-fatal_error_while_parked_in_spawn.phpt`. The S9.28 reason came from a direct run: run-tests
  `--asan` sets `USE_TRACKED_ALLOC=1` (the pinned core's `run-tests.php:576`), which keeps `memory_limit` and
  frees the request's memory after a bailout; `tools/test.py --lane pocs-asan` passes a plain `Cannot redeclare
  function` and these six. Replaces the S9.28 and S9.29 skip lines; the other lists' bailout tests keep theirs
  until a step of their own (PLAN, S9.30 handoff).
- 2026-10-09 S9.30, found by coverage and the Critics (bugs, fixed): the report of a group's unread errors made
  under the graceful shutdown released the composite it had handed to the exit's chain, a use after free at the
  print (`task_group/138`). A reporter queued before an `exit()` or an uncaught error was cancelled unrun and its
  composite dropped, so the errors were never printed; a finish handler now hands it to the exit's chain unless
  a bailout runs (`139`, `147`). The handler runs at the finish, not in the dispose: a holder of the coroutine
  (`Async\get_coroutines()`) delays the free past the request, and the exit's chain would keep a freed object
  (`146`). The tests match only the composite's header: after `exit()` no handler sees it. An `exit()` in a task
  stored an UNDEF result, which `getResults()` turned into an empty bucket; it is now null, as
  `Coroutine::getResult()` gives and TrueAsync stores (`task_group.c:931-935`; `145`).
- 2026-10-09 S9.30 (a departure from note section 9): B17 is compared with the reference at 10 000 tasks, not
  100 000: the reference's queue scans every entry per start, 905 568 instructions per task at 10 000 and
  growing with the count, so a run at 100 000 under cachegrind was stopped. Ours runs at both sizes
  (`dev/BENCHMARKS.md` 2026-10-09).
- 2026-10-12 S14.4: `gc/020-gc_threshold_adjusts_when_buffer_fills_behind_explicit_collect.phpt`,
  `gc/023-gc_threshold_untouched_by_a_run_that_found_nothing.phpt` and
  `gc/018-gc_threshold_stable_without_coroutine.phpt` are `changed:` ports, titles and comments included. A
  collection in a coroutine, main included, waits for its run (2026-10-02 entry): 020's explicit run empties
  the buffer, so no step is owed (10001); 023's run finds the objects alive and takes PHP's step (20001); 018's
  main waits for 8 runs that free the cycles, and a coroutine it spawned runs first. The fork defers each run;
  the file names are the reference's and describe the fork. Replaces the 2026-10-06 entry's "S8 change-request
  candidate". The core's drained-buffer guard (`zend_gc_coroutine()`, `gc_runs != runs`) keeps no test here;
  whether a buffer can drain before a run started without waiting is unchecked. Why: a permanent XFAIL also
  passes a crash or a leak (the Critic, the Sage).
- 2026-10-12 S14.4: `include/008` passes and is listed in S6.txt with `gc/018`. Why: both were excluded as
  `component:S6`, and S6 closed without them.
- 2026-10-12 S14.4: TrueAsync's runtime information (`available_parallelism()`, `loadavg()`, `CpuSnapshot`,
  `cpu_usage()`, `runtime_stats()`; 6 `common` tests) is an S9 layer, its plan agreed with Edmond as every
  layer's. Why: its exclusion named S9 with no step, and a drop removes TrueAsync API, which needs his word.
- 2026-10-12 S14.4: S2's scenarios, allocator-fault and network-chaos layers are stage S15 (recorded, not
  started), not dropped; its TSAN row waits for threads (Fog). `src/chaos.c`, `fuzz/` and `tests/deps/`,
  never created, leave the PLAN layout. Why: dropping a layer S2 agreed needs Edmond's word.
- 2026-10-12 S9.31: C code that walks a group's tables releases nothing inside the walk, and a read takes a
  set's entries before anything it does can wait. A release that does not free a value can start a GC run that
  the caller waits for while other coroutines run and spawn into the group (`SECURITY.md` 2026-10-12;
  `task_group/159`, `task_set/036`). So `all()` and `any()` collect the errors first and build the composite
  after, and `joinAll()` and a failed `joinAny()` take the entries before that build. Why: memory safety.
- 2026-10-12 S9.31: a task queued before one of the graceful shutdown's cancels (`exit()`, an uncaught error,
  `Async\graceful_shutdown()`, each later `exit()`) ends unstarted with a new `AsyncCancellation("Graceful
  shutdown")` at the group's next drain, as that cancel ends a queued coroutine unrun (TrueAsync's
  `cancel_queued_coroutines()`); the group goes on, and a task queued after the last cancel starts as a `spawn()`
  then does (`task_group/161`-`164`). TrueAsync's drain checks no shutdown (`task_group.c:860-889`, read, not
  run); before the fix ours held the request 7.4 s with 30 queued tasks that wait, now 0.03 s (debug). Rejected:
  cancelling the whole group, which breaks groups made in shutdown functions; giving the tasks the `$e` of
  `graceful_shutdown($e)`, which needs it held in a global past tasks that never run. Why: `exit()` ends queued
  work. Cost: a count per entry, 8 bytes with alignment; a queued task still takes 293 bytes (debug, measured).
- 2026-10-12 S14.2: `scheduler/085-deadlock_report_on_stderr.phpt` (ours, `changed:2026-10-12`) writes its own
  lines to stderr, and `tools/test.py` keeps run-tests' stderr in `run.err` apart from `run.out`. Why: a test that
  captures one stream (`--CAPTURE_STDIO--`) writes the other into run-tests' own output, where 085's stdout and
  `084`'s stderr met the result lines; a line glued to one hid it (`parsed 1690 tests, run-tests counted 1691`) and
  stopped the nightly seeds. A count that still disagrees is printed, the run goes on, and the lost test is MISSING;
  a crash report in `run.err` fails the run.
- 2026-10-12 S14.2: `stream/030-udp_timeout_operations.phpt` (`changed:2026-10-12`) cancels its server after 2 s;
  the commit that lifts its XFAIL removes the watchdog. Why: the receive ignores `stream_set_timeout()` until S8
  (`RFC-CHANGES.md` 4) and waited for run-tests' 60 s timeout, retried as "timed out": 120 of the suite's 150 s.
- 2026-10-12 S14.2: a crash (a signal, a sanitizer or leak report, a debug assertion, a hang) fails a test with
  `--XFAIL--` too, on every lane and under a seed; under a seed such a test that passes (WARN) is no failure,
  as `io/100` when a seed cancels its reader before the `fread()`. Why: an unfinished test may only fail by its
  output, and the plain lanes judge when it passes.
- 2026-10-12 S14.2: run-tests retries no crash or hang, and no test under a seed (`tools/run-tests.patch`). Why:
  the retry discards the first attempt, so an XFAIL test that hung once, or a seed's changed output
  (`channel/112`, seed 3, then WARN), was judged by the second attempt. A pass on a retry stays WARN, red.
- 2026-10-12 S14.5: the reactor's queue gets fault seams of its own, `TrueAsync\Test\fail_io($site)`: the next pass
  through the queue's creation, a submit, a wait, the wake pair's creation, a completion (ended with `EIO`) or an
  answer (left in flight, as the Ring's early Timeout) fails once as the OS or the queue would, and the request goes
  on; `wait_always` fails every wait, as a broken queue would. `fail_at()` stays the scheduler's fatal-error seam.
  Why: these paths answer a returned error, not a fatal one, and TrueAsync has no fault injection for its reactor
  (only `ASYNC_FUZZ_CANCELLED_WRITE`, `libuv_reactor.c`); a function of its own leaves `internal/033`'s message as
  it is. The hooks `read_without_owner()` and `trigger_refire()` drive the two incidents of `SECURITY.md`
  (`io_provider/043`, `reactor/051`); on the Ring each hangs or prints "refires ran out" with its fix reverted
  (the Poll queue has no drain, so `io_provider/043` passes there either way; the pocs lanes run the Ring).
- 2026-10-12 S14.5, bugs the fault tests found (fixed): an `exit()` in main's script arms D16 with no frame, where a
  throw is a fatal error that skips the drain and every `finally`. D16's submit, the fork rebuild's resubmit of the
  reactor's own ops and the rebuild's re-arm of a signal wait no longer throw: the Error goes to the exit's chain or
  to the watch's Futures (`reactor/052`, `054`, `signal/043`); the rebuild's own PHP calls still can (PLAN open
  questions). A Future completed while no coroutine stack could be had left its mapped children pending for ever,
  as TrueAsync does; the refused drain rejects them and theirs with the refusal, past a destructor that throws on
  the way (`future/125`, `126`), and a `map()` that throws it does not report its unreturned child again
  (`future/127`). A refused reporter of a TaskGroup's unread errors dropped them; they go to the unobserved
  exceptions printed at the request's end (`task_group/162`).
- 2026-10-12 S14.5: a wait of the IO queue that fails ends the request once; a wait that fails again after that is
  answered as a deadlock (`reactor/053`), as a forked child with no queue already is. Why: it added an Error and
  spun the idle loop on every pass until D16 fired, 5 s later. A coroutine that catches the deadlock's
  cancellation and waits again loops as in any deadlock, and a timer due later is cancelled, not waited for. A
  sleep until the next timer was weighed and not built: a queue that cannot wait is a broken process, where
  TrueAsync's libuv aborts.
- 2026-10-12 S14.5, not reached by a test: a forked child's signal `Io\Poll\Context` or handle that cannot be made
  (`os_signal.c`, `async_signal_rebuild`, `signal_watch_renew`: the core creates both; `signal/042` fails the re-arm
  after them) and "Cannot poll the wake descriptors" (`reactor.c`, `wakeup_arm`: needs the wakeup's POLL to
  complete inside its own submit; a raised pair left by a failed wakeup, `reactor/049`'s steps, did not on the Ring
  or the Poll queue in a probe).
- 2026-10-12 S14.5: `io_provider/042-submit_refused_answers_read_with_error.phpt` skips on Windows, as its
  `skip-on:*-win(Unix-only)` tag said: Windows has no `STREAM_PF_UNIX` socket pair, and the test lacked the
  SKIPIF (red `windows` job of 2f277fa).
- 2026-10-09 S8.1a: an adopted fiber's `resume()` and `throw()` throw "Cannot resume a fiber after the
  scheduler has stopped" once async is not active, and the switch-refused FiberError with no live current
  coroutine, before the enqueue; `fiber/036-resume_after_deactivation.phpt` and
  `fiber/037-throw_after_deactivation.phpt` change to that message. Why: the Critic showed the switch
  message false there (`fiber/038` switches a plain fiber in the same handler). Found on the way: a fiber
  destroyed while suspended and kept by a destructor crashed in `resume()`; the core marks it DEAD, as a
  plain fiber (`fiber/039`). `check-lists.py` lets a frozen line lose its `core:` tag once the pinned core
  carries the entry, as `xfail-on:`. Not done: `start()` keeps no such check (a destructor of a
  coroutine's result, the Critic's trigger, is refused as a switch already).
- 2026-10-09 S8.1b, own choices (`RFC-CHANGES.md` 25): a second resumer of a fiber run as a coroutine is
  refused with "Cannot resume a fiber that is not suspended", as on the legacy path, not queued behind
  the first. A destroyed fiber whose body runs is kept alive until the body ends rather than having
  `Fiber::getCurrent()` and `Fiber::suspend()` read the fiber from the current coroutine, which
  would change what `EG(active_fiber)` means to generators and reflection. Accepted: when an earlier
  finish handler throws, or the U6 teardown skips the handlers, the kept fiber lives to the request's
  end. `fiber/033`-`035` keep their outcomes. The transfer slot's value is detached before its release
  although no PHP trigger re-enters there: the body's frame holds a yielded value while it parks.
- 2026-10-09 S8.1c: `Fiber::getReturn()` of a fiber destroyed while suspended throws FiberError "Cannot
  get fiber return value: The fiber was destroyed", keyed on the undefined result as
  `Generator::getReturn()`, so a destroyed fiber whose `finally` returns a value still returns it. TrueAsync's
  coroutine branch returns null there instead; an error tells the programmer the fiber never returned.
- 2026-10-12 S8.1d: the php/php-src fix lets a destroyed fiber's `exit()` end the script as a destructor's
  `exit()` does, no further: the gaps a destructor has too (an exit held by a `finally` while live temporaries
  are freed, an exit replaced by a later throwing destructor) stay, recorded in PLAN "Fog". `ZendTestFiber`
  keeps clearing the unwind exit: mirroring the fix leaked the exit into a `pipeTo()` target (second Critic).
- 2026-10-12 S8.2, own defaults in `RFC-CHANGES.md` 12: `$mysqli->close()` beside a parked query stays refused,
  since the resumed query reads the `MY_MYSQL` a close frees (`mysqli/014`, `015`); `stream_socket_shutdown()`
  takes neither side and a read parked on the shut-down direction ends with `''` (`io_provider/054`, `055`); a
  close from a resource destructor (`proc_close()`) is a close as cancel, so its parked read returns false as
  after `fclose()` (`056`, `057`), with no notice; only the free waits for the last unpin, so `pclose()` keeps the
  child's status; accepts and `stream_select()` take no side (`058`, `059`). Each B1 test has a Poll-queue twin:
  the request's wait op differs per queue.
- 2026-10-12 S8.3, own defaults: a cancelled pipe read is measured on a FIFO a coroutine writes (a `cat`
  child echoed after the canceller ran). The file-read test (`io/100` with `F_FILES` forced on) was dropped with its hook: a page-cache
  read completes at its submit, so the test showed the reader's own discarded read (probe and hook patch in
  `/mnt/project-files/s8/probes/s8.3/`). `063` and `069` record today's Ring behaviour, not TrueAsync's under an
  XFAIL: under seeds the order changes and an XFAIL that passes fails the run; S8.3c rewrites them. Ring tests
  whose outcome rests on two completions' order retry as `009` does, and skip off io_uring. `socket_accept()`'s
  double free (bukka's code) is on its own branch and `io-hooks-fixes`, not in the core yet: the core slot goes to
  S14.1 first (`core:26`, S8.3b). RFC 27 drops a warning TrueAsync gives, as entry 2 did for reads, since in the
  stream exception mode it replaces the cancellation (`071`).
- 2026-10-12 S12 removed from the plan (Edmond 14:30): a coroutine that parks with an empty run queue keeps
  switching into the scheduler coroutine, which blocks on the reactor. The two switches and the tick were about
  600 of ~8 400 extra instructions per waiting op (2026-10-10), they save only for a script with one coroutine,
  and blocking in the coroutine's own context would break S4.md 3.3's single blocking site. Edmond never asked
  for the stage; it came from the session that measured the RINIT install. TrueAsync blocks in place only when
  its 100 ms throttle lets the tick poll (`scheduler.c:1557-1569`).
- 2026-10-12 S14.2: `collector/065-scope_held_by_main_keeps_its_members.phpt`,
  `collector/066-scope_held_by_stuck_coroutine_found.phpt`,
  `collector/067-parent_scope_held_keeps_child_scope_members.phpt` and
  `collector/068-child_scope_held_leaves_parent_members_found.phpt` (ours, `changed:2026-10-12`): main yields with
  `suspend()`, not `delay(10)`, until their members park, as the S4 notes of the handoff ask. Why: a timer already
  due when main parks wakes main in its own tick (`scheduler.c`, U2) before any queued member ran. Under 16
  parallel ASAN runs on 4 cores, 300 runs each, 065 failed 9 (members cancelled unstarted skip their catch), 066 4,
  067 6, 068 5 (red `linux (asan)` of fd1f9c4: 067); with yields none failed. 065 yields twice: its members park on
  their second turn.
- 2026-10-12 S8.4, own defaults: the connect retry tests (`io_provider/076`, `077`) first prove the address order,
  then the refusal; they skip where localhost has one address (here) and ran with a second `/etc/hosts` line,
  never with IPv6. `073`-`077` order their steps with a flag and `suspend()`, no timers; `073` checks through
  `/proc/self/fd` that the read end took curl's number. RFC 11 follows TrueAsync's `LOCK_NB` and timer, only while
  hooks are active, instead of a blocking work op. RFC 28 asks for the curl-side fix (any provider keyed by
  descriptor meets the late `remove()`), not Poll queue counts. The fix is bukka's code: Edmond hears first,
  it waits for the core slot (S8.4a).
- 2026-10-12 S14.1 (Edmond 14:31: «нужно показывать GC сильную ссылку»): once `map()`, `catch()` or `finally()` hangs
  a chain on a pending `recvAsync()`, group read or signal Future, its channel, group or signal wait holds the Future
  (`future_provider_hold()`) until the waiter goes; the channel and the group report it from `get_gc`, the signal
  registry is a root. A borrowed pointer let the GC free a dropped chain the provider could still complete (SIGSEGV
  of `tests/channel/145-channel_collected_future_served_by_destructor.phpt` under seeds, `channel/148` in FIFO). A
  dropped chain stays queued until a value, the close or the request's end, as TrueAsync's self-pin
  (`future.c:1746-1756`); a cycle through the provider is collected with it (`149`). `145` (`changed:2026-10-12`)
  now asserts the chain survives a collection. Rejected: an unreported chain (cycles through the provider lived to
  the request's end, a signal chain leaked at exit: `149`, `signal/045`) and a no-op Future `dtor_obj` (guarded the
  first collection only). A dropped or cancelled only child leaves its source queued to take the next value, as in
  TrueAsync. The waiter's release can start a GC run inside `channel_put()`, as the value's already can: S9.33.
- 2026-10-12 S8.5, own defaults: not requested, with the reason in the review's section 9: a "may suspend" predicate
  in the scheduler API (the provider is the scheduler), a curl multi shared per thread (a lost reuse, not a wrong
  result; Fog). `mysqli_poll()` gets entry 29 though TrueAsync has the same `select()`: legal code blocks every
  coroutine. PLAN request A becomes entry 31; a pipe write's readiness wait becomes 32, since two loggers on one pipe
  or on `STDERR` collide (`081`); 32 asks a borrowed description a zero-timeout `poll()`, so 31 does not undo it.
  Entry 12 keeps TLS streams frozen until 30 (one BIO wait state per stream); `078` carries `core:30`. The tick-function
  case of the fiber-switch block joins the open question "A pcntl handler that waits". The XFAILs of S14.4's
  list get steps: `io/039`, `040`, `042`, `043` and `stream/030` name S8.6 (the core fixes of RFC 3 and 4, a new
  step), `io/100` names S8.3a.
- 2026-10-12 S14.3: own tests that slept a guessed time for an event now wait on the event (HEALTH 2026-10-12):
  `scope/164-safe_scope_deadline_keeps_zombie_coroutine_finally.phpt` awaits the scope's completion, which the
  fire reaches by making the member a zombie; `scope/096-awaitAfterCancellation_handler_runs_in_waiter.phpt` and
  `scope/104-awaitAfterCancellation_error_while_handler_runs_goes_up.phpt` let the handlers release the members in
  turn; `await/121-timeout_two_waiters.phpt` returns at once instead of after 10 ms;
  `internal/066-iterator_walk_order.phpt` opens each worker's step only after the steps the expected order puts
  before it. Why: under `--jobs 16` a delay can end before the event.
- 2026-10-12 S14.3, the same for channels: `channel/112-channel_recvAsync_awaiter_live_while_channel_held.phpt`,
  `channel/125-channel_collector_finds_lone_receivers.phpt` and
  `channel/126-channel_collector_spares_live_receivers.phpt` cancel their sleepers after the collector's reports
  instead of after a delay; 112 and 125 cancel from a coroutine of their own, since a cancel made in the reporter
  keeps that frame's Future alive through its trace;
  `channel/116-channel_in_global_scope_open_after_unhandled_error.phpt`,
  `channel/117-channel_second_dispose_of_cancelled_scope_wakes_only_channel.phpt` and
  `channel/129-channel_collector_spares_receiver_until_child_scope_released.phpt` await the coroutine or a Future
  they slept for. Under a seed one `suspend()` need not run a new coroutine, so 116, 126 and the scope tests above
  loop until theirs have run or parked.
- 2026-10-12 S14.3, the same for the reactor: `reactor/026-trigger_wakes_from_another_thread.phpt` arms the
  thread's fire in the waiter, which nothing else can wake, and checks the state while it is parked;
  `reactor/028-trigger_fire_without_waiter_dropped.phpt` fires its burst once the waiter is linked and awaits the
  first wake; `reactor/036-trigger_child_has_own_wake_descriptors.phpt` prints the parent's outcome after
  `pcntl_waitpid()`.
- 2026-10-12 S14.3, timers in a fixed order: a timer armed later with a longer duration fires after an earlier one
  whatever the load, since the reactor completes due timers nearest first (`src/reactor.c`, `timers_run`).
  `scope/166-deadline_of_cancelled_scope_keeps_zombie_coroutine_finally.phpt`,
  `scope/168-deadline_of_completed_parent_keeps_finally_of_cancelled_child_member.phpt`,
  `await/116-timeout_shared_deadline.phpt` and `channel/120-channel_soft_timer_fires_while_another_sleeps.phpt`
  arm their pairs so; `scope/100-disposeAfterTimeout_earliest_wins.phpt` arms a 1 s delay after the deadlines,
  which the 5 s one would follow; in `io_provider/015-select_sees_pending_connections.phpt` a 10 s delay ends
  first only if the select waits for its limit. Why: two timers of different coroutines raced on the scheduler's
  order (DECISIONS 2026-10-09, `scope/170`).
- 2026-10-12 S14.3, upper bounds of elapsed time dropped: the state they stood for is printed instead (the armed
  timers from `TrueAsync\Test\reactor_state()` in
  `scope/099-disposeAfterTimeout_object_destruction_cancels_at_once.phpt`,
  `scope/107-closed_scope_timer_withdrawn_and_zombie_awaited.phpt`,
  `reactor/006-signal_during_park_runs_handler.phpt`, whose wait grows to 3 s to give the shell's signal time; the
  order of the ends against the deadlines taken in `reactor/013-delay_order_and_bounds.phpt` and
  `io_provider/002-sleep_parks_the_coroutine.phpt`). Why: a loaded runner breaks any upper bound.
- 2026-10-12 S14.3, upper bounds dropped the other way: a wait that must be unwound prints a line after it, which
  a wait that ran out would print (`reactor/018-exit_deadline_unwinds_protect.phpt`,
  `reactor/021-exit_deadline_in_forked_child.phpt`, `reactor/023-exit_deadline_over_deferred_cancellation.phpt`,
  `reactor/025-exit_deadline_fires_again.phpt`); 021 takes its start at `exit()`, which arms the deadline, not at
  the later fork. In `reactor/019-exit_deadline_keeps_no_coroutine_from_deadlock.phpt` the `DeadlockError` is the
  witness: a deadline that fired raises none (Sage). `reactor/024-delay_with_async_off.phpt` prints
  `TrueAsync\Test\coroutine_count()` before the delay: with no coroutine current, `delay()` returns before it arms
  a Timer (Sage). `await/125-timeout_traversable_fires.phpt` keeps its lower bound. A lower bound stays where the
  deadline is the behaviour. Why: as above.
- 2026-10-12 S14.3: `collector/063-automatic_run_stops_before_memory_limit_on_wake_edges.phpt` finds the smallest
  margin above the memory in use at which a walk finds one coroutine awaiting 5000 Futures, then checks that fifty
  awaiting 5000 Futures stop there, on their edges, and are found at a larger one: the margin is measured on the
  build's heap (dbg and ASAN: 4 MiB), not written in the test.
  `collector/064-automatic_run_stops_before_memory_limit_on_candidates.phpt` (dbg only) prints the outcome at each
  margin. Each margin waits for a run (`TrueAsync\Test\collector_idle_runs()`, new, test builds only, counted at
  the idle point): a `delay(1)` that ends before the scheduler goes idle makes none. Both wait until every
  coroutine has parked. Why: the final counts alone pass a walk that wrongly completes.
- 2026-10-12 S14.3 (Sage): `await/141-many_records_of_one_future_end_in_linear_time.phpt` and
  `signal/024-many_futures_on_one_signal_leave_in_linear_time.phpt` count the callback removals that search their
  vector (`TrueAsync\Test\callback_searches()`, 0 for a thousand records or Futures) instead of a million under
  `max_execution_time`. The count is kept only in builds with test hooks, on the search branch of
  `async_callbacks_remove()`, which a removal at its slot never takes, so no hot path gains a counter (P1.4).
  `scope/124-member_finish_in_nested_scope_chain_in_linear_time.phpt` and
  `scope/125-child_scopes_leave_in_linear_time.phpt` keep `max_execution_time`, raised from 20 s to 50 s (below
  run-tests' timeout of 60 s), until Edmond decides on a counter on the completion walk (PLAN "Open questions").
  Why: time is no witness under load.
- 2026-10-12 S14.3, found by S14.2's Sage: `collector/069-handler_cancels_found_members.phpt`,
  `collector/070-unheld_error_route_wakes_found_members.phpt`,
  `collector/071-strategy_hook_cancels_found_members.phpt` and
  `collector/073-handed_out_member_completes_found_waiters_future.phpt` failed their member after `delay(20)` or
  `delay(50)` and let main wait `delay(50)`, armed first and so able to end first. The member now fails once
  `TrueAsync\Test\collector_idle_runs()` has moved, so the automatic run has reported the stuck coroutines
  whatever the load, and main waits on a flag the woken coroutine sets (070 ends by its uncaught error).
  Why: the warning has to come before the error, and the error before main's line.
- 2026-10-12 S14.3, found by the full lanes on core `c53c17cfd47`: a delay is no sync point even for work
  queued before main parks. Main's own park ticks (`scheduler.c`, `scheduler_tick()` before the pop), which
  polls timers when 100 ms have passed since the last poll (the request's first park always), and a timer
  due by then, after a preemption on a loaded machine, wakes main ahead of the queue.
  `scope/084-awaitCompletion_waiter_found_with_stuck_member.phpt` (ASAN, found nothing) and
  `collector/072-oracle_on_scope_cancels.phpt` now loop `suspend()` on a counter or flag the coroutines set.
- 2026-10-12 S14.3, found by the full ASAN lane on core `c53c17cfd47`, the same class:
  `collector/043-cancel_inside_protect.phpt` and `collector/042-cancel_runs_finally_and_request_goes_on.phpt`
  (ASAN) loop `delay(1)` on a flag, so the automatic run still comes;
  `io_provider/013-cancel_parked_socket_select.phpt` (ASAN, 2 of 200 runs) waits until the cancelled select
  has unwound (its completion, as a seed can cancel it before it starts), whose wake comes through the
  reactor, where main's due timer can come first. `scope/070-handler_closure_cycle_with_zombie_member.phpt`'s
  member threw inside main's two `gc_collect_cycles()` on ASAN; it waits for main's flag set after them.
- 2026-10-12 S14.3, found by the full dbg lane on core `c53c17cfd47`:
  `await/119-timeout_is_completed_past_deadline.phpt` took `timeout(5)` and checked it was not yet
  complete; the check passed the deadline. It takes `timeout(50)` and fails only a completion seen before
  50 ms have passed, so the deadline's unit is still checked (Critic).
- 2026-10-12 S14.3, found by the full ASAN lane on core `c53c17cfd47`: `io/046-nonblocking_pipe_read_with_data.phpt`
  gave its child 500 ms to start (ten `usleep(50000)`), which a loaded sanitizer run exceeds (DECISIONS
  2026-10-07 saw it too). It now reads until the child has written or closed its end; run-tests' timeout
  bounds a child that never starts. Why: what it tests is the read of data that is there, not the start time.
- 2026-10-12 S14.3, kept as they are: `channel/114`, `118` order by events already; `internal/069`'s
  delays are armed after the timer they race and longer; `scope/102`, `reactor/030`, `038`, `043`
  check a lower bound, where the deadline is the behaviour.
- 2026-10-12 S14.7: every CI job has `timeout-minutes` (30, `lists` 10; a seed run 10 min), and each line of
  the `seeds` job runs in five parallel jobs of consecutive seeds (`tools/test.py --seeds N --shard K/5`); all
  170 seeds stay. Why: Edmond 2026-10-12 16:05, CI tests do not run long. One job ran a line's seeds one after
  another: 51 to 65 min (run 38266038345), up to 2 h 52 min (run 38230954460); S3.12 picked 100 dbg and 20
  asan seeds for 259 tests, and the suite has about 1 780. A shard takes about 13 min (dbg 20 seeds, asan 4).
- 2026-10-12 S14.7: CI has no `schedule` trigger; the `seeds` jobs run only when CI is dispatched by hand. Why:
  Edmond 2026-10-12 16:22, "пока что ночные проверки не будем вообще запускать".
- 2026-10-12 S8.4a, own defaults (bukka's curl code, the core slot from the coordinator): the provider's `remove()` for
  a socket libcurl reports removed runs inside the socket callback, as a curl callback (`in_callback`: no
  `curl_reset()` under libcurl); its exception is kept and thrown by the reconcile, since a pending one would skip
  every later PHP callback of the transfer; a pending callback exception is set aside meanwhile, and one left by a
  bailout is released at the free. Freeing the handle ends the registrations before `curl_multi_cleanup()`, so no
  `remove()` runs inside it. Not done: a `zend_try` around that (a fatal error in `remove()` at the free leaks
  libcurl's multi) and the bridge's `opline_before_exception` (Critic, both unrun). The core update also brings
  entry 26 (`io-hooks-fixes` carries both), so `067`, `068` lose `core:26` here, ahead of S8.3b.
- 2026-10-12 S14.8: `scope/124-member_finish_in_nested_scope_chain_in_linear_time.phpt` (200 nested scopes) and
  `scope/125-child_scopes_leave_in_linear_time.phpt` (2 000 children) keep their checks, with no
  `max_execution_time`; their linear-time claims are benchmarks `B19` and `B20`, counted by hand with
  `tools/bench.py --count`. Why: Edmond 2026-10-12 16:05, no performance tests in CI. The other option, a
  test-build counter of the walk's steps as S14.3 gave `await/141`, was not taken: the benchmark changes no
  code on the finish path. Settles the PLAN open question on the two tests. S14.2 ticked before its seeds
  run, which came after (run 38282314790, S14.7).
- 2026-10-12 S14.6: the core's test scheduler gets one INI countdown per refusal, as `fail_enqueue` already was:
  `fail_wake` (an enqueue of a coroutine that is not CREATED), `fail_cancel`, `fail_finish_handler` (returns 0),
  `fail_defer`, `fail_launch` (returns NULL); a refused enqueue, cancel or defer throws an Error naming its seam.
  Why: the refusal paths HEALTH listed answer a returned failure, and TrueAsync's only scheduler seam,
  `ASYNC_TEST_FAULT`, is a fatal error. The enqueue contract now says what the core relies on: every refusal
  returns false with an Error thrown. Not reached by a test: the fiber entry's caller wake after an `exit()` the
  scheduler took over (test_scheduler never clears the exit) and `zend_fiber_coroutine_finished`'s wake with an
  error (it needs a fiber's coroutine cancelled before its start). S8.1b's limit (a kept fiber lives to the
  request's end when its finish handler throws or is skipped) is gone: the coroutine entry releases it.
- 2026-10-12 S14.6, the GC's automatic run during an unwind parks the frame with an exception pending: the core
  holds the exception and `opline_before_exception` across it, as `zend_objects_destroy_object()` does, but leaves
  the frame on the exception op: moved back to the throwing opline, the TMPVAR walks read temporaries the unwind
  had freed (ASan, `116`). Cost, Sage's call over an engine change in `cleanup_live_vars` (a stale opline after
  `FAST_RET` would still be unsafe): a cycle kept only by the current frame's live temporary waits for the
  request's end. An exit wins over a cancellation delivered meanwhile. test_scheduler asserts that `suspend` is
  called with no exception pending; our extension still holds one, harmlessly.
- 2026-10-12 S14.6, own defaults: a refused wake of the GC coroutine leaves it parked with the collector off for
  the request, documented, no retry (Sage: only a scheduler breaking its contract gets there). A Critic's case,
  our collector walking a frame parked in a destructor during an unwind, did not reproduce in two probes. The
  exception lost across that park was first fixed in test_scheduler's suspend; the core's hold replaced the fix.
- 2026-10-12 S9.33: a C walk in which a release could let other coroutines run runs in scheduler context (the scope
  cancel's walks, a channel's close by its owner scope's free); a value another reference holds gets a plain
  decrement; a replaced value is released after the store; a channel releases its hold on a served Future last and
  marks a rendezvous slot committed when it promises it. Why: a release can start a GC run that the caller waits
  for while other coroutines free or change what the walk reads next (`SECURITY.md` 2026-10-12 S9.33).
- 2026-10-12 S8.7, own defaults (the step and the core slot from the coordinator): a stream given to curl and closed
  while `curl_exec()` waits fails the transfer where its `FILE*` is used, with no PHP code inside libcurl, and
  `curl_exec()` warns after it, not a reset to the default handler mid-body (the first version re-checked after
  each wait; two Critics showed yields inside libcurl, the spill, and an error handler run inside libcurl).
  `CURLOPT_STDERR` is swapped for `stderr` after each wait without PHP code; left: it closed during a yield
  inside libcurl. Not fixed here: `in_exec` covers only the wait, so a
  callback that suspends lets another coroutine use the handle (Critic, pre-existing). The core update also
  carries `466bdd66486`: bukka's two curl comments back word for word (Edmond 16:32), and the PR branch was
  force-pushed to one commit (17:04). Not in this core: `async-core` `f754dc9b194` (Bob Weinand's comment
  on the fiber cleanup back word for word), local; its push to `async-core` was refused without his word.
- 2026-10-12 S14.9: a test no longer waits for work of other coroutines with a top-level `delay()`. Main's own park
  polls the due timers before it pops the run queue at the request's first poll and when 100 ms have passed since
  the last one (`src/scheduler.c`, `scheduler_tick()`), so main's timer, due by then after a preemption, wakes it
  ahead of the queued coroutines; and a timer of another coroutine started late under load ends after main's. Under
  `--jobs 16` with four busy loops beside, 18 of 25 ASAN runs of the 99 own tests with such a delay had 1-3
  failures: 13 tests failed and 2 more passed only on run-tests' retry; CI run 38282314790 (ASAN, 89c944d) hit
  `scope/074`. After: 25 such runs of the 87 changed tests and the 15 kept beside them were all clean, and 10 more
  after the review's fixes; 20 seeds on `pocs-dbg` and `pocs-asan` found `scope/083` hung (fixed, below) and then
  nothing; the full lanes pass with 0 unexpected.
- 2026-10-12 S14.9, how: main waits on the event: queued work by a `suspend()` loop on a flag, a count or
  `isCompleted()`, work behind a timer or a signal by a `delay(1)` loop, a coroutine that ends normally by
  `await()`. A regression the case guards now shows as run-tests' timeout instead of a wrong line, as with S14.3's
  loops. A later and longer main delay stays sound only where the earlier timer's fire itself does what main checks
  (`scope/166`, `168`, `await/116`); where the fire queues a coroutine that prints, it is not.
- 2026-10-12 S14.9, waits for the coroutine whose error the case routes:
  `scope/061-route_skipped_for_awaited_error_and_cancellation.phpt`, `scope/065-handler_cannot_suspend.phpt`,
  `scope/072-handler_fatal_error_ends_request.phpt`,
  `scope/074-route_from_global_scope_closes_empty_child_scopes.phpt`,
  `scope/075-handler_exception_wrapping_the_error_keeps_one_previous.phpt`,
  `scope/079-awaitCompletion_refused_in_exception_handler.phpt` and
  `scope/103-awaitAfterCancellation_token_and_refusals.phpt`;
  `scope/062-handler_exception_goes_up_with_the_error_as_previous.phpt` and
  `scope/063-child_scope_handler_takes_errors_of_child_scopes_only.phpt` wait for the members the route cancels,
  whose own waits now take `delay(100000)`: a `delay(50)` could end before the thrower ran. In `scope/103` the
  zombie also starts for sure and waits on a Future main completes after its last line, which a 200 ms delay could
  not promise.
- 2026-10-12 S14.9, waits on a flag a finally handler sets after its line:
  `scope/111-finally_runs_when_error_cancels_scope.phpt`,
  `scope/142-cancel_leaves_finally_of_member_that_returned_running.phpt`,
  `scope/143-parent_cancel_leaves_child_scope_member_finally_running.phpt`,
  `scope/144-sibling_error_route_leaves_member_finally_running.phpt`,
  `scope/145-cancel_after_fast_finally_ended_leaves_slow_one_running.phpt`,
  `scope/146-parent_cancel_leaves_scope_finally_after_object_release_running.phpt`,
  `scope/154-stand_in_of_finally_run_scope_does_not_cancel_handler.phpt`,
  `scope/155-stand_in_disposeAfterTimeout_of_finally_run_scope_ignored.phpt` and
  `channel/115-channel_closed_by_dispose_or_cancel_of_completed_scope.phpt`.
- 2026-10-12 S14.9, counts of the coroutines that reach their park (a coroutine runs on to it) and that end:
  `scope/076-awaitCompletion_woken_by_cancel.phpt`,
  `scope/081-await_all_iterator_spawns_cancelled_with_callers_scope.phpt`,
  `scope/082-awaitCompletion_returns_after_safe_cancel.phpt`,
  `scope/086-awaitCompletion_waiter_live_while_scope_held.phpt`,
  `scope/087-awaitCompletion_waiter_found_with_stuck_child_scope_member.phpt`,
  `scope/088-awaitCompletion_found_waiter_takes_member_error.phpt`,
  `scope/097-awaitAfterCancellation_waiter_found_with_stuck_zombie.phpt`,
  `scope/101-dispose_running_scope_cancels_without_closing.phpt` and
  `scope/108-disposeAfterTimeout_cancelled_scope_timer_not_live.phpt`.
- 2026-10-12 S14.9, the same: `channel/119-channel_held_receiver_parked_after_scope_completes.phpt`,
  `channel/128-channel_timer_armed_for_future_at_receiver_exit.phpt` and `collector/032-signal_future_waiter.phpt`
  wait for a flag set before the park (in `collector/032` after the signal's watch, which a signal sent first would
  miss and the process die by); `collector/020-automatic_off.phpt` counts both parks, then gives one idle point,
  which a park that wakes in place would skip, so the check would pass with no walk to check (the walk is off and
  counts no run).
- 2026-10-12 S14.9, awaits instead: `channel/066-channel_recvAsync_future_dropped.phpt` awaits the sender; in
  `scope/067-handled_unheld_error_does_not_end_request.phpt`,
  `scope/077-awaitCompletion_waiter_takes_unhandled_error.phpt` and
  `scope/083-awaitCompletion_waiter_misses_error_with_safe_disposal.phpt` the sibling that must outlast the error
  loops `delay(1)` until the handler or the waiter has run, main awaits it, the cancelled member of `077` waits on
  `delay(100000)`, and the member of `083` waits for the sibling's line, so the order is the case's, not the
  timers'. Their thrower waits until the waiter is in `awaitCompletion()` and the others have started: a seed
  cancelled the sibling unstarted and hung `083`. `scope/071-route_from_global_scope_cancels_child_scopes.phpt`
  waits until its member completes (a seed can cancel it unstarted, and an await would throw), the member staying on
  a timer until the safe cancellation has come; `io_provider/005-cancel_parked_pipe_read.phpt` and
  `io_provider/012-cancel_parked_stream_select.phpt` wait until the cancelled wait has unwound, as S14.3's
  `io_provider/013`.
- 2026-10-12 S14.9, the automatic run at interval 0: `collector/018-automatic_report_once.phpt`,
  `collector/024-exit_in_error_handler.phpt`, `collector/025-fatal_in_error_handler.phpt`,
  `collector/050-warning_for_internal_callable_body.phpt`,
  `collector/057-automatic_run_stops_before_memory_limit.phpt`,
  `collector/058-warning_after_error_reporting_turned_back_on.phpt`, `collector/059-error_handler_that_waits.phpt`,
  `scope/089-awaitCompletion_waiter_cancelled_by_partial_deadlock.phpt` and
  `scope/092-awaitCompletion_found_waiter_in_fatal_error.phpt` wait until `TrueAsync\Test\collector_idle_runs()`
  moves, as S14.3's `collector/069`.
- 2026-10-12 S14.9, the same for runs that cancel: `collector/044-cancel_again_and_global_deadlock_after.phpt`,
  `collector/045-cancel_each_even_when_one_finally_would_wake_another.phpt`,
  `task_group/116-collector_finds_foreach_on_group_of_lone_receiver.phpt` and
  `task_group/130-collector_finds_spawner_parked_on_group_of_lone_receiver.phpt` loop `delay(1)` on a flag or count
  their coroutines set, so the idle point still comes.
- 2026-10-12 S14.9, deadlines: `scope/112-finally_handler_suspends_next_runs.phpt`,
  `scope/117-finally_error_with_cancelled_handler.phpt`,
  `scope/140-dispose_of_cancelled_scope_leaves_member_finally_running.phpt`,
  `scope/147-disposeAfterTimeout_of_cancelled_scope_stops_hanging_finally.phpt`,
  `scope/148-disposeAfterTimeout_stops_hanging_finally_under_allowZombies.phpt`,
  `scope/149-disposeAfterTimeout_after_dispose_stops_hanging_finally.phpt`,
  `scope/150-second_disposeAfterTimeout_stops_finally_that_caught_first.phpt`,
  `scope/151-deadline_runs_finally_of_idle_child_its_cancel_closes.phpt` and
  `scope/153-dispose_keeps_deadline_for_scope_finally_it_starts.phpt` loop `delay(1)` until the line the deadline or
  a finally's timer causes is out, or its cancelled member has ended.
- 2026-10-12 S14.9, the same for deadlines:
  `scope/156-deadline_stops_finally_of_finally_worker_after_run_ended.phpt`,
  `scope/157-deadline_keeps_waiting_finally_worker_out_of_deadlock_report.phpt`,
  `scope/158-deadline_drops_coroutine_finally_of_member_it_cancels.phpt`,
  `scope/159-deadline_drops_scope_finally_after_members_unwind.phpt`,
  `scope/160-parent_deadline_drops_coroutine_finally_in_child_scope.phpt`,
  `scope/161-deadline_drops_finally_of_finally_worker_it_stops.phpt`,
  `scope/162-deadline_releases_dropped_finally_closure.phpt` and
  `scope/163-child_deadline_leaves_parent_scope_finally.phpt`.
- 2026-10-12 S14.9, the same for deadlines:
  `scope/165-safe_scope_deadline_drops_finally_of_finally_worker_it_stops.phpt`,
  `scope/169-safe_scope_deadline_runs_finally_of_idle_child_its_cancel_closes.phpt`,
  `scope/171-deadline_that_stops_only_finally_run_keeps_scope_finally.phpt`,
  `scope/172-deadline_of_cancelled_scope_drops_finally_of_member_it_interrupts_below.phpt` and
  `scope/106-disposeAfterTimeout_rearmed_in_forked_child.phpt` (in the forked child). In `169` and `172` the member
  that keeps the scope running loops on that line too: a fixed delay could end before the fire, and a long one would
  hang (the fire leaves it running and the request waits for it).
- 2026-10-12 S14.9, waits that a cancel must end take `delay(100000)`, S14.3's idiom: the route's or the request's
  end in `scope/060-route_error_in_unsafe_scope_cancels_request.phpt`, `scope/066-handler_exit_ends_request.phpt`,
  `scope/068-route_stops_at_root_scope.phpt`,
  `scope/090-awaitCompletion_found_waiter_woken_by_unheld_error_route.phpt`,
  `scope/111-finally_runs_when_error_cancels_scope.phpt` and
  `bailout/017-scope_disposed_during_bailout_drops_finally.phpt` (in `060`, `066` and `068` the members' delays
  too), the stand-in's deadline in `scope/167-global_scope_stand_in_deadline_keeps_later_finally.phpt`. In
  `scope/090` main must not hold the thrower: holding skips the check for an unheld error (`src/coroutine.c`), and
  the request went on; the thrower itself waits for the run with `collector_idle_runs()`.
- 2026-10-12 S14.9, checks that a timer of the case raced: in
  `collector/027-future_state_kept_by_running_coroutine.phpt`,
  `scope/085-awaitCompletion_waiter_live_while_member_may_finish.phpt`,
  `context/029-scope_object_kept_while_zombie_reads_context.phpt` and
  `scope/091-await_all_iterator_keeps_its_scope_reachable.phpt` the coroutine (in `091` the iterator) stays parked
  on a timer (`delay(1)` loop) until main has checked; in `collector/033-await_dead_future_with_timeout_token.phpt`,
  `collector/034-await_any_with_timeout_token.phpt` and
  `collector/036-waiter_with_timeout_keeps_state_of_other_waiter.phpt` the waiter keeps its `timeout(30)` and main
  prints its outcome after the check; in `scope/098-disposeAfterTimeout_timer_keeps_members_reachable.phpt` the
  deadline is armed after the parks, just before the check.
- 2026-10-12 S14.9, kept: `collector/003` (main's own timer is the case); `collector/019`, `047`, `074` (each
  `delay(1)` is one idle point under `collector_age()`; a park that wakes in place would skip one, not seen in 50
  runs, and only a count of idle points would show it); `channel/141`, `io_provider/020` (a negative check a race
  can only pass); `scope/113` (the delay follows a line that must not print); `io/081`, `io_provider/072` (SKIP
  until RFC-CHANGES 11); `io_provider/004` (the delay installs the hooks); `await/116`, `119`, `scope/166`, `168`,
  `collector/042` (S14.3's timer pairs and loop).
- 2026-10-12 S8.8, own defaults: the libcurl versions come from TrueAsync's curl history (7.87.0: segfault
  below it; 8.5.0: the system's and CI's; 8.10.1, 8.11.0: the timer hang, 8.11.1 its fix; 8.13.0: the last
  that runs the socket callback inside `curl_multi_cleanup()`; 8.20.0: the DNS timeout; 8.22.0: the latest).
  The run is a local script, not a CI job: each version is built from source, and CI stays short (Edmond,
  16:05). `LD_LIBRARY_PATH` joins the variables `tools/test.py` hands to run-tests, so a run can load
  another libcurl. A core test whose SKIPIF needs a newer libcurl than the headers the core was built
  against is listed apart: it reads a `CURLINFO_*` the core does not have. Changed without Edmond's word
  (an obvious fix), they find the socket libcurl closed by its going, not by log wording or a socket count:
  `io_provider/073-poll_queue_curl_socket_number_reused_before_its_removal.phpt`,
  `io_provider/074-curl_socket_number_reused_before_its_removal.phpt`.
- 2026-10-12 S9.32: S9 layer 5, `Async\Pool` with the circuit breaker, is TrueAsync's `pool.c` on our wait model,
  with no pool type in the core and no C-handler pool until the PDO pool needs one, and with the 14 departures of
  `dev/plans/S9-pool.md` section 8. Why: the core has no pool slots and the PDO pool is out of the first version
  (2026-10-08 S10 outcomes); items 2-11 fix TrueAsync defects found by probes and by reading
  (`/mnt/project-files/s9/probes/s9.pool/`), checked by a Critic and the Sage; `acquire(int $timeout)` stays
  TrueAsync's (a token would break `acquire(5000)` and `catch (TimeoutException)`).
- 2026-10-12 S9.32, Edmond's answers: an `Exception` from `beforeAcquire`, `beforeRelease` or the healthcheck is a
  silent rejection, an `Error` destroys the resource and passes to the caller (printed in the check), a strategy's
  exception passes to the caller (18:38, option C); `release()` refuses an object the pool did not hand out or
  already got back, by a weak-key count per object, if the code stays small (18:39, B: «это баг в TrueAsync»);
  while INACTIVE with a strategy set, `acquire()` and `tryAcquire()` ask `shouldRecover()` and on `true` move to
  RECOVERING (18:39, B). Why: TrueAsync loses bugs and cancellations in hooks, hands one connection to two
  coroutines, and never calls `shouldRecover()`, which its documentation describes.
- 2026-10-12 S8.3a, own default (Edmond 18:43: «сперва пофикси Ring»): on Linux a reactor entry compares its
  queue's pid with one kept per process (set in MINIT, refreshed by a `pthread_atfork()` child handler), not
  with `getpid()`: a file read through the Ring asked it 6 times per op, 4 here and 2 in the core's
  `php_io_ring_foreign()`, about 0.8 of the 1.9 µs it lost to a `read()` on the thread. The core half is
  `io-hooks-fixes-ring-pid`, for the next core update. Elsewhere `getpid()` stays: FreeBSD's `pcntl_rfork()`
  runs no atfork handler (the Critic), and macOS may keep a handler after its library is unloaded (not
  checked). It also stays for the wake pair (any thread, a signal handler) and after a failed registration.
  Release, 32 MiB files from the page cache, 8 KiB reads: Ring 14.3 → 10.8 ms, the thread 6.1 (medians of 11).
- 2026-10-12 S9.38: in a coroutine, a root that finds the buffer full only queues the GC run and goes on, as in
  TrueAsync; it waits only after adding another 100 000 roots past the queued run without yielding; an explicit
  `gc_collect_cycles()` waits as before (Edmond 17:29, a Critic agreeing; core `async-core` `b318dcd834c`). Why: the
  wait was a suspension at any release, the VM's of `$this` after every call included: other coroutines ran inside
  C code and a cancel was thrown after `recv()` took its message
  (`channel/154-recv_keeps_message_when_cancelled_during_gc_wait_after_call.phpt`, no longer XFAIL). Supersedes the
  rejection in the 2026-10-07 HI-priority entry: that loop of 3 000 000 cycles in a coroutine under 128M peaks at 60.3M
  with 27 runs (debug build, measured 2026-10-12).
  Rejected: a wait at 3/4 of `memory_limit` (no limit getter in the core, `-1` never fires, an application whose
  live data sits above it would wait at every run). The RFC's GC chapter and impact section say so.
- 2026-10-12 S9.38, tests that waited at the threshold: `gc/018-gc_threshold_stable_without_coroutine.phpt` and
  `gc/023-gc_threshold_untouched_by_a_run_that_found_nothing.phpt` are the reference's again, word for word;
  `gc/022-gc_threshold_sync_mode_still_adjusts.phpt` waits for the run with `gc_collect_cycles()` before it reads
  the threshold; `scheduler/016-no_stack_full_gc_buffer.phpt` prints "end" before the refused GC coroutine's
  fatal error; `scheduler/024-gc_while_exception_unwinds.phpt` and
  `scheduler/062-collection_in_handler_while_unrun_coroutine_finishes.phpt` fill 100 000 roots past the threshold
  to wait where they waited before. Why: no coroutine waits at the threshold any more (the S9.38 entry above).
- 2026-10-12 S9.38, S9.31's and S9.33's channel tests of a release that waits for the run inside a C walk fill
  100 000 roots past the threshold, where the coroutine still waits:
  `await/149-await_any_or_fail_cancel_during_gc_wait_leaves_item_error_chain.phpt`,
  `channel/150-send_to_pending_recvAsync_leaves_later_value_uncommitted.phpt`,
  `channel/151-recv_with_token_keeps_message_when_cancelled_during_gc_wait.phpt`,
  `channel/152-send_to_chained_recvAsync_leaves_later_value_uncommitted.phpt`,
  `channel/155-owner_scope_free_with_gc_run_due_while_channel_dropped.phpt`,
  `channel/156-recvAsync_with_gc_run_due_wakes_delivering_sender.phpt`. Why: at the threshold they passed without
  reaching their window. `channel/153-handed_on_value_to_chained_recvAsync_keeps_waiting_sender_first.phpt`
  cannot reach it now (the receiver resumes by its cancel, never 100 000 roots past) and keeps the order only.
- 2026-10-12 S9.38, the same for the scope and group walks:
  `scope/190-child_free_with_gc_run_due_while_parent_dropped.phpt`,
  `scope/191-cancel_with_gc_run_due_inside_walk.phpt`, `scope/192-dispose_safely_with_gc_run_due_inside_walk.phpt`,
  `task_group/159-gc_wait_inside_all_while_group_grows.phpt`,
  `task_group/166-destructor_with_gc_run_due_while_scope_member_finishes.phpt`,
  `task_set/036-joinAll_takes_only_settled_entries_across_gc_wait.phpt`, whose spawner also catches the closed set
  that a random scheduling seed lets `joinAll()` close first (a fatal error in seed 7).
- 2026-10-12 S9.38, `gc/025-gc_run_first_keeps_100000_coroutines_from_parking.phpt` takes `fiber.stack_size=1M`.
  Why: main no longer yields at each threshold, so all 100 000 coroutines are queued before any runs and the
  10 000 that suspend once are parked beside the 10 000 that loop: 20 079 stacks at once, not 14 041 (strace,
  debug build, the core before and after b318dcd834c). Windows commits whole stacks, and 20 079 stacks of 2M
  overran the CI runner's paging file ("VirtualAlloc failed: [0x000005af]"); 1M makes 20.1 GiB of stacks, under
  the 27.7 GiB of the old peak (28.4 GB `VmPeak` in the 2026-10-08 entry) that passed. This narrows that entry's
  rejection of a smaller stack: ASAN's reserved 480K rules out 256K, not 1M, which passes on ASAN.
- 2026-10-12 S8.3a, core `async-core-io-2026-10-12-5` `46369b6a6a5` = `-4` with `io-hooks-fixes` fast-forwarded to
  `b6ede162357`, the core half of the pid entry above (`RFC-CHANGES.md` 36). Core suites (test_scheduler, poll,
  stream hooks, curl, sockets; the Ring's curl and hooks) pass as on `-4`, debug and ASAN; lanes pocs-dbg and
  pocs-asan pass. One ASAN WARN (passed on retry) in `ext/sockets/tests/socket_sendto_recvfrom_afpacket.phpt`
  was not the change: it passes 5 of 5 alone; it captures every loopback frame, so parallel tests disturb it.
- 2026-10-12 S9.34, changed reference tests: `pool/068-pool_without_a_pool_throws.phpt` checks that
  `unserialize()` and `ReflectionClass::newInstanceWithoutConstructor()` refuse `Async\Pool`, since our pool is
  `@not-serializable` and has no state without a constructor (TrueAsync builds an empty pool by `unserialize()`);
  `pool/048-pool_healthcheck_basic.phpt` and `pool/049-pool_healthcheck_exception.phpt` wait for a `FutureState`
  the healthcheck (048) or the destructor (049) completes, with a keeper coroutine on a timer, instead of
  `delay(120)`; `pool/054-pool_broken_release_wakes_waiters.phpt` and
  `pool/024-pool_max_size_concurrent_factory.phpt` wait in `suspend()` loops for a counter of parked or started
  coroutines instead of `delay()`; `pool/056-pool_counts_resource_during_hooks.phpt` reads `count()` inside its
  hooks, before and after their `delay(30)`, instead of from a probe coroutine. Why: no delay as a sync point.
  All six pass on TrueAsync's build but `068`, which TrueAsync's `unserialize()` fails.
- 2026-10-12 S9.34, own defaults of the pool: `active` drops after the destructor returns, so `count()` covers a
  resource being destroyed; a pool closed during `beforeAcquire` or the factory destroys that resource, and
  `acquire()` throws "Pool is closed"; a pre-warm failure closes the pool before the factory's exception leaves
  the constructor; `$min` and `$max` above `UINT32_MAX` are clamped to it; the healthcheck callable and interval
  are kept but unused until S9.35; TrueAsync's "Failed to create pool resource" wrapper is dropped, the factory's
  own exception passes. Why: each is the reading of `dev/plans/S9-pool.md` with the least code.
- 2026-10-12 S9.34, hook outcomes: an `Exception` from `beforeRelease` is itself passed to `reportFailure()`,
  a `false` passes a new `Exception("Resource validation failed")`. A token's `OperationCanceledException` in a
  hook is an `Error` as any other (`pool/097-token_cancel_in_beforeAcquire_passes_to_caller.phpt`): a hook that
  takes a timeout as a dead resource catches it and returns false; the class cannot tell the hook's token from a
  cancel of the caller. After an `Error`, a cancellation or `exit()` the destructor still runs; its exception goes
  beneath the hook's error, a cancellation of the destructor goes on top. One rule merges two exceptions at every
  site: an exit wins, and an exception other holders share (a scope cancel, a Future's error) gets no previous,
  the other goes unobserved and prints at the request's end. `release()` counts PHP resources by handle, other
  non-objects by number. A by-reference factory or hook is unwrapped. Why: the Critics; Edmond's option C.
  `PoolException` lives in `pool.stub.php`, as in TrueAsync.
- 2026-10-12 S9.35, own defaults of the pool's waits: one deadline for the whole `acquire()` call, from its first
  park (S9-pool.md section 8, item 15); the timeout throws TrueAsync's `TimeoutException("Operation has been
  cancelled by timeout")`; a woken waiter keeps its queue place until it runs, and its timer is withdrawn at the
  wake, so a promise always wins over a timer not yet fired; every exit of `acquire()` that frees a place without
  taking it wakes a waiter (`pool/120-circuit_refusal_after_rejection_wakes_waiter.phpt`). Why: the least code
  that keeps section 4's rule that no waiter stays parked beside a free place.
- 2026-10-12 S9.35, the healthcheck: the check stops at a closed pool or at its own cancellation even when a hook
  swallows it (`pool/122-check_stops_at_swallowed_cancellation.phpt`,
  `pool/123-check_stops_at_swallowed_graceful_shutdown.phpt`); a pool closed by the check's last release prints
  its destructor's exception at the request's end (`pool/119-destructor_exception_at_check_last_release_printed.phpt`),
  and a cancellation from it is dropped when the check never ran; an unrun check cancelled by a global cancel
  leaves the pool without a healthcheck. Not tested, by design or because only time would show them: a tick
  while a check runs (the timer is re-armed only at the check's end), a check after `graceful_shutdown()`, a tick
  over a place promised to a woken waiter, a timeout after a wake. Why: the Critic of S9.35.
- 2026-10-12 S9.36, the pool's exception order: one rule at every site of `pool.c` where a destructor runs after
  another exception (`pool_exception_settle()`): the earlier exception stays on top with the destructor's beneath,
  unless only the destructor's is a cancellation, which goes on top. By that rule `close()` puts its destructors'
  chain under an exception in flight (a pre-warm factory's exception, a cancellation of the unwind) and keeps a
  cancellation in the chain on top of later destructors' exceptions; `release()` merges the strategy's exception
  with the destructor's. A shared exception in flight takes no previous; the destructor's prints at request end.
  A pool closed while `shouldRecover()` suspends throws "Pool is closed" and calls no factory. Why: the layer
  Critic of S9.36 (`pool/129`-`135`); a cancelled coroutine must not run on behind a `catch (Exception)`.
- 2026-10-12 S9.36, not changed: a `beforeAcquire` hook that catches the caller's cancellation and returns a
  verdict goes on as any code that swallows a cancellation does; the coroutine is not cancelled again, so
  `acquire()` may still hand it a resource. Why: a swallowed cancellation lets the coroutine run on everywhere in
  this extension (a second `delay()` after a caught cancel sleeps), and the hook is the user's own code.
- 2026-10-12 S9.36, Mull survivors on `pool.c` kept: line 205 (`==` to `!=`) survives while the new hash slot's
  value happens to be zero; 741 and 779 (`> 0` to `>= 0` on the free places) need a check that resumes between
  a release promising the last place and the woken waiter's run; 1105 is equivalent (`UINT32_MAX` either way);
  1210 (`<` to `<=`) reads past the array, which only ASAN sees, and Mull builds debug. The run-tests timeout is
  10 s for such runs (`TEST_TIMEOUT=10`): a mutant that hangs a test cost 60 s. Why: S9.30's practice.
- 2026-10-13 S9.37, the constructor's pre-warm works as the healthcheck's refill does: it stops at `min` resources
  counted idle and active, at no free place, or when the pool is closed, which destroys the resource just made with
  the destructor callable, and wakes a waiter for each resource made; it still calls the factory at most `min`
  times, as TrueAsync. Why: the factory reaches the pool through `debug_backtrace()`; an `acquire()` there pushed
  the pool past `max` and the idle buffer past its limit, a `close()` left resources idle in the closed pool, a
  waiter parked meanwhile was never woken (`pool/144`-`146`, the security pass and its Critic).
- 2026-10-13 S9.37, not changed: after `exit()` in the destructor callable, `close()` still destroys the other
  idle resources, and the exit wins at the end. Why: the destructor is cleanup, as a `finally` is, and TrueAsync's
  close destroys every idle resource (pool.c:1121-1181); a resource left undestroyed keeps its connection open.
- 2026-10-13 S8.3a, `PHP_IO_HOOKS_F_FILES` stays off: regular files run on the thread, a limitation in the README.
  With it on, the core freezes a stream while one op waits (B1) and a second coroutine on the same file handle
  throws "Concurrent access to a stream": eight `io/` tests with a shared handle, two coroutines including from one
  phar. TrueAsync queues a handle's reads and writes in libuv (`libuv_reactor.c:6385-6396`, `6610-6625`); a queue
  here needs the core to hand the second user to the provider as a wait (`RFC-CHANGES.md` 12). The core fix the
  draft needed (a descriptor cast seeking a frozen stream, `main/streams/cast.c`) is not sent: with files on the
  thread no seekable stream is frozen. Why: Edmond 2026-10-13 02:55, «не делать async для files».
- 2026-10-13 S8.3b, a socket call that fails for a pending exception reports only it, at every report site the
  failed call reaches (accept, connect, lookup, `sendto()`'s address, the TLS handshake): the check is
  `EG(exception)`, not `ECANCELED`, because an accept has no error code, a cancelled lookup gives `EAI_SYSTEM`
  and a lookup failure leaves `$errno` 0. A PHP function starts with no exception pending, so only an exception
  of the call itself silences a report; the core's `session_resumption_invalid_data.phpt` (a TypeError from
  `session_data`) loses its two warnings, the TypeError being the report. Why: RFC-CHANGES 27 and 37, the
  S8.3b Critic; TrueAsync warns on a cancelled accept (`network_async.c:1264-1272`), a departure we prefer.
- 2026-10-13 S9.39: S9 layer 6, iterators, is TrueAsync's `Async\iterate()` alone, on S9.6's iterator core with
  a hook for the walk's end and an ITERATE wait kind; no core change (`dev/plans/S9-iterate.md`). Why: it is
  TrueAsync's only public iterator; FileSystemWatcher's iterator is its own layer.
- 2026-10-13 S9.39, Edmond's answer: a cancel of `iterate()`'s caller cancels the iterator's scope (callbacks and
  the coroutines they spawned, whatever `$cancelPending`), waits until they end and throws the cancellation; a
  second cancel during that wait ends it at once. Why: Edmond 03:38 «поведение TrueAsync похоже на баг... в
  случае отмены обход тоже должен прерваться»; TrueAsync runs the walk to its end (probe i1).
- 2026-10-13 S9.39, departures decided here (note section 6): `$concurrency` outside 0-4294967295 is a
  `ValueError` (TrueAsync: -1 is no limit), by Edmond's rule of 2026-10-12 that a programmer's error is caught
  where the check is small; `exit()` in a callback ends the request (TrueAsync returns from `iterate()`, i11); a
  spawned coroutine's error during the walk is `iterate()`'s exception, as after the walk (TrueAsync ends the
  request with "Uncaught", i22).
- 2026-10-13 S8.6, `stream_set_timeout()` bounds `stream_socket_recvfrom()` and `sendto()` only once it is called;
  `default_socket_timeout` stays off them, in a coroutine too. TrueAsync applies it in a coroutine (60 s), which
  would end a UDP server's receive loop after a quiet minute; the PoC's `stream_socket_recvfrom_blocking_no_timeout.phpt`
  keeps it off. Why: Edmond 2026-10-13 06:02 «если так ведёт себя сам пхп... можно и так».
- 2026-10-13 S8.6, a pipe's read timeout works on Unix only: Windows refuses `stream_set_timeout()` on a pipe as
  before, and `tests/io/039-pipe_read_timeout.phpt`, `tests/io/040-pipe_read_timeout_then_success.phpt`,
  `tests/io/042-pipe_timeout_concurrent_coroutines.phpt`, `tests/io/043-pipe_fread_timeout.phpt` carry
  `xfail-on:pocs-win(S8.9)`. Why: the S8.6 Critic: a timed-out overlapped READ cancelled at its completion loses
  its bytes, and the provider-less paths still wait; a readiness wait first, as on Unix, is S8.9.
- 2026-10-13 S9.40, `iterate()`'s scope has a subscriber for the whole call that takes the route's errors, marked
  `ASYNC_CALLBACK_F_TAKES_ERROR`, which `async_callbacks_notify()` counts as a wait record; the caller's record only
  wakes it. Why: an enqueue unlinks the record (D26), and a child's error while the cancelled caller waits in the
  queue went past the iterator's scope and ended the request (`tests/iterate/049-child_error_while_cancelled_caller_is_queued.phpt`,
  the S9.40 Critic). TrueAsync stops no such error at the scope (S9.39, i22).
- 2026-10-13 S9.40, with `$cancelPending` the last worker yields once before cancelling the coroutines the callbacks
  spawned, whatever it ends with but an exit, and a scope already cancelled (safely from above, or by the walk's error) is cancelled again with
  `async_scope_cancel_remaining()`. Why: reference test 012 lets them run first, as TrueAsync's caller runs after
  them; a zombie that a safe cancel left otherwise hangs the call
  (`tests/iterate/050-cancel_pending_after_safe_scope_cancel.phpt`,
  `tests/iterate/051-cancel_pending_after_worker_cancelled.phpt`, both Critics).
- 2026-10-13 S9.40, a second cancel of `iterate()`'s caller only ends its wait and is dropped: the first cancel stays
  the outcome, with the walk's errors beneath; an exit stays the outcome over all. A route error after the first one
  goes to the unobserved exceptions. S9.36's merge moved from `src/pool.c` to `src/exceptions.c`
  (`async_exception_settle()`, `async_exception_merge()`). Why: one rule for both users; a second cancellation printed
  as "Uncaught" otherwise (`tests/iterate/043-second_caller_cancel_leaves_at_once.phpt`).
- 2026-10-13 S9.41, a route error and the first cancel of `iterate()`'s caller stop the walk as a callback's exception
  does; before, a callback that caught its cancellation let its worker take the next element. Why: Edmond's answer B
  of S9.39 (a cancel stops the walk) and note section 6 item 6 (`tests/iterate/055-caller_cancel_caught_by_callback_stops_walk.phpt`,
  `tests/iterate/056-child_error_stops_walk_when_callback_catches.phpt`, the S9.41 Critic).
- 2026-10-13 S9.41, `$cancelPending` in a scope cancelled before spares a coroutine that cancel reached, unless it is a
  zombie: it finishes its cleanup; a coroutine spawned after the cancel and the zombies are cancelled
  (`async_scope_cancel_remaining()`'s `spare_cancelled`). The caller's cancel still reaches every coroutine. Why:
  TrueAsync cancels nothing in a cancelled scope (`async.c:1084`); a zombie or a new coroutine would hang the call
  (`tests/iterate/057-cancel_pending_lets_cancelled_child_finish_cleanup.phpt`,
  `tests/iterate/058-cancel_pending_cancels_child_spawned_after_walk_cancel.phpt`, both Critics).
- 2026-10-13 S9.41, a destructor's error when `iterate()` releases the walk goes under the outcome by S9.36's rule.
  Why: it hit `async_exception_settle()`'s assertion (`tests/iterate/054-destructor_error_at_release_goes_under_outcome.phpt`).
- 2026-10-13 S9.41, a cancellation that the iterator's handler throws while its worker is not cancelled (an expired
  `timeout()` in an `iterate()` callback, a `throw new AsyncCancellation`) is the walk's exception: it stops the walk,
  cancels the scope and is thrown by `iterate()`; a cancel of the worker itself still only ends it. Why: Edmond
  2026-10-13 06:00, «B»; TrueAsync drops it, `iterate()` returned normally and with `concurrency: 1` skipped the
  elements left (`tests/iterate/059-callback_own_cancellation_stops_walk.phpt`,
  `tests/iterate/060-callback_own_cancellation_cancels_other_callbacks.phpt`). `async_exception_merge()` now drops a
  cancellation it would set aside under a shared exception, as a cancellation is never printed as uncaught (Critic).
- 2026-10-13 S9.41, Mull survivors on `iterator.c` kept: line 586 (`record->event != NULL` to `==`) only adds wakes,
  and the caller's loop waits again while the scope has coroutines; line 827 is the refused worker start, which no
  PHP code reaches (coverage). Why: S9.36's practice.
- 2026-10-13 S8.9, a read of an overlapped pipe on Windows under `stream_set_timeout()` waits first in an Any of
  the pipe's Poll and a Timer, as `stream_select()` does, then reads only what is there; `tests/io/039-pipe_read_timeout.phpt`,
  `tests/io/040-pipe_read_timeout_then_success.phpt`, `tests/io/042-pipe_timeout_concurrent_coroutines.phpt`,
  `tests/io/043-pipe_fread_timeout.phpt` lose `xfail-on:pocs-win(S8.9)`. Why: the Critic: a single Poll with a
  deadline left its cancelled zero-byte read unkept by the Ring, which the Any path keeps and drains.
- 2026-10-13 S8.10, the core tests `hooks/connect-throws.phpt`, `connect-throws-one-attempt.phpt`, `fsockopen-throws.phpt`
  expect a Connect or a Poll op; the one-attempt test counts connections at its listener, not provider ops. Why: the
  connect-started fix waits with a Poll, and after the exception no op reaches the provider, so the count could not fail.
- 2026-10-13 S8.11, Edmond's review of the bukka branches: the comment repeated over each `!EG(exception)` guard is
  gone (the commits say why); the guards read `EXPECTED(!EG(exception))`, the end of the address loop
  `UNEXPECTED(EG(exception))`. `io_hooks_handshake_cancelled_client.phpt` completes a Poll as it does a Connect, so
  it reaches the handshake with either version of the connect-started fix. Why: the core's Poll made the provider
  throw before the handshake, so the test checked nothing since `-4`.
- 2026-10-13 S9.42, an exception set aside that is already in the pending one's chain (the pending one, or one of its
  previous) is released by `async_exception_merge()`, for `iterate()` and `Pool` alike; `iterate()`'s route
  subscriber ignores a second route error already in the chain of the walk's exception or of the first route error.
  Known limit: a second route error that a later exception of the walk takes as its previous still prints. Why:
  rethrown by a coroutine a callback spawned, or by a pool hook and the destructor, it was thrown, caught and still
  printed as "Uncaught" with exit code 255 (`tests/iterate/061-callback_exception_rethrown_by_child_not_uncaught.phpt`,
  `062-exception_rethrown_by_two_children_not_uncaught.phpt`, `063-previous_of_callback_exception_rethrown_not_uncaught.phpt`,
  `tests/pool/147-same_error_from_hook_and_destructor_not_uncaught.phpt`; the second reader, both Critics).
- 2026-10-13 S9.42, not changed: a callback's exception held elsewhere (a variable the callback captured) takes no
  previous, so a spawned coroutine's later error that is not in its chain prints at request end, by S9.36's rule for
  any shared exception; a walk's later exception still chains the earlier one under it, as TrueAsync and PHP's
  `finally` do. Why: that error reached no `catch`; the merge leaves shared objects unchanged (S9.36).
- 2026-10-13 S9.43: S9 layer 7 is TrueAsync's `Async\FileSystemWatcher` (`dev/plans/S9-fs-watcher.md`): on Linux one
  inotify descriptor per request, waited for with the POLL op on the reactor's `own` list, a parked iterator counted as
  a wait; Windows on a directory-change op requested from bukka and ior; macOS and the BSDs refused until a CI line
  builds there. Why: bukka's API and ior have no filesystem op; 128 inotify instances per user by default.
- 2026-10-13 S9.43, departures decided here (note section 6): an open watcher keeps nothing waiting unless iterated
  (TrueAsync hangs on a deadlock, the script's end and `exit()`, probes f1, f9, f10); a second constructor is an
  `Error` (f13); the root's removal closes the watcher (f7); arguments are checked (`Z_PARAM_PATH`, `open_basedir`,
  timings, extensions); buffers on the request allocator; a monitoring error and an inotify overflow are reported;
  subdirectories added with `IN_DONT_FOLLOW`, their name-less events dropped. `$extensions` without a debounce is
  question 1 of the note. Why: Edmond's rules of 2026-10-12 (catch a programmer's error when the check is small) and
  2026-10-13 (a TrueAsync bug is not copied).
- 2026-10-13 S8.12, the core takes bukka's merged connect-started fix (`7022e9af819`) as a pick on `io-hooks-fixes`,
  not bukka's whole head (that is S13.2). Why: the core's first version sent a Poll op, bukka's test expects the
  flagged Connect op, and the core should carry what bukka merged.
