# Health

## How this project is checked

Maturity: young (no users besides its authors, no release)
Check day: Monday
Time budget: 15 minutes per run
Suite: `tools/test.py --lane pocs-dbg`, 1,691 tests on the CI coverage lane `pocs-dbg-cov` (1,664 PASS,
  16 SKIP, 11 XFAIL), run-tests 152.0 s, `ubuntu-24.04` runner (run 38081141533, 9bbd906, 2026-10-10);
  the dbg lane's Test step 2 min 31 s, asan 9 min 22 s, windows 2 min 57 s. `tools/test.py --show-slow 10`
  prints the ten slowest (the linux lanes pass it); locally after S14.2 and S14.4 (cloud container, 4 CPUs,
  `pocs-dbg`, 2026-10-12): 1,687 PASS, 19 SKIP, 9 XFAIL, run-tests 40.7 s; slowest: stream/034 10.0 s,
  mysqli/009 3.0 s, collector/064 2.2 s, stream/027 2.0 s, stream/030 2.0 s (XFAIL, S8), signal/024 1.8 s,
  await/141 1.7 s, reactor/042 1.5 s, channel/044 1.5 s, reactor/036 1.3 s; `pocs-asan` before S14.4:
  run-tests 160.8 s, its slowest io/100 29.8 s (XFAIL, S8).
Coverage: CI lane `pocs-dbg-cov`, `coverage.info` in the `results-mutants-coverage` artifact;
  last: 94.3 % of `src/` lines (9,383 of 9,947), CI run 38081141533 at 9bbd906 (2026-10-10)
Mutants: Mull 0.34.1 (`tools/mull.py`); CI runs only the known-answer check; reference run S3.13
  (2026-10-03), the S3 stage diff from 6478f20: 196 mutants, 167 killed, 29 survived; later runs
  cover one stage's diff at its review (S4.6, S5.5, S6.8, S7.5, S9.7, S9.14, S9.20, S9.30, recorded
  in dev/PLAN.md), not a common population
Practices: mutation tool (known-answer on every push, stage diffs at stage reviews), fault tests (the
  `fail_at` sites enqueue, reserve, link in 12 tests; IO chaos C1-C3 by `--io-chaos`, S6.8; the core
  test scheduler's `fail_new_coroutine`/`fail_enqueue` in 8 tests), fuzz seeds (nightly `seeds` job
  only, with an `--io-chaos` entry since S14.2), benchmarks (bench/, `tools/bench.py`, by hand),
  dev/PRINCIPLES.md, CI (lists, linux dbg and asan, mutants-coverage, windows on push; seeds on schedule);
  missing: specification tests (dev/TESTING.md), the allocator-fault and network-chaos layers of
  dev/plans/S2.md section 4
Scope: the extension (`src/`, `tests/`, `tools/`, `dev/`) and our core diff on `async-core`
  against the php-src master it is based on
Slices: the whole project (young)
Rotation: 8, 9, 6, 10, 5; last run: all on 2026-10-02 (baseline); 8 and 9 on 2026-10-05; 6 and 10
  on 2026-10-12
Known dark places: none beyond the open findings

## Open findings

- 2 tests/channel/145: under `TRUE_ASYNC_SCHED=random:<seed>` the process ends with SIGSEGV (dbg seeds 4-6, 9, 11, 12
  of 12 on 2026-10-12; CI nightly since 2026-10-10) (S14.1)
- 2 tests/fiber/035: under a seed (7, 10) the cancelled body runs after the `exit()` and the debug allocator reports
  two leaks (a closure, an object) (S14.1)
