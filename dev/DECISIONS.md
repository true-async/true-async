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
- 2026-10-02 Every callback of a notify runs even after one throws (the fork stops at the first
  and disposes the rest uncalled). Why: the core's finish handler "fires exactly once"
  (`zend_async_API.h:83`). To be confirmed by Edmond.
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
  switched into, and the debug build aborted at RSHUTDOWN.
- 2026-10-02 `gc/013`, `gc/014` (S3.10) and `gc/022` (S3.7) carry `--XFAIL--` from S3.5: with the
  scheduler registered, the GC needs the await slot and `suspend()` (S3.md section 14). Until then
  an automatic collection over objects with `__destruct` does not end; nothing short of the
  parking `suspend()` and await fixes it. Why: Critic judged the reason real; the Sage found the
  hang and no S3.5 fix.
