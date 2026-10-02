# Handoff

Where the work stopped and what the next session needs. Replaced whole at every stop; the plan
(`dev/PLAN.md`) outranks this file when they differ.

Written 2026-10-02. Active step: **S3.2** (in progress), core fixes on `async-core`.

## State

- S3.1 closed (design agreed, D1-D38). No extension code for S3 yet.
- `async-core` in true-async/php-src: `996a9bd7047` "async: mark a coroutine started when its body
  begins" pushed on top of `9944e2d7b95`. It is section 10 items 1-4 of `dev/plans/S3.md` (D3):
  `ZEND_COROUTINE_F_STARTED` (bit 8), `zend_fibers.c` release cancels any unfinished coroutine and
  the failed-enqueue branch only releases, test_scheduler sets the bit (entry, adopted main),
  `ts_cancel` lost its `!IS_STARTED` term, the entry's BAILOUT branch became an assertion, and
  `ts_bailout_all` empties the run queue (a queued entry would outlive the bailout).
  New test `ext/test_scheduler/tests/069_is_started.phpt`: fails on `9944e2d7b95`, passes after.
- Per-test check, cloud container, ZTS debug `--disable-all --enable-test-scheduler`:
  `ext/test_scheduler/tests`, `Zend/tests/fibers`, `Zend/tests/gc` with the scheduler on
  (223 PASS, 30 FAIL, 1 SKIP, the 30 fails identical before and after) and off (253 PASS, 1 SKIP).
  The 30 fails with the scheduler on are the legacy fiber and gc tests, red at `9944e2d7b95` too.
- Item 2's case (a fiber dropped while its coroutine is queued and never ran) is not reachable from
  PHP on test_scheduler: its FIFO runs the fiber before anyone can drop it. No test; covered by
  reading. The test_scheduler entry's error branch keeps a graceful exit as the coroutine's
  exception (not reachable either).

## Also done: item 5 (D5)

`async-core` `5add2bcac22`: `zend_fibers.c` `Fiber::suspend` refuses only when `extended_data` is
NULL; `ts_suspend` parks a cancelled coroutine; `ts_cancel` ignores a cancel only for a RUNNING
coroutine or one with an error pending, so a later cancel is delivered again. `018_cancel.phpt`
changed on purpose by D5 (fails on the base, passes after); new `070` (a scheduler-cancelled fiber
suspends in `finally`; fails on the base). With the scheduler on,
`Zend/tests/fibers/suspend-in-force-close-fiber-after-shutdown.phpt` goes PASS to FAIL: the
FiberError is no longer raised, the behaviour D5 accepted (the extension excludes its copy,
`fiber/030`). Off: 255 PASS, 1 SKIP.

## Next

1. Items 6-14 of section 10 of `dev/plans/S3.md`, one commit and one test each, known-answer
   against the unchanged base build.
2. Core update by `WORKFLOW.md`: `async-core-io/<date>` with the new `async-core` merged, debug and
   ASAN, S1 suites diffed with `tools/results.py --diff`.

## How to run

- Core worktree in the cloud container: `/home/user/php-core` (branch `async-core`), base build for
  known-answer checks in the session scratchpad (`basecore`, detached at `9944e2d7b95`).
- Suites: `TEST_PHP_EXECUTABLE=sapi/cli/php TEST_PHP_JUNIT=out.xml sapi/cli/php run-tests.php -j4
  -d test_scheduler.enable=1 ext/test_scheduler/tests Zend/tests/fibers Zend/tests/gc`, and again
  without the `-d`.