- 2 tools/test.py: a seed run stops at the first output it cannot parse (`parsed 1690 tests, run-tests counted
  1691`: a stray `  thrown` glued before scheduler/082's result line), so the nightly dbg job ran 10 of its 100 seeds
  on 2026-10-04, 10-11 and 10-12; io/100 (XFAIL) passes when a seed cancels the reader before its `fread()`, and
  the passing XFAIL fails the seed; nobody watches the scheduled run (S14.2)
- 2 tests/stream/030 (XFAIL, S8): the UDP receive waits for run-tests' 60 s timeout and is retried, 120 s of the
  150 s suite on 2026-10-12 (S14.2)
- 2 own tests that order events by wall-clock delays: scope/164, 166, 168 (a member's `delay(50)` against
  `disposeAfterTimeout(10)`, the defect DECISIONS 2026-10-09 fixed in scope/170), scope/096, 104; channel/112,
  114, 116-118, 125, 126, 129; await/116, 119, 121; internal/066, 069; reactor/036, 043 (S14.3)
- 2 own tests whose assertion is a wall-clock bound: scope/099, 100, 102, 107; channel/120; await/125;
  io_provider/002, 015; reactor/006, 013, 018, 019, 021, 023-026, 028, 030, 038; "linear time" checked only by
  `max_execution_time`: scope/124, 125, await/141, signal/024 (one million Futures) (S14.3)
- 2 tests/gc/020, 023: `--XFAIL--` "(S8 candidate)" with no S8 step and no RFC-CHANGES entry that would lift it;
  tests/lists/S3.excluded:4, 7 exclude gc/018 and include/008 as `component:S6` after S6 closed; :13-18 exclude six
  common/ tests as `component:S9` with no S9 step naming them (S14.4)
- 2 collector/063, 064: the walk at the tight `memory_limit` is checked only through the final warning counts,
  which a walk that wrongly completes gives too (S14.3)
- 6 src/io_provider.c:125-131 (the drain with no owner, dev/SECURITY.md:230-235) and src/reactor.c:1266-1274 (a
  thread that fires a trigger without pause starves timers, SECURITY.md:198-201): incidents without a fault test
  (S14.5)
- 6 src/reactor.c:790-812, 1065-1126, 1253-1263, src/scheduler.c:1757-1762 (D16's timer), src/os_signal.c:533-555,
  656-682, src/timeout.c:183-189, src/scope.c:1593-1597, src/channel.c:326-331, 906-910: a failed IO queue creation,
  submit, wait or wake pair has no seam and no test; a failed wait spins the idle loop by reading (S14.5)
- 6 src/future.c:591-595, src/await.c:1304-1306, src/task_group.c:1338-1344, 1442-1444, 2044-2047,
  src/scope.c:1081-1087, src/channel.c:931-936: a refused coroutine stack (the `fiber.stack_size` seam reaches
  them) has no test; the drain's refusal leaves mapped children pending for ever by reading (S14.5)
- 6 core Zend/zend_gc.c:2082, 2091-2092, zend_fibers.c:908, 930, 935, 962, 1110, 1548, 1585, 815: no fault test
  for the enqueue or cancel of a coroutine that is not CREATED; the seam fires only on a CREATED coroutine (S14.6)
- 6 core Zend/zend_gc.c:733-740 with 2270-2273 (`fail_enqueue` with the root buffer full), 2293-2296, 2116-2117;
  a finish handler refused (handler id 0: zend_gc.c:2172, 2270; zend_fibers.c:1132-1133 stores it unchecked);
  zend_async_API.c:760-764 "The scheduler failed to launch": no test (S14.6)
- 8 src/exceptions.h:21, 32: `async_composite_exception_add_exception`'s `transfer` is always false, and it and `async_ce_composite_exception` are exported but used only in exceptions.c (S3.19)
- 8 src/true_async_API.h:132-136: `async_callbacks_add()` is called only by test_hooks.c (S3.19)
- 8 src/internal/circular_buffer.h:26, .c:58-97: `item_size` is generic while every buffer holds pointers; the `capacity == 0` branch of `count` serves only a test hook (S3.19)
- 8 src/scheduler.c:1196-1200: `scheduler_gc_new_coroutine` is a copy of `scheduler_new_coroutine`; the core falls back to new_coroutine when the slot is NULL (S3.19)
- 9 dev/plans/S3.md:318-319, handoff.md:134: say the core's `ZEND_ASYNC_DEACTIVATE` will clear the scheduler-context flag "next core update"; two updates later it still does not, and no step owns it (S3.20)
- 9 dev/plans/S3.md:621-623, DECISIONS.md:202-203: "the call that comes back is then a plain one"; since `async-core` `c43060ea12d` the shutdown function's catch makes a bailout call (S3.19)
- 9 dev/PRINCIPLES.md:46, 48: P3.2's gate names the `windows-latest` job; the job is `windows` on `windows-2025-vs2026` (S3.19)
- 10 dev/plans/S2.md:190: the scenarios layer (`.feature` ports of fuzzy-tests) has no owning plan step and no
  DECISIONS entry for its generator; its "From" column names S5, which closed without it (S14.4)
- fine 6 src/scheduler.c, async_coroutine_new: the registry holds a coroutine from its creation, so one whose enqueue fails or bails out on OOM stays CREATED until RSHUTDOWN releases it; deadlock counting skips it (S3.18; tests scheduler/086, 087)
- fine 8 src/true_async.c:58: `scheduler_registered` is a process-wide static for a process-wide fact (the core's slots are set once in MINIT)
- fine 8 php_true_async.h:37: `ASYNC_G(last_handler_id)` is the id source of the agreed design (S3.md 3.6), for finish and switch handlers; the alternative is a counter in every coroutine
- fine 8 core Zend/zend_gc.c: `GC_G(dtor_pending)` counts more than one outstanding iterator; the reason is at its definition
- fine 8 core Zend/zend_async_API.h, .c, zend_fibers.h, zend_objects_API.c: API with no caller (the cancel slot's `is_safely`, `gc_new_coroutine`, `call_on_main_stack`, `coroutine_from_object`, the state, class and context aliases, the VM-stack helpers, the objects-store forwarder): the RFC's API is kept for any provider (P1.5; S3.18 and S3.20 removed it, S3.21 restored it; S3.22 removed `extra_size`, `active_coroutine_count` and the object-less coroutine on Edmond's word)

## Journal

### 2026-10-12

Passes: 1-4, 6, 10 over the extension at 6c30875 and `async-core` 5610980dc (diff against master
d7f966e073b); the pinned `async-core-io-2026-10-09-1` 3aa1cd120f4 built and run for pass 3 and the seed probes.
Numbers: suite 1,691 tests on `pocs-dbg-cov`, run-tests 152.0 s, CI run 38081141533 at 9bbd906 (320 tests,
  2.55 s, same lane and runner, run 37325011015 on 2026-10-05); dbg Test step 2 min 31 s (6 s), asan 9 min 22 s
  (30 s); coverage 94.3 % of `src/` lines (9,383 of 9,947), same run (97.2 % of 2,178). Locally (cloud container,
  4 CPUs, `pocs-dbg`, 6c30875): 1,683 PASS, 19 SKIP, 11 XFAIL, run-tests 150.3 s; slowest: stream/030 60.1 s
  twice (XFAIL, a UDP receive that waits for run-tests' timeout, then its retry), stream/034 10.0 s, mysqli/009
  3.0 s, await/141 2.3 s, signal/024 2.2 s, stream/027 2.0 s, collector/064 1.7 s, reactor/042 1.5 s,
  channel/044 1.5 s, reactor/036 1.3 s. Without stream/030 the suite takes about 30 s. Mutants: no common
  population since S3.13 (pass 5).
Findings: 12 NEW (2: eight; 6: four, from the extension and the core; 10: none). Resolved: 2 core
  ext/test_scheduler/tests 027, 037, 038, 040-045 (S3.20: each has a `--DESCRIPTION--` with its upstream original
  and reason). Known: 6 core zend_gc.c and zend_fibers.c (lines moved, zend_fibers.c:1110 and 815 added); 10
  dev/plans/S2.md (now :190). Passes 8 and 9 did not run; their S3.19 and S3.20 lines wait for them.
  The two seed failures were reproduced in this container (`tools/test.py --lane pocs-dbg --seeds 12` on the two
  tests): channel/145 SIGSEGV on 6 of 12 seeds, fiber/035 leaks on 2 of 12.
Strategy (pass 4): no step reopened; S6.3 and S6.4 moved obligations to later steps in their `done:` lines at
  the close, as the 2026-10-02 proposal asked. The nightly CI run has been red every day since 2026-10-04
  (8 of 8: lists on 10-04, 10-05, 10-06, 10-08, 10-09; windows on 10-07, 10-09; seeds every day since 10-06),
  and no thread looked: WORKFLOW (ba07347) has a thread watch only the push run of its commit. 42 of 123
  commits since 0e06d08 name no step; most are core pins and test repairs, but e3b8137, d66d7f4, aa27d33,
  1292c1a and 287a2c5 change code with no step line, which the 2026-10-05 proposal asked for. PLAN "Open
  questions" holds 22 items waiting for Edmond (none on 2026-10-05).
  Proposals: whoever starts a step looks at main's last scheduled run as well; Edmond takes the open questions
  in one sitting, one at a time.
Looks bad but is fine:
- 0 XFAIL of 92 core tests; 11 of the 13 extension XFAILs name S8 or a DECISIONS entry (stream/001, 002, 017,
  018, exec/012), gc/020 and 023 are a finding; skips allowed per lane by list tags: pocs-win 119, pocs-asan 50, pocs-dbg 19, each with a reason.
- Mass loops scope/058, gc/025 (100 000 coroutines), gc/016-023, context/033, future/103, 112, scheduler/016,
  023, 071, 074, 076 test scale or fill a buffer, not a race (DECISIONS:1260, 1417, 1566-1584).
- Trailing `%A` cannot hide a crash or a leak: `tools/run-tests.patch` fails an output with Termsig, a sanitizer
  report or a leak report. Weak ported tests (channel/064, 065, fiber/023, 025, 027, 028, scope/005, 006, 009,
  010, 012, 019, 020, 043, spawn/015, exec/002, 006, 010, dns/002, 005, 008) keep the reference's bytes (P1.3,
  P2.2); an own test checks each case except fiber/023 and 028 (a `resume()` right after the cancel) and
  scope/043 (a second `setExceptionHandler()`), which go with S14.3.
- Fiber stack refusals in the scheduler, the enqueue, reserve and link sites, the refused provider install and
  the collector's memory ceiling have fault tests (scheduler/005-018, 056, 065, 081, 106, scope/123,
  internal/027-033, 040, 057, 058, 061, await/127, io_provider/008, collector/063, 064).
- Specification tests (pass 10): the S3.7 spec and the S9 notes already state behaviour as numbered items that
  own tests cite (wait/ 27 of 27; scope/ 15 of 186). If Edmond takes them up, given/when/then inside the
  phpt files under `tools/test.py` keeps one runner; the `.feature` port needs a generator and a DECISIONS entry.
Plan: S14.1-S14.6 added; Edmond approved them the same day, to be fixed at once, ahead of other work.
Next: 5, 8

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
