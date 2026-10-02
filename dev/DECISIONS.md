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
  applies; Critic to accept.
