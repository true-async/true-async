# Health

## How this project is checked

Maturity: young (no users besides its authors, no release)
Check day: Monday
Time budget: 15 minutes per run
Suite: `tools/test.py --lane pocs-dbg`, 169 tests (69 PASS, 100 XFAIL), run-tests 1.06 s on the CI
  `ubuntu-24.04` runner (run 37017719803, ece91a6, 2026-10-02); the asan lane's Test step 19 s
Coverage: CI lane `pocs-dbg-cov`, `coverage.info` in the `results-mutants-coverage` artifact;
  last: 96.1 % of `src/` lines (2,094 of 2,180), local `pocs-dbg-cov` at the S3.13 commit
  (2026-10-03); the uncovered lines and their reasons in dev/plans/S3.md section 14
Mutants: Mull 0.34.1 (`tools/mull.py`); CI runs only the known-answer check (4 of 4 killed, 4 of 4
  survived); reference run S3.13 (2026-10-03), the S3 stage diff from 6478f20: 196 mutants, 167
  killed, 29 survived (5 killed by tests after it, 24 explained in dev/plans/S3.md section 9)
Practices: mutation tool, fault tests (engine-limit bailouts, test hooks; injection points due S3.12),
  dev/PRINCIPLES.md, CI; missing: specification tests (dev/TESTING.md)
Scope: the extension (`src/`, `tests/`, `tools/`, `dev/`) and our core diff on `async-core`
  against the php-src master it is based on
Slices: the whole project (young)
Rotation: 8, 9, 6, 10, 5; last run: all on 2026-10-02 (baseline)
Known dark places: none beyond the open findings

## Open findings

- 2 core ext/test_scheduler/tests/034, 036, 055-059: expected output departs from the upstream originals with no reason comment (S3.18)
- 6 src/true_async.c:89-115, 169-176, 218-221: the refusals (extension off, scheduler refused, async off, scheduler context, no current coroutine) have no test (S3.16)
- 6 src/scheduler.c:469-476, 511-519: enqueue of a finished or running coroutine refused, no extension test (S3.16)
- 6 src/coroutine.c:299-304: finalize moves what waiters and finish handlers threw into the exit exception, no test (S3.16)
- 6 tools/test.py:393-396: a failed `lcov --summary` prints "unknown" and the coverage lane still passes (S3.16)
- 6 core Zend/zend_execute_API.c:282, zend_objects_API.c:111, zend_gc.c, zend_fibers.c:1079: enqueue and spawn failures have no test; the test scheduler has no fault seam but the API version (S3.18)
- 7 dev/INDEX.md: the `src/` files, the build files and six of the eight `tools/*.py` are not listed (S3.15)
- 7 src/internal/circular_buffer.c, allocator.c: functions only the test hooks call; an explicit count of `circular_buffer_realloc` nobody passes (S3.17)
- 8 php_true_async.h:35: module global `test_trace` where a field of `test_finish_t` would do (S3.15)
- 8 src/true_async.c:52-53: `async_ce_awaitable` and `async_ce_completable` exported, read in one file (S3.15)
- 8 src/scheduler.c:658-669: `scheduler_add/remove_finish_handler` only cast and forward (S3.15)
- 8 core Zend/zend_async_API.h:682: `active_coroutine_count` never written or read (S3.18)
- 8 core Zend/zend_async_API.h, .c: API surface with no caller and no RFC text: `call_on_main_stack`, `coroutine_from_object`, `ZEND_COROUTINE_F_OBJ_REF`, `ZEND_ASYNC_GET_EXCEPTION_CE`, `zend_async_is_enabled`, empty `internal_globals_dtor` (S3.18)
- 8 core Zend/zend_execute_API.c:243: `shutdown_destructors_iterator_entry` forwards to a function of the same signature (S3.18)
- 8 core Zend/zend_fibers.h:141-143: the comment says the coroutine owns the fiber; the code has the fiber own the coroutine (S3.18)
- 9 dev/plans/S3.md:302-304: says the fiber entry's catch restores the scheduler-context flag; DECISIONS 2026-10-02 and the code restore it when main is adopted (S3.15)
- 10 dev/plans/S2.md:187: the scenarios layer (`.feature` ports of fuzzy-tests) has no owning plan step and no DECISIONS entry for its generator
- fine 6 src/scheduler.c:502-503: the registry insert precedes a push that can bail out on OOM; the leftover is the case RSHUTDOWN names for S3.10's bailout drain
- fine 8 src/true_async.c:57: `scheduler_registered` is a process-wide static for a process-wide fact (the core's slots are set once in MINIT)
- fine 8 src/true_async_API.c:203-205: `ASYNC_G(last_finish_handler_id)` is the id source of the agreed design (S3.md 3.6); the alternative is a counter in every coroutine
- fine 8 core Zend/zend_gc.c: `GC_G(dtor_pending)` counts more than one outstanding iterator; the reason is at its definition

## Journal

### 2026-10-02

Baseline. Passes: 1-10 over the extension at ece91a6; 2, 6, 8 also over `async-core` 565f515df16
(diff against master d7f966e073b); `async-core-io-2026-10-02-2` 82df2fc6ccc checked merge-only
(`git log --no-merges` against `async-core`, master and the hooks head: empty).
Numbers: suite 169 tests, run-tests 1.06 s, CI run 37017719803 at ece91a6 (no earlier measure);
  coverage 88.9 % of `src/` lines (1,165 of 1,311), same run; mutants: no reference run.
Findings: the open findings above, all NEW. Disputed ones were settled by the Sage.
Strategy (pass 4): S3.5's done-when moved two obligations on after the step started
  (`scheduler/003` to S3.6, "a target destroyed with records linked" to S3.7), both written into the
  later steps; win was not run before the close and passed after it (run 37017719803). The notify
  cleanup 10cac41 belongs to no plan step. Proposal: a step that moves an obligation says so in its
  `done:` line in the same commit, as S3.5 did; no plan edit beyond that.
Looks bad but is fine:
- 100 XFAIL tests: each names an open step S3.6-S3.10, and the runner fails a lane when one passes.
- 13 tests skip on asan (`USE_ZEND_ALLOC=0` turns `memory_limit` off); they run on dbg and win.
- Weak ported tests (spawn/014, coroutine/004, 007, 011, spawn/013) keep the reference's
  expectations by P1.3 and P2.2.
- `zend_try` in coroutine.c and scheduler.c runs once per coroutine or context, not per switch.
- Stub slots and "not implemented yet" methods belong to S3.6-S3.9, as their comments say.
- The core never calls `zend_fiber_switch_block()` in new code and adds no global that is written
  but never read; it uses the scheduler-context flag.
Plan: S3.15-S3.18 added after S3.5.
Edmond's answers the same day: the notify stops at the first throw, as TrueAsync; the parked
  fiber stays uncollected, recorded in the RFC and the tests (S3.18); principle P1.4 with its gate.
  Applied with the notify change; the two pass 9 findings they settle are closed.
Next: 8, 9
