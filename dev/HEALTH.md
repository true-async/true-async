# Health

## How this project is checked

Maturity: young (no users besides its authors, no release)
Check day: Monday
Time budget: 15 minutes per run
Suite: `tools/test.py --lane pocs-dbg`, 320 tests (320 PASS, 0 XFAIL); run-tests 2.55 s on the CI
  coverage lane `pocs-dbg-cov`, `ubuntu-24.04` runner (run 37325011015, 0e06d08, 2026-10-05); the
  dbg lane's Test step 6 s, the asan lane's 30 s. The runner keeps no per-test times.
Coverage: CI lane `pocs-dbg-cov`, `coverage.info` in the `results-mutants-coverage` artifact;
  last: 97.2 % of `src/` lines (2,116 of 2,178), CI run 37325011015 at 0e06d08 (2026-10-05);
  the uncovered lines and their reasons in dev/plans/S3.md section 14
Mutants: Mull 0.34.1 (`tools/mull.py`); CI runs only the known-answer check (4 of 4 killed, 4 of 4
  survived); reference run S3.13 (2026-10-03), the S3 stage diff from 6478f20: 196 mutants, 167
  killed, 29 survived (5 killed by tests after it, 24 explained in dev/plans/S3.md section 9)
Practices: mutation tool, fault tests (extension fault sites U1-U6, S3.12; the test scheduler's INI
  fault seam `fail_new_coroutine`/`fail_enqueue`, S3.18), fuzz seeds in CI, dev/PRINCIPLES.md, CI;
  missing: specification tests (dev/TESTING.md)
Scope: the extension (`src/`, `tests/`, `tools/`, `dev/`) and our core diff on `async-core`
  against the php-src master it is based on
Slices: the whole project (young)
Rotation: 8, 9, 6, 10, 5; last run: all on 2026-10-02 (baseline); 8 and 9 on 2026-10-05
Known dark places: none beyond the open findings

## Open findings

