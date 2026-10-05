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
  queue's pending count is wrong both ways (review M5).
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
  core's ts.c order, not TrueAsync's main-first. Why: main's bailout may land in a `zend_try` of the
  shutdown functions, and the from_main call that comes back is then a plain one (Critic); not
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
- 2026-10-05 After a fatal error (a bailout: `E_ERROR` and the other levels `php_error_cb()` bails
  out on) no queued coroutine runs, in the script, a shutdown function or a shutdown destructor;
  core `async-core` `ab94befe389`: `shutdown_destructors()` re-raises a destructor's bailout, so it
  ends the shutdown iterator coroutine as a bailout and `zend_call_destructors()` returns it to the
  last from_main call, as `c43060ea12d` does for shutdown functions (`scheduler/093`-`095`; RFC `22f5c49`). An
  `exit()` or an uncaught exception in a shutdown destructor bails out in plain PHP too (the rest of
  the destructors are skipped), so it also stops the queue. Why: Edmond, after the Critic: PHP
  runs no user code after a fatal error but shutdown functions, and `php_error_cb()` marks every
  object destructed first, so a queued coroutine ran with its destructors silently off. An uncaught
  exception and `exit()` keep the graceful shutdown with cancellation. Rejected: running `finally`
  blocks after a fatal error (the core's `fatal-error-with-multiple-fibers.phpt`, an OOM repeats),
  and a cancellation that rethrows at every later suspend (Edmond: "очень очень плохая идея").
  TrueAsync runs the queue on in the shutdown phase: a departure from P1.4.
- 2026-10-05 An exception nobody observed, of a coroutine alive at the request's end, is printed as
  uncaught (Edmond: "да печатать", over TrueAsync's silence), once per object, even after a fatal
  error or an `exit()` in a shutdown function. Released without a frame, it goes into
  `ASYNC_G(unobserved_exceptions)` (by handle); the last from_main call adds the rest by a walk of
  the object store and prints each in `zend_try` (a throwing `__toString()` bails out), with
  `set_exception_handler()`'s handler set aside, as for the exit exception, which stays in the
  table as printed. Nothing is cancelled; exit status 255; `getException()` observes. Why: the
  error vanished with exit status 0 (`scheduler/088`-`092`). Rejected: chaining into the exit
  exception (`zend_exception_set_previous` drops one already in the chain; waiters share one
  object); a table of the coroutines (removal at every observer and at `free_obj`).
- 2026-10-05 Main is re-minted at every from_main call, as before. Rejected: keeping main when
  nothing ran (efficiency report 2026-10-03): the call after the destructors re-mints it anyway
  (the core's destructors give main a switch handler), and a shutdown function awaiting the
  script's main deadlocked, or not, by whether a GC run or a `defer` came first (the Critic).
