# PLAN — TrueAsync rebuilt as a regular PHP extension

Updated: 2026-10-03 · Active: S3.18

Destination: `true-async/true-async`, an ordinary PHP extension written from scratch, with no
php-src patches of its own. It stands on the scheduler RFC (php/php-src#22561) and bukka's IO
RFCs (IO hooks, Poll API additions, Ring: php/php-src#23997). Until they are merged, they live in
the core branch `async-core-io`; every change we need in them is a one-topic commit there and an
entry in `RFC-CHANGES.md`.

## Principles and decisions

The eleven principles Edmond set on 2026-10-01 are in `dev/PRINCIPLES.md`, the decisions with
their reasons in `dev/DECISIONS.md`. The review of the IO hooks design that items B1-B3 and
M1-M13 below refer to is `dev/reviews/io-hooks-design-review.md`.

## Repository layout (agreed)

```
true-async/
├── config.m4, config.w32, php_true_async.h
├── src/            flat: true_async.c, scheduler.c, coroutine.c, exceptions.c, reactor.c,
│                   future.c, await.c, io_provider.c, collector.c, chaos.c (+ .h, .stub.php)
├── tests/          <group>/*.phpt; lists/ (frozen per stage), deps/ (core dependencies)
├── fuzz/           seed runs, chaos, scenarios
├── tools/          core build (async-core-io + ior), runner, coverage, Mull config
├── .github/workflows/
├── dev/            INDEX, WORKFLOW, PLAN, PRINCIPLES, DECISIONS, RFC-CHANGES, reviews/
└── README.md, CHANGELOG.md, LICENSE, .clang-format, .gitignore
```

## Fog

- Multi-thread ZTS (thread pool): needs a loop per thread on Poll/Ring; `SignalHandle` is CLI-only
  under ZTS (gist:2836-2845).
- An exported C API for other extensions (TrueAsync Server and others): the fork's extended
  `zend_async_API` (events, wakers, `resume_when`) may move into the extension. Not decided; S3
  keeps internal structures open to it, the first version does not promise it.
- Stream concurrency: the IO hooks freeze a whole stream (review B1); today's TrueAsync allows
  duplex and close-from-another-coroutine.

## S1 — Core branch `async-core-io`  [x] (S1.5 deferred)

Goal: the php-src the extension is built on: master + the two PoCs + ior, nothing else.
Done when: in `~/php-src2`, ZTS debug and ZTS ASAN builds (`--with-ior`, `--enable-test-scheduler`,
curl, openssl, sockets, pcntl, mysqli, pdo_mysql) give the same per-test results (PASS, FAIL and
SKIP counted apart) on `ext/test_scheduler/tests` (with `test_scheduler.enable=1`),
`ext/standard/tests/streams/hooks` and `ext/standard/tests/poll` as each PoC alone with the same
configure line; differences listed with a reason; a non-zero number of Ring and test_scheduler
tests executed (SKIP is not a pass); the hooks suite run twice, with `IOR_BACKEND` unset and set
to `threads`.
Tier: T1.

- [x] S1.1 `~/php-src2` as a git worktree of `/home/edmond/php-src`; ior `2fb12e8ce01` built into
      two prefixes with cmake (as the PR's `build-ior` action does): Release, and with
      `-fsanitize=address` for the ASAN tree.
      handoff: worktree on `async-core-io` at `14af3cb2f40`, no upstream. ior sources in
      `~/ior-src`, prefixes `~/ior` and `~/ior-asan` (`IOR_ENABLE_ASAN=ON`); both carry the
      io_uring and thread backends, which needs `liburing-dev` (2.5, installed via apt as the PR CI does).
- [x] S1.2 Baselines of `14af3cb2f40` and `056d9f803a3` with the same configure line.
      handoff: four trees in `~/ta-base/{sched,hooks}-{dbg,asan}`, per-test results in
      `~/ta-base/baseline-results`. Debug green on both. ASAN: two UAFs in the scheduler PoC (41 of
      61 `test_scheduler` red), fixed on `async-core` (`45c834ad383`, pushed); `async-core-io` must
      take that head in S1.3. Thread-backend hooks under ASAN (`ring-fork`,
      `ring-destroy-signal`, `ring-orphan-outputs`): cause found by PHP3, the sanitizer itself:
      any fork while a second thread runs leaves that thread "running" in the child's ASAN
      registry, and LeakSanitizer prints "Running thread N was not suspended" (reproduced in
      plain C, gcc 13 and clang 18; same on bukka's `b05a2fd63e5`). Details and the open choice
      of where to handle it: `dev/plans/S1-lsan-fork.md`.
- [x] S1.3 `async-core-io` from `async-core`; merge `origin/master` (conflicts in
      `zend_fibers.c`, `config.w32`), then `056d9f803a3`; build; diff against S1.2.
      handoff: merged 2026-10-01: `async-core` `6111c75b90c` (ff), `origin/master`
      `940ff2098ea` (`177e3f8c8c3`), then `056d9f803a3` (clean). `config.w32`: master's list
      (`zend_atomic.c` removed by #23927) plus `zend_async_API.c`. `zend_fibers.c`: async-core's
      side; its `zend_fiber_vm_stack_start` already covers GH-23921, checked by master's
      `gh23921.phpt` and `silence-operator-…phpt`. ASAN tree: `~/ta-base/coreio-asan`.
      The merge exposed `gh19983.phpt` (scheduler on, ASAN, 10/10 red): the loop allocated its
      VM stack before seeing the bailout flag, a second OOM; fixed on `async-core` `5d5fe3520bb`,
      merged (`834811f2d88`). Result: every suite equals S1.2 per test, plus master's two new
      fiber tests passing; the thread-backend hooks under ASAN keep the S1.2 open item.
- [ ] S1.5 Windows: `async-core-io` built with nmake (Debug_TS) and ior for IOCP (as the PR's
      `build-ior-windows` action does); the three suites run; per-test diff against Linux.
      Deferred (Edmond, 2026-10-01): no Windows agent yet. When one exists, this session writes
      `tools/windows/` (README, build-ior.ps1 with Debug `/MDd`, build-core.bat, run-suites.bat;
      structure agreed) and the agent runs it.
- [x] S1.4 Rules into the extension repo's `dev/`: a new PoC version gets a new branch
      `async-core-io/<sha>` with our commits cherry-picked (no history rewrite); RFC changes are
      one topic per commit; the PoC SHAs pinned in `dev/`; first entry of `RFC-CHANGES.md`: a
      `ZEND_ASYNC_API_VERSION` macro. Done when the rules are in `dev/WORKFLOW.md`.
      handoff: rules in `WORKFLOW.md` ("Branches", "Pinned core"). Changed against the first
      wording: `async-core-io` is merge-only (no commits of ours to cherry-pick), so an update is
      a new branch with merges; `ZEND_ASYNC_API_VERSION` went to `async-core` (`9944e2d7b95`,
      date format as `PDO_DRIVER_API`), not to `RFC-CHANGES.md`, since the scheduler RFC is ours.

## S2 — Repository and test system  [done]

Goal: `true-async/true-async` (clone in `~/true-async`) builds against `~/php-src2`, and the
test system every later stage plugs into exists.
Done when: the extension (module `true_async`, `phpize` against an installed core on Linux, a
copy into `ext/true_async` on Windows, INI switch `true_async.enable`) builds; CI on push builds
the published core trees and runs the `pocs-dbg` and `pocs-asan` lanes (and `rfc` once it
exists) on Linux, and a Windows job; a registration test passes; Mull's known-answer check holds
both ways: mutants of a planted tested function are killed, mutants of a planted untested
function survive or come out NotCovered.
Tier: T2. Roles: Critic on the design (S2.1).
Notes: dev/plans/S2.md (build, runner, lists, layers, CI, Mull)
Base: 82fd6d6

- [x] S2.1 Design note of the test system and of the build against the core.
      handoff: `dev/plans/S2.md` (Critic's ten findings applied, Edmond's answers in section 7).
- [x] S2.2 Skeleton, core prefixes, S1 suites rediffed.
      handoff: cores in `~/ta-prefix/pocs-{dbg,asan}`, S1 suites equal per test (14 of 14).
- [x] S2.3 Runner, list checker, registration test.
      handoff: `tools/test.py`, `tools/check-lists.py`, `tests/lists/S2.txt` (2 own tests).
- [x] S2.4 CI: `async-core-io` published in true-async/php-src; cached core prefixes; push and
      nightly workflows; the Windows job.
      handoff: `.github/workflows/ci.yml`, run 36896278925 green on 2026-10-01: lists, `pocs-dbg`,
      `pocs-asan`, Windows (`windows-2025-vs2026`, the extension built shared in ext/, 1 PASS),
      mutants-coverage. Nightly is the same workflow on a 02:30 schedule. The first Windows run
      found `php_true_async.h` missing from the include path (fixed in `config.w32`).
- [x] S2.5 Mutation: Mull for clang 18 on the stage diff, known-answer check both ways, time of
      one mutant run measured; hand mutants by rule 28.2 as the fallback.
      handoff: `tools/mull.py --known-answer`: 4 of 4 killed, 4 of 4 survived, locally and in CI;
      fails when the test calls both functions. `--diff-ref` filters by our own `git diff -U0`:
      Mull's `gitDiffRef` makes no mutant in a new file. Took `tools/run-tests.patch` (environment
      from `/proc/self/environ`, tests through bash) for mutants to switch on at all; facts in the
      note, section 6. The coverage lane (`pocs-dbg-cov`, 14 of 14 `src/` lines) came with it:
      the layer table had it from S2 and no step owned it. Hand mutants not needed.
- [x] S2.6 Actions on Node 24 in every project repository (GitHub forces Node 20 actions onto
      Node 24 and warns).
      handoff: true-async `ci.yml` upload-artifact v4 to v7 (`de2e015`, run 37001631753 green, no
      annotations); the site's `deploy.yml` checkout and setup-node v7, upload-pages-artifact and
      deploy-pages v5 (`c5d30eb`, deployed, no Node 20 warning). true-async-doc,
      php-async-core-rfc and claude-skills have no workflows; true-async/php-src carries only
      upstream's workflows, and every job of theirs in the fork is skipped (no warning to fix).

Test layers: `dev/plans/S2.md`, section 4.

## S3 — Scheduler on the scheduler API  [ ]

Goal: coroutines, the run queue and the request lifecycle, no reactor yet.
Done when: the S3 list passes on debug and ASAN and on both core trees (tests gated on an RFC
change SKIP on PoCs-only with that reason); scheduler fuzz runs the list over 100 seeds clean;
every wait-model line covered on `pocs-dbg-cov` except the listed out-of-memory branches; the
fault-injection points (`--enable-true-async-test-hooks`) drive a bailout through every unlink site;
Mull survivors in the wait and scheduler code killed by a test or explained (D34).
Tier: T2. Roles: Critic on S3.1 and on the steps marked below, stage review (S3.13), security
pass (S3.14).
Trees: "both core trees" is PoCs only in S3: the `rfc` tree exists once a change to bukka's RFCs is
needed (`dev/plans/S2.md`, section 5), and S3 needs none (scheduler RFC changes go to `async-core`).
Test ownership: each listed test belongs to the step named in its `--XFAIL--` section (`dev/plans/S3.md`,
section 14); a step that finds a test needs more moves it on with a note.

- [x] S3.1 Design note: coroutine object and methods (classes from TrueAsync), run queue, idle point before the deadlock
      and end-of-main decisions, cancellation, end-of-main and bailout policy, GC destructor
      coroutine, `zend_fiber_switch_blocked()` honoured in suspend (withdrawn 2026-10-02), EH_THROW window saved per
      coroutine, the wait-graph edge every wait registers. Frozen list with one exclusion reason per excluded test (needs component X,
      needs a core change, needs a fixture, platform), and the core-dependency table (extension
      fix, RFC change, upstream fix, drop).
      Notes: dev/plans/S3.md.
      Critic 2026-10-01: execution model self-contradictory (embedded context vs pool vs scheduler
        hop); the waker premise false (the reference allocates nothing per await); the core awaits
        GC where parking is impossible; TrueAsync states not readable from RFC status;
        F_CANCELLED terminal in the core; lifecycle needs the three from_main calls; fiber
        exemption from deadlock; positional handles break; front-of-queue rules. All accepted,
        note rewritten (version 2).
      Performance review 2026-10-01: measured the reference (release build, pre-1fdacf8 objects);
        corrected section 11; ranked improvements and benchmark method B0-B5 with instructions:u.
        Accepted.
      Design agreed 2026-10-02: Edmond's decisions D1-D38 (`dev/reviews/s3-structures/`), note
        rewritten (version 3), test list frozen at 133. Critic on version 3: self in the run queue,
        tick exceptions, Fiber methods inside the tick, ts.c for D5, thresholds; all fixed.
- [x] S3.2 Fixes on `async-core` that S3 needs, each with a test: `F_STARTED` (bit 8) and the fiber
      release and force-close checks; EH_THROW saved per switch; GC where switching is blocked
      starts its coroutine without waiting; GC's async pointers cleared on any finish and in
      `gc_reset`; no NULL dereference after `shutdown` in a fiber coroutine; current and main
      coroutine cleared at deactivation; Fiber methods refuse in scheduler context; the observer
      call on a switch guarded; `ts.c` reads CANCELLED as requested; RFC comments. Then a core update by `WORKFLOW.md`. List
      and reasons: `dev/plans/S3.md`, section 10.
      Progress 2026-10-02: items 1-4 (D3) in `async-core` `996a9bd7047`, test 069; item 5 (D5) in
        `5add2bcac22`, test 070, `018_cancel` changed by D5. Items 6-14 in `c625aa49b83`..`2aee763aeed`,
        one commit each; tests `Zend/tests/fibers/error-handling-window-per-fiber` (6), 071 (8),
        072 (9), 073 (11), 074 (13); each fails without its fix (072 on its parent, the rest on
        `5add2bcac22`); 7, 10, 12 and 14 have no phpt (reasons in `dev/handoff.md`).
      Core update 2026-10-02: `async-core-io-2026-10-02` (`8a29d63edcf`, true-async/php-src) =
        the current branch + master `d7f966e073b` + `async-core` `2aee763aeed`. Built without ior
        (auto mode refused building ior in the cloud container): S1 suites per test equal to
        `834811f2d88` on debug and ASAN, except the seven new test_scheduler tests 068-074, all
        PASS; 18 hooks and 3 poll tests SKIP in both (Ring and ior absent). Left: the ior half
        (Ring tests, `IOR_BACKEND=threads`) accepted without a run (Edmond, 2026-10-02); `CORE_REF` and
        "Pinned core" point at `8a29d63edcf`.
      handoff: CI green on `8a29d63edcf` (run 36995546671, 5ad8c0a: lists, `pocs-dbg`, `pocs-asan`,
        Windows, mutants-coverage). CI gates on lists up to `CLOSED_STAGE` (2); the S3 list runs in a
        step that reports without failing; with no S3 code it gives 127 FAIL, 8 PASS on dbg, on this
        core (run 36994985179) as on `834811f2d88` (run 36994693932).
      RFC text 2026-10-02: true-async/php-async-core-rfc `f1e64a8`, both `scheduler_rfc.md` and
        `.dokuwiki.txt`: the *started* attribute (item 1) and enqueue of a finished coroutine
        refused with an `Error` (item 7).
- [x] S3.3 Internal API: circular buffer, allocator, callbacks vector with the cursor rule, wait
      record and kinds, flat waker, finish handlers with ids, exception save and restore; strict
      pointer flags; the grep gates of S3.md section 11 in CI.
      done: own tests through test hooks pass: A B C D with B removing A runs each once;
        self-removal order A C B; a nested notify is refused; the positional-handle scenario of 3.6
      tier: T2 · role: Critic
      handoff: done 2026-10-02 (`7f1cba7`, `ee6e071` and the commit closing it): `tests/internal/001`-`017`
        pass on dbg (25 PASS, 127 XFAIL), valgrind clean; CI green on `7f1cba7` (run 37000266077,
        all lanes). Two Critic rounds: 8 findings on the API, 5 on the buffer port, all fixed;
        departures from the spec text in S3.md 3.6, "As built in S3.3". Obligations for S3.5: the
        finalize holds the coroutine's object across its notify; teardown with a linked record
        wakes the waiter (4.4); the bailout handling calls `async_callbacks_bailout_reset()`.
- [x] S3.4 Classes and test ports: exceptions, `Awaitable` (refuses foreign classes), `Completable`,
      the `Coroutine` object (296 B), INI; five of the seven `changed:` ports of S3.md section 9
      (`gc/005` and `gc/011` go to S3.7).
      done: `edge_cases/013` passes; own test for bug 10; `check-lists.py` clean with the tags
      tier: T2 · role: —
      handoff: done 2026-10-02 (`158e5c6`, `bc5b1fb`, `8196bd2`): 33 PASS, 127 XFAIL on dbg; CI green
        on all three (runs 37002982898, 37004144080, 37005572624). Critic: 7 findings, all fixed
        (GC walked the embedded `internal_context` as an array; `new Async\Coroutine` refused;
        `true_async.enable=On`); the seven test changes accepted. Notify callbacks run in
        scheduler context instead of `zend_fiber_switch_block()` (Edmond). Then, on Edmond's call,
        the notify cursor moved into the vector (coroutine 304 B); `async_callbacks_bailout_reset()`
        is gone, so S3.3's obligation to call it lapses.
- [x] S3.5 Spawn and run: the 21 slots, launch, run queue, enqueue (4.3), in-place run, call 1
      from main, RINIT and RSHUTDOWN, `spawn`, `current_coroutine`, `get_coroutines`, state methods;
      wait unlink stubbed.
      done: the S3.5 tests pass on dbg, asan and win; own tests for `asHiPriority` (D20, D35) and
        `isSuspended` of the running coroutine (D18)
      tier: T2 · role: Critic
      handoff: done 2026-10-02 on core `82df2fc6ccc`: dbg 69 PASS, 100 XFAIL; asan 58 PASS,
        13 SKIP, 98 XFAIL; win not run yet (CI after the push). Own tests
        `scheduler/001`-`009`; `003` (the one-shot front of `asHiPriority()`) needs a yield and
        moves to S3.6. Critic and Sage (Edmond asked for both) against TrueAsync: 1 core bug, a
        context freed before the core read its stack (`async-core` `565f515df16`, core branch
        `async-core-io-2026-10-02-2`; the asan lane shows the use-after-free without it; S1 suites
        equal per test on dbg; on asan all PASS but `039_oom_recursive_fiber` SKIP (no ZendMM) and
        `hooks/process-ops-proc-open` passing on run-tests' retry under 4 workers, 5 of 5 alone, not
        run on asan before), 10 extension findings fixed and 1 moved to S3.7; a second round found
        4 more (the drain and the callable release around a bailout), fixed (DECISIONS 2026-10-02). `gc/013`, `014`, `022` wait for await (XFAIL), 15 tests of
        later steps pass early (S3.md section 14); until S3.7 an automatic GC over objects with
        `__destruct` hangs (Sage).
- [x] S3.6 Suspend: `suspend()` by 4.2 with the tick (microtasks), yield, the context pool (D23),
      direct switches.
      done: the S3.6 tests pass, `scheduler/003` included; own test: a yield with nobody ahead
      tier: T2 · role: —
      handoff: done 2026-10-02 on core `82df2fc6ccc`: dbg 93 PASS, 81 XFAIL; asan 80 PASS, 14 SKIP, 80 XFAIL.
        `Async\suspend()`, the suspend slot (4.2 without the U5 `zend_try`: no records before S3.7,
        and TrueAsync's tick has none), the defer slot and the tick in suspend, in each context's
        loop and in the drain; the suspend location and `getTrace()`; a bailout while main is
        parked goes to main's stack (U4); a refusal inside an unadopted Fiber until S3.9. Own tests
        `scheduler/010`-`013`, `internal/019`. Critic and Sage: 4 bugs fixed (a drop with no bailout
        address, a dangling current coroutine in the tick, a yield inside a Fiber, a stale main entry),
        latent cases recorded in `dev/handoff.md`. Ten tests of later steps
        pass early, `bailout/012` goes back to S3.10 and `edge_cases/014` hangs until S3.8 (S3.md
        section 14); run-tests fails a test the timeout killed (DECISIONS 2026-10-02). The Sage's
        GC probe (12 000 cyclic objects with `__destruct`) ends now, in main and in a coroutine.
- [x] S3.6a Scheduler coroutine: TrueAsync's hybrid algorithm (Edmond, 2026-10-02): the scheduler's
      code runs between coroutines and in a scheduler coroutine on its own fiber; the drain on the OS
      stack, `drain_context()` and the bailout to main's stack go; `scheduler_bailout_all` (4.5)
      moves here from S3.10, in the core's ts.c order (main last).
      done: the S3.6 tests pass, `bailout/012` too; own tests: a microtask that throws after main is
        the exit exception (it had no frame on the OS stack), coroutines spawned by a shutdown
        function and a destructor run on a new scheduler coroutine, a destructor that throws in a
        finalize skips no coroutine, a stack that cannot be taken ends the request once, with a full
        GC buffer too
      tier: T2 · role: Critic, Sage
      handoff: done 2026-10-02 on core `82df2fc6ccc`: dbg 100 PASS, 80 XFAIL; asan 86 PASS, 14 SKIP,
        80 XFAIL. Analysis, Critic and Sage before the code:
        `/mnt/project-files/notes/hybrid-scheduler.md` (project files). A stack that cannot be taken
        ends the request (DECISIONS): `scheduler/007` expects the fatal of `005` now.
- [x] S3.7 Await and GC: the wait model (4.1, U1-U6, the debug asserts of 4.4), the await slot, the
      GC rules of section 7, awaiting info; the `changed:` ports of `gc/005` and `gc/011` (moved from
      S3.4: their output under the eager scheduler start is known only by running them).
      done: the S3.7 tests pass; own tests of layer 2 (two waiters, two wakes in one tick, a target
        destroyed with records linked and its waiter woken with an error (moved from S3.5), a
        script with 12 000 cyclic objects with `__destruct` ends (it hung in S3.5, ends since S3.6), a wait refused in scheduler context, GC while an exception
        unwinds, a waiter's record left behind a finish handler that throws (S3.md 4.6)); blind tests from section 4 by `test-author` pass
      tier: T2 · role: Critic
      handoff: done 2026-10-02 on core `82df2fc6ccc`: dbg 182 PASS, 36 XFAIL; asan 168 PASS, 14 SKIP, 36 XFAIL.
        `Async\await()`, the await slot (the GC waits for its run in main and in coroutines),
        `getAwaitingInfo()`. The record lives in the waker, so U5 and its `zend_try` go (Edmond
        agreed); the wait kind, `F_TYPED`, `F_COUNTED`, the waker's `wait` and count and
        `awaiting_info` go (coroutine 320 B). Blind tests `wait/001`-`027` from
        `dev/plans/S3.7-spec.md` pass the check; own tests `scheduler/019`-`027`, `internal/021`,
        `022`. Critic: 6 findings fixed (a wait from a destructor of a finished coroutine, the
        observed mark before a refusal, a debug assert on main, the teardown loop, the tick's flag
        order, unused fields); the GC tests `gc/002`, `007`, `011`, `012` and `scheduler/016` are
        `changed:` ports the Critic judged; S3.md section 14 lists the early passes.
- [x] S3.8 Cancellation and exit paths: `cancel`, `protect` (D7), unhandled exceptions as the exit
      exception, deadlock with its report, `graceful_shutdown`, `exit()` in a coroutine.
      done: the S3.8 tests pass; own tests: nested protect cancelled, cancel-before-run then await,
        `exit()` in a spawned coroutine, await from an output handler during the report
      tier: T2 · role: Critic
      handoff: done 2026-10-02 on core `82df2fc6ccc`: dbg 227 PASS, 7 XFAIL; asan 213 PASS, 14 SKIP, 7 XFAIL.
        The 29 S3.8 tests lost their `--XFAIL--`. The coroutine's values go in `dtor_obj`, as
        TrueAsync: a use-after-free since S3.5 (the Sage); it reverses the S3.5 entry "the arguments
        stay until the object dies" (a Sage and Critic call, not Edmond's). The outcome is observed
        when a waiter reads it, so a cancelled waiter no longer loses the target's exception. A
        deadlock cancels and reports instead of a fatal. Own tests `scheduler/028`-`043`; four own
        tests got new fixtures (`changed:`, Critic and Sage judged). Critic: 1 blocking and 7 more
        fixed (an exception of an unrun coroutine's release reached the next coroutine, scheduler
        context of that finalize, the observed mark at the wake, the cancel-all walk, one deadlock
        cancellation per coroutine, `exit()` after the shutdown, the `shutdown` slot, weak tests);
        Sage: no blocker, an `exit()` in the report's output handler fixed, two TrueAsync
        behaviours kept (DECISIONS).
- [x] S3.9 Fibers: `intercept_fiber` (always adopt), the deadlock exemption.
      done: the S3.9 tests pass; own tests: a cancelled fiber suspends in `finally` (D5), Fiber
        methods from a destructor in the tick, `exit()` in an adopted fiber
      tier: T2 · role: —
      handoff: done 2026-10-02 on core `82df2fc6ccc`: dbg 241 PASS; asan 227 PASS, 14 SKIP; no XFAIL
        left but S3.10's. Every Fiber runs as a coroutine; the unadopted-Fiber refusals went
        (`scheduler/013`, `027` changed). `extended_dispose` moved to `free_obj` (the finalize call
        leaked every fiber coroutine). Suspended fibers alone are closed with a graceful exit, no
        `DeadlockError`. The waker never chains an exit object. Own tests `scheduler/044`-`049`,
        `internal/023`. Critic: 1 blocking fixed (a fresh context inherited the switcher's
        `EG(active_fiber)`: wrong `Fiber::getCurrent()`, a freed Fiber after a park), two behaviours
        recorded with tests (fibers closed before the shutdown functions, as TrueAsync; `Fiber::start()`
        in a finished coroutine's release refused); Sage: agreed, moved that refusal into
        `intercept_fiber` (a refusal at the park ran the body twice).
- [x] S3.10 Shutdown windows and bailout: calls 2 and 3 from main, the bailout drain (4.5, D24),
      shutdown functions, destructors, output handlers.
      done: the S3.10 tests pass (bailout SKIP on asan); own test: two shutdown destructors, the
        first waiting on the second; no `--XFAIL--` left in the S3 list
      tier: T2 · role: Critic
      handoff: done 2026-10-03 on core `82df2fc6ccc`: dbg 249 PASS; asan 235 PASS, 14 SKIP; no
        `--XFAIL--` left. The 15 S3.10 tests passed early (S3.5-S3.7). The core's switch handlers
        run (leave before the tick, as TrueAsync), so a shutdown destructor that waits lets the
        pass go on in the core's new coroutine; it hung before. A bailout out of the bailout walk
        goes on with the walk (it was a bailout with no address); RSHUTDOWN releases what a
        bailout out of the last from_main call leaves (U6), where it asserted. Critic: a coroutine
        whose finish bailed out stayed in the registry and a later drain spun on it (fixed: it
        leaves as it finishes); weak tests and doc wording fixed. Sage: kept the walk's retry, the
        stack unmapping at RSHUTDOWN and the early registry delete (DECISIONS). Own tests `scheduler/050`-`054`,
        `internal/024`-`026`.
- [x] S3.11 Measurements of S3.md section 12: B0-B5 against the reference, known answer first;
      the open items of its table decided by their runs.
      done: numbers with date in `BENCHMARKS.md`; D2 holds, or each excess has a DECISIONS line
      tier: T2 · role: —
      handoff: done 2026-10-03 on core `82df2fc6ccc`, release builds, `dev/BENCHMARKS.md`: D2 holds on
        B1-B5, shared build against the static reference, 0.15-0.97 of its instructions per operation
        and no more allocations (cachegrind's count: the container has no hardware counters). Taken:
        O6 (`spawn_fcall`, -1 allocation per spawn; its unbatched wall time 5.8 % slower, recorded);
        the pool floor 1024 with TrueAsync's rule (D23 changed: B4 -21 % at depth 100, B5 a tenth of
        the wall time, about 20 KiB resident per pooled context). Found: every context took its first
        VM stack page (16 KiB) from the request's memory, TrueAsync puts it on the C stack; fixed,
        test `scheduler/055`. Kept: the inline vector element, D31. Critic: a small `fiber.stack_size`
        crashed with the page on the stack (TrueAsync too); the context now takes the page on top,
        test `scheduler/056`, the no-stack tests moved to a size mmap refuses; the pool buffer no
        longer allocated per request. Sage: pool 1024 and O6 kept. dbg 251 PASS; asan 237 PASS,
        14 SKIP, also with `detect_stack_use_after_return=1`.
- [x] S3.12 Fault injection and fuzz: hooks for U1-U6 (layer 3); the fuzz hook
      (`--enable-true-async-fuzz`, `TRUE_ASYNC_SCHED=random:<seed>`, TrueAsync's) and `test.py --seeds N`.
      done: one passing test per site U1-U6; 100 seeds over the S3 list on dbg and asan with no
        crash, assertion, sanitizer report or leak (output order differences do not count)
      tier: T2 · role: —
      handoff: done 2026-10-03 on core `82df2fc6ccc`: dbg 259 PASS; asan 245 PASS, 14 SKIP; seeds
        1-100 over all lists: dbg 0 failed, asan 0 failed. Fault sites `enqueue`, `reserve`, `link`
        armed by `TrueAsync\Test\fail_at()` (a fatal error, as out of memory); tests `internal/027`-`033`,
        where each record is unlinked checked under gdb (S3.md section 9). The fuzz hook is TrueAsync's
        swap before the pop. Seed 37 found a lost coroutine: a suspender woken in its own tick by its
        pop (an unrun coroutine's release starts the shutdown) switched away RUNNING and the deadlock
        resolution looped; fixed, `scheduler/057`. run-tests.patch now fails a crash, sanitizer or leak
        report a trailing `%A` used to take (known answer checked). Critic: U2's test never reached its
        site (fixed), crashes passing under `%A`, stray artifacts, no wall timeout; Sage: kept the fix
        and the hook as a plain call, dropped the order matcher for a new-diagnostic list, added the
        nightly `seeds` CI job (dbg 100, asan 20; not run yet).
- [x] S3.13 Stage review: Critic on the stage diff; coverage of every wait-model line except the
      out-of-memory branches listed in S3.md section 14; Mull on the stage diff, survivors killed
      or explained (D34); Code Reviewer.
      done: Critic's findings answered; coverage and survivors recorded with date
      tier: T2 · role: Critic, Code Reviewer
      handoff: done 2026-10-03 on core `82df2fc6ccc`: dbg 294 PASS; asan 280 PASS, 14 SKIP. Critic
        on the stage diff: the cancelling walks broke on a registry insert (ASAN use-after-free; a
        hash iterator now), no current coroutine in the tick after a body (the scheduler coroutine
        now), a coroutine woken with an error before it ran ran its body, a refused wake left its
        error in the waker; all fixed with tests. Found along the way: the stored error of an unrun
        coroutine corrupted parked main's opline (thrown now), its finalize awaited the GC with no
        context, and a wake with an error of the running coroutine left it queued after it finished
        (refused now, TrueAsync's rule). Second Critic, on the commit: the thrown error bailed out in
        shutdown destructors (stored there now), the refusal broke the GC's wake of a coroutine woken
        in its own pop (taken now), wrong "unreachable" claims (fixed, tests). The Sage: the refusal
        narrowed to outside scheduler context, the walks bounded to the coroutines present at their
        start, the suspend's error stored with no frame (tests); two leaks of a coroutine the core
        fails to enqueue recorded in the handoff for S3.18.
        Coverage 96.1 % of `src/` (2094 of 2180), every uncovered
        line listed with its reason in S3.md section 14; no out-of-memory branch left in the wait
        model. Mull 0.34.1 on the stage diff: 196 mutants, 167 killed, 29 survived; 5 then killed by
        tests, 24 explained in S3.md section 9. Code Reviewer: comments, hints and names fixed.
        Tests `scheduler/058`-`076`, `internal/034`-`047`, `classes/008`, `009`.
- [x] S3.14 Security pass over the stage diff by `dev/SECURITY.md`; findings fixed with a test or
      recorded with the reason; the open finding on the runner's secret filter closed.
      done: every checklist item has an outcome in `dev/SECURITY.md`
      tier: T2 · role: —
      handoff: done 2026-10-03 on core `82df2fc6ccc`: dbg 299 PASS; asan 285 PASS, 14 SKIP. Four
        reviewers by checklist item. Fixed with tests: `$this` of a class-string callable freed
        before `spawn()`'s coroutine ran (`spawn/021`), main freed in the queue after a bailout a
        shutdown function's zend_try caught (`internal/048`), a suspend's EH_THROW window and `@`
        covering other coroutines' code on its stack (`scheduler/077`, `078`), a by-reference result handed out as a
        reference (`await/096`). Fixed without a test: the Windows snapshot build compiled the test
        hooks in; run-tests gets an allowlist of variables (the open finding, checked with a planted
        secret); checkouts drop the token; Windows builds ior at `IOR_REF`; `--end-of-options` in
        the tools' git calls; the scheduler stack's room for its VM page. Open, with owners in
        `dev/SECURITY.md`: the stale current coroutine after such a bailout and the core Fiber's
        unreferenced callable cache (S3.18), the deadlock report's paths (S3.15, as Edmond answers).
        Critic on the commit: the Windows lane lost the hooks (`=yes` now), the `@` leak (fixed by
        the Sage's shape), weak tests strengthened, owners written into S3.15 and S3.18.
- [x] S3.15 Health fixes, extension (health check 2026-10-02, `dev/HEALTH.md`; S3.15-S3.18 run after S3.14, Edmond 2026-10-02): `test_trace` becomes a
      field of `test_finish_t`; `async_ce_awaitable` and `async_ce_completable` static; the finish
      handler functions take `zend_coroutine_t *` and sit in the slots, the two forwarders in
      `scheduler.c` go; S3.md "As built" on the scheduler-context flag follows DECISIONS 2026-10-02;
      `dev/INDEX.md` lists `src/`, the build files and every tool; the deadlock report's output
      (`true_async.debug_deadlock`, open finding of S3.14) as Edmond answers. Done ahead with Edmond's answers:
      the notify stops at the first throw, P1.4 and its gate, the P1.1 and P2.2 gate fields.
      done: the S3 list passes as before; `check-gates.py` and `check-lists.py` clean
      tier: T1 · role: —
      handoff: done 2026-10-03 on core `82df2fc6ccc`: dbg 299 PASS; asan 285 PASS, 14 SKIP, as at
        S3.14; both checks clean. The switch handler functions went into their slots the same way,
        with their two forwarders. A test hook's switch handler finds its trace beside its
        coroutine (`test_switch_coroutine_t`).
        The deadlock report's output waits for Edmond's answer (asked 2026-10-03) and moves to
        S3.16.
- [x] S3.16 Health tests: own tests for the refusals of `true_async.c` (extension off, async off, scheduler
      context, no current coroutine), enqueue of a finished or running coroutine, finalize moving
      what finish handlers threw into the exit exception; `tools/test.py` fails the coverage lane
      when `lcov --summary` gives no number; a test that main's adopt clears the scheduler-context
      flag a bailout out of a notify on the OS stack left set (no test fails without the reset, S3.15).
      Done ahead in S3.13: the enqueue refusals (`internal/039`, `043`, `scheduler/066`) and the
      finish handlers' exception (`internal/041`).
      done: the new tests pass on dbg, asan and win; those lines covered on `pocs-dbg-cov`
      tier: T1 · role: —
      handoff: done 2026-10-03 on core `82df2fc6ccc`: dbg 306 PASS; asan 292 PASS, 14 SKIP;
        `pocs-dbg-cov` 96.5 % of `src/` (2119 of 2196); win runs in CI after the push, `module/004`
        skips there (no `test_scheduler` loaded). Tests `module/003`, `004`, `scheduler/079`-`082`,
        `internal/049` (fails with the adopt's reset removed, checked). Left uncovered in
        `true_async.c`, with reasons in S3.md section 14: the failed function registration and
        `current_coroutine()` with no current coroutine. The coverage lane now fails on "no data
        found" too, which the old pattern took as a number. CI on 8fa582c failed in win and
        mutants-coverage: `getResult()` copied a result out of a `const` coroutine (`coroutine.c`
        482, an error under MSVC `/WX` and Mull's clang), fixed. Critic: `scheduler/065` and `081`
        expected the POSIX allocator's message, which Windows does not print (fixed, DECISIONS);
        line 275 is reached by a coroutine ended by `exit()` (`scheduler/082`). The deadlock
        report's output still waits for Edmond's answer and moves to S3.18. The Sage: `suspend()`
        checks only the scheduler context after its async-off return, as TrueAsync's `async.c:223-235`
        (`THROW_IF_ASYNC_OFF`, `THROW_IF_SCHEDULER_CONTEXT`).
- [x] S3.17 Circular buffer cut to what S3 uses (DECISIONS 2026-10-01): `circular_buffer_clean`,
      `_new`, `_destroy`, the explicit count of `circular_buffer_realloc` and the persistent allocator
      go; `circular_buffer_ctor` returns void; the `internal/012`-`017` hooks drive the production
      paths; a DECISIONS line names the changed internal tests.
      done: `internal/*` pass on dbg and asan
      tier: T2 · role: Critic
      handoff: done 2026-10-03 on core `82df2fc6ccc`: dbg 306 PASS; asan 292 PASS, 14 SKIP, as at
        S3.16; `check-lists.py`, `check-gates.py`, `format.sh --check` clean. `clean` stays: the
        bailout walk calls it since S3.6a, after the health check listed it. The Critic found the
        rest of the class (`pop`, `capacity`, the refusing push, the ctor's count, shrinking, which
        no queue reached, and the one-instance allocator); the Sage cut it too (DECISIONS). Growth
        is one branch; its condition and move are killed by `internal/012`, `014`, `016`, the swap's
        offsets by `015`, each mutant applied by hand (the short `erealloc2` on asan only). Critic
        on the commit: two growth corners untested (head at slot 0; push_front on a full buffer from
        tail 0), now in `012` and `016`; the pushes return void, as nothing can refuse them. The
        Sage: dtor's NULL check cut, `push_front` keeps the reference's `&item` shape.
- [ ] S3.18 Health fixes, core (`async-core`, one topic per commit, then a core update by `WORKFLOW.md`):
      `active_coroutine_count` removed; `shutdown_destructors` is the iterator's entry itself; the
      `zend_fibers.h` ownership comment corrected; uncalled surface removed (`call_on_main_stack`,
      `coroutine_from_object`, `ZEND_COROUTINE_F_OBJ_REF`, `ZEND_ASYNC_GET_EXCEPTION_CE`,
      `zend_async_is_enabled`, empty `internal_globals_dtor`) with the RFC text and
      `ZEND_ASYNC_API_VERSION`; tests 034, 036, 055-059 get the reason of their departure from
      upstream (a parked fiber is collected only at the scheduler's shutdown) and the RFC's
      "Backward Incompatible Changes" its fourth item (Edmond, 2026-10-02, DECISIONS); the finish
      handler contract in `zend_async_API.h` and the RFC reads "at most once: a handler that throws
      ends the notify", as the notify now does (DECISIONS 2026-10-02); a fault seam
      in the test scheduler for enqueue and spawn failures. On Edmond's word, the two core findings
      of S3.14 (`dev/SECURITY.md`, open findings): `ZEND_ASYNC_FCALL_DEFINE` keeps references for
      its callable's cache, and `main.c` ends the scheduler as a bailout after a shutdown function
      that bailed out, so no finished coroutine stays current. The extension's deadlock report output
      (`true_async.debug_deadlock`, open finding of S3.14) as Edmond answers, moved from S3.16; a default
      of off also changes `module/002-info`'s INI line.
      done: `ext/test_scheduler/tests` equal per test on dbg and ASAN; `CORE_REF` and "Pinned core"
        moved; each S3.14 core finding fixed with a test or closed by Edmond
      tier: T2 · role: Critic

## S4 — Reactor on Poll, Poll additions and Ring  [ ]

Goal: the scheduler's idle wait and timers on one per-thread `php_io_queue` (the Ring when built
with ior, the Poll queue otherwise), coded only against `php_io_queue_ops`; the S6 provider
submits to the same queue, so completion dispatch is designed here once for both.
Done when: S3 + S4 lists pass; `delay(1000)` costs under 50 ms of user CPU; a test-only C
function wakes the loop from another pthread through `NotifyHandle`.
Tier: T2. Roles: Critic on S4.1, Critic after S4.2.

- [ ] S4.1 Design note: completion dispatch for scheduler-owned ops and provider ops; idle wait
      in `queue->wait()` with its `EDEADLK` and `EINTR` answers; deadlock decided from the
      scheduler's own count of parked user waits (a wakeup op is always pending, review M5);
      `delay`/`timeout` as Timer ops; the Windows path (IOCP Ring for pipes, console, processes); cross-thread wakeup on an own eventfd/pipe (no C API for an
      owned `NotifyHandle`; an RFC change request); the queue rebuilt lazily after fork by pid;
      frozen list and core-dependency table.
- [ ] S4.2 Implementation; `Async\delay`.

## S5 — Futures, timeouts and combinators  [ ]

Goal: the API the ported tests use everywhere.
Done when: S3–S5 lists pass, including the `await` group's combinator tests.
Tier: T2. Roles: Critic on S5.1, Critic after S5.2.

- [ ] S5.1 Design note, frozen list, core-dependency table.
- [ ] S5.2 Minimal `Future`, `timeout()` with `TimeoutException`, the `await_*` family.

## S6 — IO hooks provider  [ ]

Goal: blocking PHP functions suspend the coroutine through `run()` on the reactor; signals and
children through the same contract.
Done when: S3–S6 lists (from `sleep`, `io`, `stream`, `socket_ext`, `dns`, `curl`, `exec`,
`mysqli`, `pdo_mysql` without the pool, `signal`) pass on debug and ASAN; IO chaos runs clean
over 100 seeds; tests that fail because of the hooks design are listed against the review item;
`dns` counted only on the Ring configuration (the Poll queue answers Unsupported for lookups).
Tier: T2. Roles: Critic on S6.1, Critic after S6.3.

- [ ] S6.1 Design note: install point per request and thread, readiness ops, deadlines, the
      suspend predicate, `zend_try` around the suspend (M12), Unsupported when async is off (M13),
      DNS and files on the Ring, SigWait on `SignalHandle` and WaitPid on `ProcessHandle` with
      the core's reaped-status table; signal ownership with threads decided (a live
      `SignalHandle` blocks the signal in its own thread only; until decided, `signal/*` is
      excluded with that reason); fixtures (MySQL with two connections, HTTP server with
      `PHP_CLI_SERVER_WORKERS`); frozen list; core-dependency table.
- [ ] S6.2 Pipe tests (`io` pipes, `proc_open`, STDIN) on Linux and Windows first, then Timer, Poll, Recv, Send, Accept, Connect, Any, WaitPid, SigWait, GetAddrInfo.
- [ ] S6.3 IO shutdown windows (the `ts_suspend` NULL case), run everything, record failures.

## S7 — Async object collector  [ ]

Goal: find coroutines that can never wake and the async objects only they keep alive: partial
deadlocks (a cycle of waits while other coroutines run), a Future nobody can complete, a channel
with no senders left; report them and resolve them by policy (DeadlockError into the waiters,
or a report only).
Done when: tests for each case pass on debug and ASAN; waits on IO, timers, signals and
cross-thread wakeups are never reported; a run over 10 000 parked coroutines costs a measured
time, recorded; scheduler fuzz over 100 seeds reports no false positives.
Tier: T2. Roles: Critic on S7.1, Critic after S7.2.

- [ ] S7.1 Design note: roots (runnable coroutines, pending external sources: provider ops,
      timers, signals, wakeups, main), edges (waiter → awaitable → completers), when it runs (on
      idle, on PHP GC, on demand), the policy per finding, interaction with PHP's own GC
      (suspended coroutines are GC roots through their stacks), the PHP API (report, setting).
- [ ] S7.2 Implementation and tests: mutual await, cycle of three, partial deadlock with
      other coroutines running, unreachable Future, channel without senders (after channels
      exist), IO and timer waiters not reported.

## S8 — Review checks and RFC change list  [ ]

Goal: the review's findings measured on a real provider; requests to both RFCs written.
Done when: B1, B2, B3, M1, M4, M10, M12, M13 each have an outcome (reproduced, not reproduced,
not expressible with why); `RFC-CHANGES.md` complete; the review updated.
Tier: T1.

## S9 — Higher layers, one at a time  [ ]

Scope, context, channels, task groups, pools, iterators: each its own plan, agreed with Edmond.

## S10 — Beyond the RFCs  [ ]

One decision per fork feature that needs core changes: PDO pool, per-coroutine output buffers,
pgsql, `Fiber::getCoroutine()`, `zend_sigaction` hook, thread pool, Windows. Each becomes an RFC
change, an upstream fix, an extension-level design, or "not in the first version".

## Design history

- Round 1 (v1, trial integration): porting a slice of `ext/async` is not possible, it needs the
  fork's core; Facts taken from docs were wrong; shutdown lifetime, Done-when, two providers.
- Round 2 (v2, thin core + shared provider): shutdown-function IO crashes the reference
  scheduler; all 21 slots; idle before deadlock decisions; libuv embedding spin; fixtures.
- Owner decision: Windows to the maximum from S1; pipes a mandatory test group.
- Owner decision after round 3: an async object collector that finds deadlocks (new S7).
- Owner decisions after round 2: full rebuild from scratch as a regular extension in
  `true-async/true-async`; core branch `async-core-io`; reactor on bukka's APIs, no libuv;
  fuzz, chaos, phpt and mutation tests.
- Round 3, with the Poll/Ring reactor applied: reactor and provider on one `php_io_queue`;
  deadlock from the scheduler's own count; signals under threads; no C API for `NotifyHandle`;
  DNS needs the Ring; Poll context unusable after fork; ior built with ASAN; SKIP is not a pass.
- Round 3 (v3): ported tests need API planned later (78 of 95 `await` tests use combinators) →
  new S5; core-dependency table per stage; two core trees in CI; PoC update by new branch;
  cumulative lists; Done-when that passed trivially fixed.
