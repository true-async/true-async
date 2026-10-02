# Handoff

Where the work stopped and what the next session needs. Replaced whole at every stop; the plan
(`dev/PLAN.md`) outranks this file when they differ.

Written 2026-10-02. Active step: **S3.2** (in progress), the core update after the fixes on `async-core`.

## State

- S3.1 closed (design agreed, D1-D38). No extension code for S3 yet.
- Section 10 of `dev/plans/S3.md`, all 14 items, is on `async-core` in true-async/php-src, head
  `2aee763aeed`, one commit per item (items 1-4 share `996a9bd7047`):

| Item | Commit | Test | Checked |
|---|---|---|---|
| 1-4 (D3) | `996a9bd7047` | `069_is_started` | fails on `9944e2d7b95` |
| 5 (D5) | `5add2bcac22` | `070`, `018_cancel` changed | fails on `996a9bd7047` |
| 6 EH_THROW window (D1) | `c625aa49b83` | `Zend/tests/fibers/error-handling-window-per-fiber` | fails on `5add2bcac22`; without the reset before the jump it fails with the scheduler off |
| 7 enqueue on FINISHED (D19) | `1d84327c5b9` | none | comment, and test_scheduler throws instead of a no-op; no core caller reaches it from PHP |
| 8 GC where switching is blocked (D13) | `38eadbb9ce9` | `071` | fails on `5add2bcac22` (GC ran inside the tick, threshold raised) |
| 9 GC pointers after a bailout | `4c39d6e45eb` | `072` | fails on its parent (shutdown function's GC returned 0) |
| 10 NULL exception after `shutdown` | `101407b6f00` | none | test_scheduler keeps the exception; with its `shutdown` slot clearing it by hand: segfault before, clean after |
| 11 coroutines cleared at deactivation | `c267f4642a5` | `073` | fails on `5add2bcac22` (`current()` in the final output handler) |
| 12 RFC comments (D36) | `2b64c1f1c09` | none | comments; test_scheduler's await slot returns false without an exception in scheduler context, userland `await()` keeps its Error |
| 13 Fiber methods in scheduler context (D38) | `5c47924e3b5` | `074` | fails on `5add2bcac22` (the fiber never ran, a stray GracefulExit) |
| 14 observer behind its flag | `2aee763aeed` | `ext/zend_test` observer tests | same per test before and after, both modes; all six `observer_fiber_0*` fail when the call is dropped |

- Per-test check, cloud container, ZTS debug `--disable-all --enable-test-scheduler
  --enable-zend-test`, `ext/test_scheduler/tests`, `Zend/tests/fibers`, `Zend/tests/gc`:
  scheduler on 231 PASS, 31 FAIL, 1 SKIP; off 262 PASS, 1 SKIP. The 31 fails are the same as at
  `5add2bcac22` (30 legacy fiber and gc tests, plus the force-close test D5 accepted). The new
  tests account for the PASS growth, and `--enable-zend-test` adds `gc/gh19543-00{1,2}` (PASS).

## Decisions taken in this run

- Item 8: a run that only started the GC coroutine (switching blocked, scheduler context, or a
  cancelled await) sets `GC_G(run_deferred)`, and `gc_possible_root_when_full` then skips
  `gc_adjust_threshold`. The GC checks `ZEND_ASYNC_IN_SCHEDULER_CONTEXT` itself instead of relying
  on the await slot's false.
- Item 9: `gc_reset` also clears `run_deferred`. The cross-request half has no test (CLI runs one
  request).
- Item 12: test_scheduler's `ts_coroutine_execute_data` still answers only for SUSPENDED (the
  comment allows a provider not to track QUEUED); reporting a QUEUED fiber's frame could let the GC
  collect a fiber that is about to run, so it was left as is.
- Item 13: the check is `zend_fiber_switch_blocked() || (ZEND_ASYNC_ON &&
  ZEND_ASYNC_IN_SCHEDULER_CONTEXT)`; `Fiber::suspend` in scheduler context still answers "Cannot
  suspend outside of a fiber", because that check comes first.

## Core update (2026-10-02)

- `async-core-io-2026-10-02` in true-async/php-src, head `8a29d63edcf`: `834811f2d88` + master
  `d7f966e073b` (clean) + `async-core` `2aee763aeed`. Merging `async-core` straight in gave two
  merge bases and false conflicts in 14 files; with master first, the rest resolve by side: hooks
  files take ours (`async-core` equals master there), `Zend/zend_gc.c` takes `async-core`. Checked
  file by file that each comes from the side that changed it.
- The slash name `async-core-io/<date>` is refused by git beside the `async-core-io` branch;
  `WORKFLOW.md` names it `async-core-io-<date>`.
- Built without ior: auto mode refused fetching and building libior/ior in the cloud container.
  Four trees (old/new x debug/ASAN, WORKFLOW's configure line minus `--with-ior`) in
  `/home/user/ta`, results in `/home/user/ta/res`. Per test: test_scheduler 61 -> 68 PASS on
  debug, 59 -> 66 PASS + 2 SKIP on ASAN, the difference is exactly the new tests 068-074; hooks 43
  PASS 18 SKIP and poll 74 PASS 3 SKIP, equal.
- Not done: the ior half (Ring tests, `IOR_BACKEND=threads`); `CORE_REF` in CI and "Pinned core"
  stay at `834811f2d88` until it is done or Edmond accepts the run without ior.

## Next

1. Close the ior half of the core update (Edmond decides how), then move `CORE_REF` and "Pinned
   core" to `8a29d63edcf` and check CI green.
2. S3.3.

## How to run

- Core clone in the cloud container: `/home/user/php-src` on `async-core`; the base build for
  known-answer checks is a worktree at `5add2bcac22` in the session scratchpad.
- Suites: `TEST_PHP_EXECUTABLE=sapi/cli/php TEST_PHP_JUNIT=out.xml sapi/cli/php run-tests.php -j4
  -d test_scheduler.enable=1 ext/test_scheduler/tests Zend/tests/fibers Zend/tests/gc`, and again
  without the `-d`. `re2c` is not preinstalled: `apt-get install -y re2c`.