- 2 core ext/test_scheduler/tests/027, 037, 038, 040, 041, 042-045: expected output departs from the upstream originals (destructors_002, oss-fuzz-471533782-001/002, suspend-in-force-close-fiber-catching-exception, throw-during-fiber-destruct, unfinished-fiber-*) with no reason in the file; the comments of 042-045 ("executing finally block") and the title of 040 contradict their own expected output (S3.20)
- 6 core Zend/zend_gc.c:2079-2080, and the enqueue of a SUSPENDED coroutine (zend_gc.c:2070, zend_fibers.c:909, 931, 936, 963, 1516, 1553): no fault test; the seam fires only on a CREATED coroutine (pass 6 next)
- 8 src/exceptions.h:21, 32: `async_composite_exception_add_exception`'s `transfer` is always false, and it and `async_ce_composite_exception` are exported but used only in exceptions.c (S3.19)
- 8 src/true_async_API.h:132-136: `async_callbacks_add()` is called only by test_hooks.c (S3.19)
- 8 src/internal/circular_buffer.h:26, .c:58-97: `item_size` is generic while every buffer holds pointers; the `capacity == 0` branch of `count` serves only a test hook (S3.19)
- 8 src/scheduler.c:1196-1200: `scheduler_gc_new_coroutine` is a copy of `scheduler_new_coroutine`; the core falls back to new_coroutine when the slot is NULL (S3.19)
- 9 dev/plans/S3.md:318-319, handoff.md:134: say the core's `ZEND_ASYNC_DEACTIVATE` will clear the scheduler-context flag "next core update"; two updates later it still does not, and no step owns it (S3.20)
- 9 dev/plans/S3.md:621-623, DECISIONS.md:202-203: "the call that comes back is then a plain one"; since `async-core` `c43060ea12d` the shutdown function's catch makes a bailout call (S3.19)
- 9 dev/PRINCIPLES.md:46, 48: P3.2's gate names the `windows-latest` job; the job is `windows` on `windows-2025-vs2026` (S3.19)
- 10 dev/plans/S2.md:187: the scenarios layer (`.feature` ports of fuzzy-tests) has no owning plan step and no DECISIONS entry for its generator
- fine 6 src/scheduler.c, async_coroutine_new: the registry holds a coroutine from its creation, so one whose enqueue fails or bails out on OOM stays CREATED until RSHUTDOWN releases it; deadlock counting skips it (S3.18; tests scheduler/086, 087)
- fine 8 src/true_async.c:58: `scheduler_registered` is a process-wide static for a process-wide fact (the core's slots are set once in MINIT)
- fine 8 php_true_async.h:37: `ASYNC_G(last_handler_id)` is the id source of the agreed design (S3.md 3.6), for finish and switch handlers; the alternative is a counter in every coroutine
- fine 8 core Zend/zend_gc.c: `GC_G(dtor_pending)` counts more than one outstanding iterator; the reason is at its definition
- fine 8 core Zend/zend_async_API.h, .c, zend_fibers.h, zend_objects_API.c: API with no caller (the cancel slot's `is_safely`, `gc_new_coroutine`, `call_on_main_stack`, `coroutine_from_object`, the state, class and context aliases, the VM-stack helpers, the objects-store forwarder): the RFC's API is kept for any provider (P1.5; S3.18 and S3.20 removed it, S3.21 restored it; S3.22 removed `extra_size`, `active_coroutine_count` and the object-less coroutine on Edmond's word)

## Journal

### 2026-10-05

Passes: 1-4, 8, 9 over the extension at 0e06d08 and `async-core` ab94befe389 (diff against master
d7f966e073b); the pinned `async-core-io-2026-10-05` a9de8425106 checked merge-only (empty). The
open lines of passes 6, 7 and 10 were rechecked because their steps S3.15-S3.18 closed.
Numbers: suite 320 tests, 320 PASS, run-tests 2.55 s on `pocs-dbg-cov`, CI run 37325011015 at
  0e06d08 (169 tests, 69 PASS and 100 XFAIL, 1.06 s, same lane and runner, run 37017719803 on
  2026-10-02; the baseline's 1.06 s was this lane); dbg Test step 6 s (5 s), asan 30 s (19 s);
  coverage 97.2 % of `src/` lines (2,116 of 2,178), same run (96.1 % at S3.13, local). Ten slowest
  tests: not measured, the runner keeps no per-test times; at 2.6 s for the suite not worth a step.
  Mutants: no reference run since S3.13.
Findings: 17 NEW (2: one; 6: one, from the recheck; 8: ten; 9: five).
  Resolved: 2 (034, 036, 055-059 reasons), all five 6 lines, both 7 lines, all seven 8 lines, 9
  (S3.md flag restore). Still open: 10 dev/plans/S2.md:187.
Strategy (pass 4): no step reopened; `done:` lines only re-wrapped. Main was red from 4693b88
  (2026-10-03 10:20, run 37116104933) to 2700fd6 (2026-10-05 12:56): five runs, the lists job's
  "Check the README roadmap"; nobody looked, since threads do not wait for CI. 2700fd6 and 7db8bdd
  are code with tests after every S3 step closed, from "Open questions", with no step line.
  aeb7424 and 0e06d08 shorten DECISIONS entries in commits of their own after the push.
  Proposals: a thread looks at main's last CI run when it starts a step; an open question that
  turns into code gets a step line in the same commit; the Critic checks the DECISIONS entry's
  length before the push.
Looks bad but is fine:
- 0 XFAIL of 320 extension and 86 core tests; 14 tests skip on asan (bailout/001-009, 011, 012,
  scheduler/006, 008, 011), all `memory_limit`; module/004 skips only on win (P3.2).
- Weak ported tests coroutine/008, 010, edge_cases/014 keep the reference's bytes (P1.3, P2.2);
  scheduler/012, 025, 035, 042 and wait/019, 020 check the same behaviour fully.
- run-tests retries a test whose output says "deadlock"; a pass on retry is WARN, which test.py
  counts as unexpected, so a retry cannot hide a failure.
- `current_coroutine()` with async on and no current coroutine (true_async.c:368-371) has no test:
  unreachable by S3.md:1573-1577.
- DECISIONS.md:485-494 (no queued coroutine after a fatal error) departs from TrueAsync and names
  P1.4 with Edmond's word; :305-311 says :495 replaced it.
- `zend_try` at scheduler.c:869 and 1093 runs once per bailout or request end, not on a hot path.
- Stubs for later stages: `ASYNC_COROUTINE_F_EXCEPTION_HANDLED` (S5), `scope` NULL (D11), the
  callbacks' unread arguments (TrueAsync's shape, S3.md 3), the core context stores (RFC text).
Plan: S3.19 (extension) and S3.20 (core) added; Edmond approved both the same day. P1.4: Edmond,
  2026-10-05, "if the code got better and is correct, that is enough"; its Flips field says so and
  the departures line is closed.
Next: 6, 10

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
