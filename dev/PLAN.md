# PLAN — TrueAsync rebuilt as a regular PHP extension

Updated: 2026-10-02 · Active: S3.2

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

Test layers: `dev/plans/S2.md`, section 4.

## S3 — Scheduler on the scheduler API  [ ]

Goal: coroutines, the run queue and the request lifecycle, no reactor yet.
Done when: the S3 list passes on debug and ASAN and on both core trees (tests gated on an RFC
change SKIP on PoCs-only with that reason); scheduler fuzz runs the list over 100 seeds clean;
every wait-model line covered on `pocs-dbg-cov` except the listed out-of-memory branches; the
fault-injection points (`--enable-true-async-test-hooks`) drive a bailout through every unlink site;
Mull survivors in the wait and scheduler code killed by a test or explained (D34).
Tier: T2. Roles: Critic on S3.1, Critic after S3.4, security pass (S3.5).

- [x] S3.1 Design note: coroutine object and methods (classes from TrueAsync), run queue, idle point before the deadlock
      and end-of-main decisions, cancellation, end-of-main and bailout policy, GC destructor
      coroutine, `zend_fiber_switch_blocked()` honoured in suspend, EH_THROW window saved per
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
- [~] S3.2 Fixes on `async-core` that S3 needs, each with a test: `F_STARTED` (bit 8) and the fiber
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
        "Pinned core" point at `8a29d63edcf`. Left: CI green on it.
      RFC text 2026-10-02: true-async/php-async-core-rfc `f1e64a8`, both `scheduler_rfc.md` and
        `.dokuwiki.txt`: the *started* attribute (item 1) and enqueue of a finished coroutine
        refused with an `Error` (item 7).
- [ ] S3.3 All 21 slots, internal context init and destroy; `Async\spawn`, `await`, `suspend`,
      `Coroutine` and its methods, `DeadlockError`.
- [ ] S3.4 Shutdown windows without IO: shutdown functions, destructors, output handlers.
- [ ] S3.5 Security pass over the stage diff by `dev/SECURITY.md`; findings fixed with a test or
      recorded with the reason; the open finding on the runner's secret filter closed.

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
Known: the True Async RFC (true-async/php-true-async-rfc, `base.rfc:80-87`) declares
`Cancellation extends \Throwable`, which an extension cannot implement; D8 chose
`AsyncCancellation extends \Error`. The RFC text needs that change.

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
