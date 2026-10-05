# PLAN — TrueAsync rebuilt as a regular PHP extension

Updated: 2026-10-05 · Active: S3.20 (approved by Edmond 2026-10-05)

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

## Open questions

Waiting for Edmond's call; nothing here is being worked on. None open.

## S1 — Core branch `async-core-io`  [x] (S1.5 deferred)

Goal: the php-src the extension is built on: master + the two PoCs + ior, nothing else.

- [x] S1.1 `~/php-src2` worktree; ior `2fb12e8ce01` built into a Release and an ASAN prefix.
- [x] S1.2 Baselines of the scheduler PoC and the IO hooks PoC with the same configure line.
- [x] S1.3 `async-core-io` from `async-core`, master and the IO hooks PoC merged; diffed against S1.2.
- [x] S1.4 Rules in `dev/WORKFLOW.md` ("Branches", "Pinned core"): a core update is a new merge-only branch.
- [ ] S1.5 Windows: `async-core-io` built with nmake (Debug_TS) and ior for IOCP (as the PR's
      `build-ior-windows` action does); the three suites run; per-test diff against Linux.
      Deferred (Edmond, 2026-10-01): no Windows agent yet. When one exists, this session writes
      `tools/windows/` (README, build-ior.ps1 with Debug `/MDd`, build-core.bat, run-suites.bat;
      structure agreed) and the agent runs it.

## S2 — Repository and test system  [done]

Goal: `true-async/true-async` (clone in `~/true-async`) builds against `~/php-src2`, and the
test system every later stage plugs into exists.
Notes: dev/plans/S2.md (build, runner, lists, layers, CI, Mull)

- [x] S2.1 Design note of the test system and of the build against the core (`dev/plans/S2.md`).
- [x] S2.2 Skeleton, core prefixes, S1 suites rediffed.
- [x] S2.3 Runner, list checker, registration test.
- [x] S2.4 CI: `async-core-io` published in true-async/php-src; cached core prefixes; Linux and Windows jobs.
- [x] S2.5 Mutation: Mull for clang 18 on the stage diff, known-answer check both ways.
- [x] S2.6 Actions on Node 24 in every project repository (356eb2b).

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

- [x] S3.1 Design note: coroutine object and methods, run queue, idle point (`dev/plans/S3.md`).
- [x] S3.2 Fixes on `async-core` that S3 needs, each with a test (5e33d89).
- [x] S3.3 Internal API: circular buffer, callbacks vector with the cursor rule, wait records (d56bcfb).
- [x] S3.4 Classes and test ports: exceptions, `Awaitable`, `Completable`, `Coroutine` (6b454c1).
- [x] S3.5 Spawn and run: the 21 slots, launch, run queue, enqueue (ece91a6).
- [x] S3.6 Suspend with the tick and direct switches, the context pool (47633ef).
- [x] S3.6a Scheduler coroutine: TrueAsync's hybrid algorithm (Edmond, 2026-10-02) (1766674).
- [x] S3.7 Await and GC: the wait model, the await slot, `getAwaitingInfo()` (927584a).
- [x] S3.8 Cancellation and exit paths: `cancel`, `protect`, graceful shutdown, deadlocks, `exit()` (779d7a8).
- [x] S3.9 Fibers: every Fiber runs as a coroutine (4a6cdbf).
- [x] S3.10 Shutdown windows and bailout (89e4fe7).
- [x] S3.11 Measurements B0-B5 against the reference, `dev/BENCHMARKS.md` (3cb5416).
- [x] S3.12 Fault sites U1-U6, the fuzz hook, `test.py --seeds` (ce472a1).
- [x] S3.13 Stage review; coverage 96.1 %, Mull reference run (8d792fa).
- [x] S3.14 Security pass by `dev/SECURITY.md` (9df3ee7).
- [x] S3.15 Health fixes 2026-10-02, extension (8fa582c).
- [x] S3.16 Health tests 2026-10-02: the refusals of `true_async.c` and others (cee7f53).
- [x] S3.17 Circular buffer cut to what S3 uses (9b03cb3).
- [x] S3.18 Health fixes 2026-10-02, core; pinned `async-core-io-2026-10-03` (4693b88).
- [x] S3.19 Health fixes, extension (health check 2026-10-05, `dev/HEALTH.md`; Edmond's go,
      2026-10-05): `async_composite_exception_add_exception` loses `transfer`, it and
      `async_ce_composite_exception` become static; `async_callbacks_add()` moves to test_hooks.c or
      goes; the circular buffer holds pointers only (no `item_size`, no zero-filled `count`
      branch); the gc_new_coroutine slot left NULL or set to new_coroutine; `check-gates.py` drops
      the two removed core types; PRINCIPLES P3.2 names the `windows` job; S3.md 621-623 and
      DECISIONS 202-203 restate the main-last reason after `c43060ea12d`.
      done: the S3 list passes as before; `check-gates.py` and `check-lists.py` clean
      handoff: done 2026-10-05 on core `a9de8425106`: `pocs-dbg` 321 PASS, `pocs-asan` 307 PASS and
        14 SKIP, 10 seeds on dbg 0 failed; gates, lists, roadmap and format clean. `internal/017`
        counts a wrapped buffer now; `classes/010`: `addException()` asserted when the list's next
        key was taken (the Critic), now throws as `$array[] =`. The gc slot stays NULL (it also survives S3.20 removing it). The main-last
        reason now names `main.c:1937-1945`, the try around the last from_main call.
- [ ] S3.20 Health fixes, core (`async-core`, one topic per commit, then a core update by
      `WORKFLOW.md`; Edmond's go, 2026-10-05): `is_safely` and the `gc_new_coroutine` slot removed or
      given RFC text; the object-less coroutine branch and the uncalled surface
      (`zend_async_get_scheduler_module`, `IS_OFF`, `IS_READY`, `CLASS_NO`, the context and
      new-coroutine aliases, `SCHEDULER_LAUNCH`, the second caller named by
      `zend_async_scheduler_unregister`'s comment) removed; the two VM-stack `ZEND_API` functions
      made static; the objects-store iterator's forwarder removed; `ZEND_ASYNC_DEACTIVATE` clears
      the scheduler-context flag (S3.md 318-319); tests 027, 037, 038, 040, 041, 042-045 get the
      reason of their departure from upstream and comments that match.
      done: `ext/test_scheduler/tests` equal per test on dbg and ASAN; `CORE_REF` and "Pinned core"
        moved

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
