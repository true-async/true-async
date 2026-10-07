# PLAN — TrueAsync rebuilt as a regular PHP extension

Updated: 2026-10-07 · Active: per stage, under its `Tier:` line (Parallel tracks)

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

Waiting for Edmond's call; nothing here is being worked on.

- `call_on_main_stack` and callbacks into PHP (S3.23, Critic and Sage): the slot moves only the
  stack pointer, as TrueAsync, so fn must not re-enter PHP. A JNI call whose Java code calls back
  into PHP would run PHP on the OS stack with the coroutine still current: a suspend there, a
  nested slot call, the fiber's stack limit or a bailout breaks one of the stacks. If Java
  embeddings need such callbacks, the slot's contract in the core changes; Edmond's call.
- A pcntl handler that waits (S4.3, Critic and Sage): `pcntl_signal_dispatch()` blocks every signal
  of the thread and holds the core's fiber-switch block while its handlers run
  (`ext/pcntl/pcntl.c:1424-1435`), so a handler that suspends lets other coroutines run with
  signals blocked and `Fiber::resume()` refused until it resumes, on the busy path and in the idle
  interrupt coroutine alike. Refusing that suspend needs `zend_fiber_switch_blocked()` in the
  scheduler's suspend, which the extension does not call by Edmond's rule (`tools/check-gates.py`
  refuses it); the second option is leaving it to the script, the third a core change (pcntl lifts
  its block and mask around a coroutine's suspend), an S8 change-request candidate. Edmond's call.

## Parallel tracks

Stages run as parallel threads where neither waits for the other's code (decided 2026-10-05, under
Edmond's coordinator mandate of the same day). Now: S4, S5 and S6.1. A gate names a result on
`main`, never a step number, since a design note may re-split its stage. No track waits for another
track's test runs: each tests against the same pinned core, in its own container.

| Stage | Starts | Because |
|---|---|---|
| S4 | now; its design note closes once S5's note has stated its needs | S4's design note covers the wait-record layer for both (D25, `DECISIONS.md` 2026-10-02: events are S4's) |
| S5 | S5.1 now; S5.2 once the wait-record layer is on `main`; `timeout()` once `delay()` is; closes after S4 | Futures and `await_*` need no reactor: 87 of the 97 `component:S5` lines of `tests/lists/S3.excluded` call neither `delay()` nor `timeout()` (counted 2026-10-05 in the reference at `REFERENCE`); `await_*` over a Traversable needs typed kinds; Done when includes S4's list |
| S6 | S6.1 now; S6.2 once S4's design note is pushed; S6.3's code once the reactor's queue is on `main`, its commit once `delay()` and `await_*` are; closes after S5 | the fixtures need no extension code; S4's note designs completion dispatch for provider ops too; the `io`, `exec`, `socket_ext` tests call `await_all` |
| S7 | once the S5 and S6 design notes are pushed | its roots are provider ops, timers, signals and wakeups, its edges Futures and channels |
| S8 | after S6 | measured on a real provider |
| S9 | after S5, each layer once Edmond agrees its plan | Scope's `awaitCompletion`, task groups and channels wait on Futures, cancellations and timeouts; Context hangs off Scope |
| S10 | after S6 | each outcome is a core or RFC change; the pool needs S9, the `zend_sigaction` hook S6, the thread pool the Fog's ZTS line |

While tracks run in parallel:

- A track edits only its own stage section of this file, its `Active:` line included; on the
  header's `Updated:` date the later one wins. Before a push the unpushed step commit is rebased
  on `origin/main` (`git pull --rebase`, WORKFLOW "Branches"). A conflict in this file, `config.m4`,
  `config.w32`, `CHANGELOG.md`, `dev/DECISIONS.md` or `dev/handoff.md` keeps both sides; the README
  roadmap is regenerated with `tools/roadmap.py` and `*_arginfo.h` from its stub, never merged by
  hand. A rebase that changed code, not only these files, runs the lanes again before the push.
- `dev/handoff.md`: a track adds its own section (`## S4`, `## S5`, `## S6`) in its first commit and
  replaces only that one. The shared sections stay S3's until the thread that closes S3 condenses
  them.
- New sources: their own `true_async_sources="$true_async_sources ..."` line in `config.m4` and
  their own `ADD_SOURCES` line in `config.w32`, so two tracks never edit one line.
- The wait-record layer has one owner, S4: the record struct, `async_wait_kind_t`, `F_TYPED`,
  unlink and abort, the reactor's lists of waits that decide a deadlock, the storage of a wait of several records
  and the event header (`dev/plans/S3.md` 3.3-3.7, section 4). S5's design note writes down what
  S5 needs from it; S5 defines its own kinds (FUTURE, AWAIT_ITER, the cancellation record) and
  their functions in its own files, and asks for changes to the layer through the coordinator
  instead of editing it. The scheduler loop's "nothing runnable" branch (`src/scheduler.c`) is
  S4's too; S5 puts no drain of its own there.
- One core update at a time, announced to the coordinator before it starts. It branches from the
  core `CORE_REF` names on `main` at that moment and takes the next free
  `async-core-io-<date>-<n>`; if `CORE_REF` moved before its push, the newer branch is merged in and
  the suites rediffed. The bridge `ext-scheduler-hook` builds and passes on every new core. The
  `rfc` tree (`dev/plans/S2.md` section 5) is S4's: the owned `NotifyHandle` request is its first
  RFC change.
- A ported test goes to the list of the stage its `S3.excluded` line names and leaves that file in
  the same commit; a test listed twice fails `check-lists.py`. A test that needs both tracks names
  in its `--XFAIL--` the step of its own stage that needs the other track's result; whichever push
  makes it pass removes the section, in either track's file. A new test in a shared group
  (`internal/`, `scheduler/`) takes its number at the push and is renumbered if a rebase took it.
- Long runs inside a step: one clone's lanes run one after another (two `test.py` runs share
  `tests/`, handoff S3.16), and each already takes every core (`--jobs` and `make -j` default to
  the CPU count). The Critic and the Sage read the local commit while the lanes run, not after;
  a fix amended from their findings reruns the lanes it touches before the push. 100-seed fuzz
  runs only in a stage review. Timer tests wait on the clock, not the CPU: S4.4
  times a lane with `--jobs` above the core count (assumption until then, not measured).

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

## S3 — Scheduler on the scheduler API  [x]

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
- [x] S3.20 Health fixes, core (`async-core`, one topic per commit, then a core update by
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
      handoff: done 2026-10-05 on core `8a927bda8f6`: the core's `ext/test_scheduler/tests` and
        `Zend/tests` equal per test with `a9de8425106` on dbg (5,544 PASS); test_scheduler and
        `Zend/tests/{fibers,gc,generators}` on ASAN (468 PASS); `pocs-dbg` 328 PASS, `pocs-asan` 313
        PASS and 15 SKIP, 10 seeds on dbg 0 failed (323 tests, before `287a2c5`); gates, lists, roadmap and format clean. The
        out-of-tree bridge `ext-scheduler-hook` calls `zend_async_get_scheduler_module`,
        `zend_async_scheduler_unregister` (its failed launch is the second caller),
        `SCHEDULER_LAUNCH` and the VM-stack helpers: kept, the health check had counted two
        providers only. Removing the forwarder departs from TrueAsync (P1.4); the Sage kept the
        removal. Every API removal of S3.18 and S3.20 is undone in S3.21.
- [x] S3.21 Core API back (`async-core`, one revert per removing commit, then a core update;
      Edmond, 2026-10-05: "RFC создаётся для многих API, функции в нём потенциально могут быть
      кем-то использованы"): what S3.18 and S3.20 removed for having no caller comes back
      (`active_coroutine_count`, `call_on_main_stack`, `coroutine_from_object`, `F_OBJ_REF`,
      `GET_EXCEPTION_CE`, `zend_async_is_enabled()`, `extra_size`, `is_safely`, `gc_new_coroutine`,
      the object-less coroutine, the state, class and context aliases, the objects-store pass taking
      the store); the fixes those steps carried stay. The bridge `ext-scheduler-hook` builds again.
      done: core tests equal per test on dbg and ASAN; extension lanes green; the bridge's tests
        pass on both trees; `CORE_REF` and "Pinned core" moved
      handoff: done 2026-10-05 on core `710a79707ce`: `Zend/tests` and `ext/test_scheduler/tests` equal
        per test with `8a927bda8f6` on dbg (5,544 PASS), test_scheduler and
        `Zend/tests/{fibers,gc,generators}` on ASAN (468 PASS); `pocs-dbg` 328 PASS, `pocs-asan` 313
        PASS and 15 SKIP; the bridge builds with no warning and passes its 22 tests on dbg and ASAN
        after it fills the API `version` field (bridge `92e14e7`). Our scheduler fills
        `coroutine_from_object` again and ignores `is_safely` until S9. The API version is a counter
        (`ZEND_ASYNC_API_VERSION 1`, Edmond 2026-10-05): two changes on one day had got the same
        date.
- [x] S3.22 The Critic's six findings on S3.21, one question each to Edmond (2026-10-05): the
      core launches the scheduler only in a READY request, so the bridge works in every request
      (it registers its C slots once per process, PHP code calls `register()` per request);
      `extra_size` and `ZEND_ASYNC_NEW_COROUTINE_EX` go (API version 2); `is_safely` says
      TrueAsync's zombie and `ZEND_ASYNC_CANCEL_EX` passes it; `active_coroutine_count` becomes
      the `get_coroutine_count` slot, filled by ours and test_scheduler; every coroutine has an
      object; `call_on_main_stack` stays (S3.23).
      done: core tests equal per test on dbg and ASAN; extension lanes green; the bridge passes on
        both trees, in a second request too; `CORE_REF` and "Pinned core" moved
      handoff: done 2026-10-05 on core `9531d5b0b1f`: `Zend/tests` and `ext/test_scheduler/tests`
        equal per test with `710a79707ce` on dbg (5,545 PASS with the new `090`),
        test_scheduler and `Zend/tests/{fibers,gc,generators}` on ASAN (469 PASS); `pocs-dbg` 329
        PASS, `pocs-asan` 314 PASS and 15 SKIP; the bridge's 23 tests pass on dbg and ASAN, and
        `php-cgi -T 3` runs its scheduler in each of three requests (bridge `a0fc2fd`).
- [x] S3.23 Our scheduler fills `call_on_main_stack` with TrueAsync's `async_call_on_main_stack`
      (`php-async/scheduler.c:154`), with a test (Edmond 2026-10-05: the slot exists for Java and
      mobile embeddings).
      done: `scheduler_call_on_main_stack` and `async_asm_stack_call` in `src/scheduler.c`; test
        `internal/051` through the test hook `call_on_main_stack()`; extension lanes green
      handoff: done 2026-10-05 on core `9531d5b0b1f` (unchanged): `pocs-dbg` 330 PASS, `pocs-asan`
        315 PASS and 15 SKIP. `internal/051` fails on all six off-main lines with the slot left
        unfilled. The x86-64 path runs here; the AArch64 asm only assembles (clang), not run. The
        Critic found a crash after the core turned async off (fixed: TrueAsync's early-out on no
        current coroutine; RSHUTDOWN no longer leaves `EG(current_fiber_context)` at the freed copy
        of main's context) and GCC 13 ignoring `naked` on AArch64 (guarded by
        `__has_attribute(naked)`); callbacks into PHP are an open question.
- [x] S3.24 Stage exit: Done when re-run on the final core, the bridge with it.
      done: every Done when line holds on `9531d5b0b1f`; the bridge passes on dbg and ASAN
      handoff: done 2026-10-06 on main after S5.2 with this step's four tests: `pocs-dbg` 334 PASS,
        `pocs-asan` 319 PASS and 15 SKIP (U1-U6 in `internal/027`-`033` among them); 100 seeds on
        dbg over 333 tests (before `scheduler/105`), 0 failed; the bridge's 23 tests pass on both trees. `pocs-dbg-cov`
        97.3 % of `src/` (2184 of 2244); before the new tests 97.0 % (2168 of 2236). Mull on the
        lines changed since S3.13's run (the whole stage diff, about 800 mutants, did not finish in
        two hours): 36 mutants, 30 killed, 3 more killed by `scheduler/105`, 3 explained. The new
        uncovered lines and the survivors: S3.md section 9. Tools: `mull.py` could not run a list on
        the core's run-tests (a worker per CPU unless `-j1`, mull-runner's 3 s timeout); it runs
        each mutant on its own copy of `tests/` with every CPU and takes `--build-ref`. The seed
        report compares a diagnostic with the expected lines of its kind, EXPECTF placeholders
        expanded; the 10 tests it lists were read: order artifacts. `check-lists.py` refuses a
        shallow clone, where every list looked changed. CI on `main` was red since S3.22: `pocs-win`
        SKIPs `internal/051` (Linux only) untagged, now tagged; the cancelled ASAN and lists jobs
        say "The job was not acquired by Runner of type hosted even after multiple attempts".

## S4 — Reactor on Poll, Poll additions and Ring  [ ]

Goal: the scheduler's idle wait and timers on one per-thread `php_io_queue` (the Ring when built
with ior, the Poll queue otherwise), coded only against `php_io_queue_ops`; the S6 provider
submits to the same queue, so completion dispatch is designed here once for both.
Done when: S3 + S4 lists pass; `delay(1000)` costs under 50 ms of user CPU; a test-only C
function wakes the loop from another pthread (through wake descriptors of the reactor's own until
the core exports the pair behind `NotifyHandle`, `RFC-CHANGES.md` 1).
Tier: T2. Roles: Critic on S4.1, Critic after S4.2 and after S4.3.
Active: S4.6

- [x] S4.1 Design note `dev/plans/S4.md`: completion dispatch for scheduler-owned ops and provider
      ops; idle wait in `queue->wait()` with its `EDEADLK` and `EINTR` answers; deadlock decided
      from the scheduler's own count of parked user waits (a wakeup op is always pending, review
      M5); `delay`/`timeout` as Timer ops; the wait-record layer for S4 and S5 (Parallel tracks),
      D25 decided (kinds or event methods); the Windows path (IOCP Ring for pipes, console,
      processes); cross-thread wakeup on an own eventfd/pipe (no C API for an owned `NotifyHandle`;
      an RFC change request); the queue rebuilt lazily after fork by pid; frozen list
      `tests/lists/S4.txt` and core-dependency table. It may re-split S4.3-S4.5.
      done: the note and the list pushed; S5.1's needs answered in it; every Critic finding fixed
        or answered in the note
      handoff: done 2026-10-05: `dev/plans/S4.md` (kinds on the record per D28, no event methods;
        two records inline in the waker and a stage-owned block past two; the reactor's `waits`
        and `triggers` lists decide a deadlock instead of a counter; heap Timer events; the core's
        `NotifyHandle` for cross-thread wakeup; the queue rebuilt on `EPERM` after a fork, the
        parent's waits ending in the child's deadlock report); S5's N1-N9 answered in its 2.5 (N7
        without `F_COUNTED`); `tests/lists/S4.txt` holds 9 tests with `--XFAIL--` naming S4.4,
        `edge_cases/016`, `017` stay excluded until zlib in S4.4. On core `9531d5b0b1f` after the
        rebase on S5.1: `pocs-dbg` 333 PASS, 144 XFAIL; `pocs-asan` 318 PASS, 16 SKIP, 143 XFAIL.
        The Critic's 2 critical and 6 major findings and the Sage's six rulings are in the note.
- [x] S4.2 The wait-record layer and the event header as S4.1 designs them (`dev/plans/S4.md`
      section 2), no reactor yet.
      done: the S3 list passes unchanged on `pocs-dbg`, `pocs-asan` and `pocs-win`; internal tests
        link and unlink waits of one, two and five records (a test block, records linked into a
        parked waiter's block by another coroutine) by a wake, a cancel and a bailout out of the
        tick, with no record left linked and the block released once; B1 measured again
      handoff: done 2026-10-05: the layer in `src/true_async_API.h`/`.c` and `src/coroutine.h`
        (as built: `dev/plans/S4.md` 2.3), `TrueAsync\Test\Event` and three test functions in the
        hooks, tests `internal/052`-`061` in `S4.txt`. `pocs-dbg` 343 PASS, 144 XFAIL; `pocs-asan`
        328 PASS, 16 SKIP, 143 XFAIL; `pocs-win` left to CI. B1 7.5 instructions more per spawn, no
        allocation more (`dev/BENCHMARKS.md`). The Critic's 8 findings fixed (the waiter takes its
        block as `suspend()` returns, the finish aborts a wait still linked, a teardown unlinks by the
        kind), each fix caught by a test when reverted.
- [x] S4.3 The per-thread queue and the idle wait: the scheduler parks in `queue->wait()` when
      nothing is runnable and the reactor's lists of waits are not empty, deadlock from those
      lists, the interrupt coroutine, the queue rebuilt after fork.
      done: the S3 list passes unchanged on the three lanes; a test parks the scheduler in the
        queue and is woken by a pending op
      handoff: done 2026-10-06: `src/reactor.c`/`.h` (as built: `dev/plans/S4.md` 3.7), the tick's
        throttled poll and the idle wait in `src/scheduler.c`, the interrupt coroutine, the fork
        rebuild (pid at submit, `EPERM` at the wait); tests `reactor/001`-`012` through
        `TrueAsync\Test\reactor_wait()`. `pocs-dbg` 355 PASS, 144 XFAIL; `pocs-asan` 340 PASS,
        16 SKIP, 143 XFAIL; `pocs-win` left to CI. B1 8.1 instructions more per spawn
        (`dev/BENCHMARKS.md`). The Critic's and the Sage's rounds on the interrupt and the fork
        design kept the coroutine, added the pid check and the open question of a handler that
        waits; the Critic's code findings fixed, each caught by a test when reverted.
- [x] S4.4 Timer ops: `Async\delay()`, the TIMER kind and the D16 deadline; `--with-zlib` in the
      core build.
      done: the `component:S4` tests listed in S4.txt and `edge_cases/016`, `017` passing on debug
        and ASAN; `delay(1000)` measured under 50 ms of user CPU and a lane with `--jobs` above the
        core count timed, both in `dev/BENCHMARKS.md`
      handoff: done 2026-10-06: `delay()` in `src/reactor.c` (`async_reactor_delay()`, the TIMER
        kind), D16 in `src/scheduler.c` on the reactor's new `own` list (as built: `dev/plans/S4.md`
        3.7 "As built (S4.4)"), `--with-zlib` in `tools/ci/build-core.sh`; tests `reactor/013`-`025`,
        `edge_cases/016`, `017` ported. Seven of the nine S4 tests lost `--XFAIL--`; `gc/020`, `023`
        keep it for the core's awaited collection (an S8 candidate). All 822 listed tests on
        top of S7.1: `pocs-dbg` 605 PASS, 8 SKIP, 209 XFAIL; `pocs-asan` 590 PASS, 25 SKIP, 207
        XFAIL; `pocs-win` left to CI; `sleep/003` and `signal/015` of S6.txt lost `--XFAIL--` too.
        `delay(1000)`: no user CPU beyond the process's start; `--jobs 16` 9.6 s against 10.3 s at 4. The Ring wakes 10 000 waiters of one
        deadline 39-66 ms late (the Poll queue 0.1 ms): S4.6 looks at it. The Critic's 2 major and
        4 minor findings fixed or ruled on by the Sage (refire every 100 ms, arm only with a started
        coroutine, withdrawn when the drain ends, two limits documented), each fix caught by a test
        when reverted.
- [x] S4.5 Cross-thread wakeup on the core's `NotifyHandle`; the request for its C constructor
      filed by `RFC-CHANGES.md`.
      done: a test-only C function wakes the parked loop from another pthread, on debug and ASAN
      handoff: done 2026-10-06: triggers in `src/reactor.c` (`async_trigger_*`, the TRIGGER kind, the
        wakeup POLL with the event's new `complete`), as built in `dev/plans/S4.md` 3.6. No
        `NotifyHandle`: the handle dies with the request while a thread may hold a trigger, so the
        reactor keeps an eventfd, pipe or socket pair per thread (the Sage); `RFC-CHANGES.md` 1 asks
        the core to export it. Test hooks `TrueAsync\Test\trigger_*()`, tests `reactor/026`-`036`.
        The Critic's design round (1 critical, 5 major) and code round (3 major, 7 minor) fixed or
        ruled on; each tested behaviour caught by a test when reverted. All 837 listed tests:
        `pocs-dbg` 620 PASS, 8 SKIP, 209 XFAIL; `pocs-asan` 605 PASS, 25 SKIP, 207 XFAIL;
        `pocs-win` left to CI.
- [ ] S4.6 Stage review: Critic after S4.2-S4.5, coverage of the reactor code, Mull on the stage
      diff, fuzz over 100 seeds; the Ring's lateness with many Timer ops (`dev/BENCHMARKS.md`,
      S4.4: a timer heap of the reactor's own, as libuv's, or a core change to the Ring's backlog).
      done: Done when of S4 holds on the day; survivors killed or explained
- [ ] S4.7 Security pass by `dev/SECURITY.md`.
      done: a journal entry per checklist item; findings fixed with a test or recorded

## S5 — Futures, timeouts and combinators  [ ]

Goal: the API the ported tests use everywhere.
Done when: S3–S5 lists pass, including the `await` group's combinator tests.
Tier: T2. Roles: Critic on S5.1, Critic after S5.2.
Notes: dev/plans/S5.md
Active: S5.5

- [x] S5.1 Design note `dev/plans/S5.md`: `Future` and `FutureState` as TrueAsync has them within one
      thread (the remote and cross-thread futures wait for S10's thread pool), `map`/`catch`/
      `finally` chains completed without recursion (`dev/plans/S3.md` 3.6), `await()` on any
      awaitable with `$cancellation`, the `await_*` family over arrays and Traversables (S3.md
      section 4 table), `OperationCanceledException`; S5's kinds (FUTURE, AWAIT_ITER, the
      cancellation record) and what they need from S4's wait-record layer, the storage of N
      records included; frozen list `tests/lists/S5.txt` with each test that
      needs `delay()` or `timeout()` marked; core-dependency table.
      done: the note and the list pushed; every Critic finding fixed or answered in the note
      handoff: done 2026-10-05: `dev/plans/S5.md`, needs N1-N9 for S4.1 in its section 7;
        `tests/lists/S5.txt` holds 135 tests (97 from `S3.excluded`, 38 of `future/`), each with
        `--XFAIL--` naming S5.2, S5.3 or S5.4; on core `9531d5b0b1f` `pocs-dbg` 330 PASS and 135
        XFAIL, `pocs-asan` 315 PASS, 16 SKIP and 134 XFAIL. The Critic's 13 findings are in the
        note; the Sage ruled on three: mappers run in one drain coroutine with TrueAsync's helper
        microtask, a mapper's error goes only into its child, the shared event is reported to the
        GC only by its sole holder. `iterate()` moves to S9.
- [x] S5.2 `Future` and `FutureState` with their chains; `await()` and `Future::await()` on a Future
      (once the wait-record layer is on `main`).
      done: S5.txt's `future/` tests pass on debug and ASAN; the S3 list unchanged
      handoff: done 2026-10-06: `src/future.c`; the 38 `future/` tests pass with their `--XFAIL--`
        removed, and 14 own tests `future/100`-`113` join S5.txt (GC cycles, the drain's order and
        helpers, `exit()` in a mapper, a 200000-deep chain). On core `9531d5b0b1f` after S4.3 and S6.2
        `pocs-dbg` 581 PASS, 8 SKIP and 218 XFAIL, `pocs-asan` 566 PASS, 25 SKIP and 216 XFAIL, 0
        unexpected. The Critic's three high findings and the re-check's medium one are fixed (S5.md
        section 3, "As built in S5.2"). `Future::await()` takes no `$cancellation` until S5.3.
- [x] S5.3 `$cancellation` on `await()` and the `await_*` family.
      done: every S5.txt test that needs no timer passes on debug and ASAN
      handoff: done 2026-10-06: `src/await.c`, `OperationCanceledException`; the 87 S5.3 tests pass
        with their `--XFAIL--` removed, and so do 48 S6 tests and `coroutine/038` that waited for
        `await_*`; 16 own tests `await/100`-`115` join S5.txt. `await/062` waits for S9's scope. On
        core `9531d5b0b1f` after S4.4 `pocs-dbg` 754 PASS, 10 SKIP and 74 XFAIL, `pocs-asan` 738
        PASS, 27 SKIP and 73 XFAIL, nothing else unexpected (`edge_cases/016`, `017` skip in a
        container whose cores lack zlib). The Critic's high finding (the token checked before
        `getIterator()`) and five medium ones are fixed or documented (S5.md section 5, "As built in
        S5.3"; section 8, item 10).
- [x] S5.4 `timeout()` and `TimeoutException` on Timer ops (once `delay()` is on `main`).
      done: the S5.txt tests marked for timers pass on debug and ASAN
      handoff: done 2026-10-06: `src/timeout.c` (D32: one deadline per `timeout()`, the timer armed
        only while a wait is parked on it, fired for good); the 9 S5.4 tests and S6's `dns/006` pass
        with their `--XFAIL--` removed, and 12 own tests `await/116`-`127` join S5.txt. On core
        `1ee473ff67b` after S6.4 and S7.2 `pocs-dbg` 863 PASS, 8 SKIP and 34 XFAIL, `pocs-asan` 847
        PASS, 25 SKIP and 33 XFAIL, 0 unexpected (S6's `dns/003` failed once on an earlier dbg run:
        `localhost` resolved before the other coroutine ran; 3 of 3 alone pass). The Critic's three defects (a forked child's first wait
        before any submit, one shared `TimeoutException`, a bailout between the subscribe and the
        link) are fixed with `await/117`, `124`, `127` (S5.md section 6). Also: the `const` that
        broke the clang and MSVC builds since S5.3 (`src/true_async.c`).
- [ ] S5.5 Stage review: Critic after S5.2-S5.4, coverage, Mull on the stage diff, the S3.md section
      12 benchmarks of `await_*` (N in 1, 2, 8, 100, 10 000).
      done: Done when of S5 holds on the day; survivors killed or explained; results in
        `dev/BENCHMARKS.md`
- [ ] S5.6 Security pass by `dev/SECURITY.md`.
      done: a journal entry per checklist item; findings fixed with a test or recorded

## S6 — IO hooks provider  [ ]

Goal: blocking PHP functions suspend the coroutine through `run()` on the reactor; signals and
children through the same contract.
Done when: S3–S6 lists (from `sleep`, `io`, `stream`, `socket_ext`, `dns`, `curl`, `exec`,
`mysqli`, `pdo_mysql` without the pool, `signal`) pass on debug and ASAN; IO chaos runs clean
over 100 seeds; tests that fail because of the hooks design are listed against the review item;
`dns` counted only on the Ring configuration (the Poll queue answers Unsupported for lookups).
Tier: T2. Roles: Critic on S6.2, Critic after S6.7 (S6.8).
Active: S6.7

- [x] S6.1 Fixtures: MySQL with two connections and an HTTP server with
      `PHP_CLI_SERVER_WORKERS`, started by `tools/test.py` locally and by the CI lanes.
      done: a smoke test per fixture, listed in `tests/lists/S6.txt` (two MySQL connections; a request to the server) passes on
        `pocs-dbg` and `pocs-asan` in a fresh container and in CI, with no async code involved
      handoff: done 2026-10-05 on core `9531d5b0b1f` (unchanged), in a fresh container: `pocs-dbg`
        333 PASS, `pocs-asan` 318 PASS and 15 SKIP; CI not watched. MySQL: TrueAsync's `mysql:8.3`
        service in CI, a private `mysqld` started by `tools/test.py` locally (`mysqli/011`). HTTP:
        TrueAsync's per-test server (`common/http_server.php`), every test's server with four
        workers (`common/http_server_fixture`, `common/http_server_workers`; the latter answers
        "timeout" without workers); WORKFLOW "Test fixtures".
- [x] S6.2 Design note: install point per request and thread, readiness ops, deadlines, the
      suspend predicate, M12 (a `zend_try` around the suspend or an op off the frame), Unsupported
      when async is off (M13), DNS and files, SigWait on `SignalHandle` and WaitPid on `ProcessHandle` with
      the core's reaped-status table; signal ownership with threads decided (a live
      `SignalHandle` blocks the signal in its own thread only; until decided, `signal/*` is
      excluded with that reason); frozen list; core-dependency table.
      done: the note and the list pushed; every Critic finding fixed or answered in the note
      handoff: done 2026-10-06: `dev/plans/S6.md`. The provider is installed at the first
        coroutine other than main or at the reactor queue's creation; `run()` submits a heap copy
        of the op, so M12 needs no `zend_try` (TrueAsync's heap events); a completed op under a
        late cancellation returns its result with the exception pending; files stay synchronous
        until the core's commit-on-settle (B3); signals belong to the one PHP thread of the
        process (`signal/008`, `009`, `012` excluded for S10); Windows `proc_open()` pipes become
        overlapped named pipes in a core commit of S6.3. `tests/lists/S6.txt` gains 294 tests
        and 8 helpers, 38 excluded in `S6.excluded`; `tools/test.py` passes `opcache.jit=off`.
        On core `9531d5b0b1f` after the rebase on S4.3: `pocs-dbg` 529 PASS, 8 SKIP, 256 XFAIL;
        `pocs-asan` 514 PASS, 25 SKIP, 254 XFAIL. The Critic's 1 critical and 7 major findings, the Sage's nine rulings and
        two Critic re-checks against S4.3's reactor are in the note; nothing went to Edmond.
- [x] S6.3 The provider for every op type (`dev/plans/S6.md` sections 2, 3, 5, 13), pipes and
      timers.
      done: `sleep` and `io` without `--XFAIL--` except the by-design ones and those naming a core
        change; every test that passed before still passes on `pocs-dbg` and `pocs-asan`; the
        note's S6.3 own tests pass
      handoff: done 2026-10-06 on core `1ee473ff67b`: `pocs-dbg` 809 PASS, 8 SKIP, 47 XFAIL;
        `pocs-asan` 793 PASS, 25 SKIP, 46 XFAIL; nothing unexpected. `src/io_provider.c`: `run()`
        parks on a heap copy of the op and keeps its own reference (the Sage, after the Critic found
        stale writes through frame pointers). Also passing now, sections removed: 12 `stream/` and
        `socket_ext/` tests of S6.4 and `curl/069`. Moved: the Windows pipe commit to S6.5,
        `io/035`-`037`, `094`, `095` and two bailout tests to S6.7; the pipe timeout and `io/100`
        name S8 (`RFC-CHANGES.md` 3). Bugs found in bukka's code went to him (`io-hooks-fixes`);
        `io/094` is a php-src streams bug, its branch asked of Edmond.
- [x] S6.4 Sockets (Recv, Send, Accept, Connect, Poll, Any, registrations) and DNS on the Ring.
      done: `stream`, `socket_ext`, `dns` without `--XFAIL--` except the by-design ones and those
        naming a core change or another track's step
      handoff: done 2026-10-06 on core `1ee473ff67b`: `pocs-dbg` 816 PASS, 8 SKIP, 44 XFAIL;
        `pocs-asan` 800 PASS, 25 SKIP, 43 XFAIL; nothing unexpected. An accept is `accept()` first,
        then a POLL READ: the Ring's multishot accept hid pending connections from `stream_select()`
        (the Critic; `io_provider/015`), a bug in bukka's code told to Edmond. `stream/004`, `007`,
        `028` changed (the core resolves a numeric host without a switch); `stream/017`, `018` XFAIL
        by design (PHP's `stream_select()` errors); `stream/030` names S8 (`RFC-CHANGES.md` 4);
        `dns/006` waits for S5.4's `timeout()`; `io/096` names S6.7 (B1). Own tests
        `io_provider/012`-`015`. The Windows lane's socket expectations moved to S6.5.
- [x] S6.5 Children and signals: WaitPid, SigWait, `Async\signal()` (once S5.2's Future is on
      `main`).
      done: `exec`, `signal` without `--XFAIL--` except the by-design ones and those naming S6.10
      handoff: done 2026-10-06 on core `1ee473ff67b`: `pocs-dbg` 880 PASS, 8 SKIP, 25 XFAIL;
        `pocs-asan` 864 PASS, 25 SKIP, 24 XFAIL;
        nothing unexpected. `src/os_signal.c`: one watch per signal number while a Future waits
        for it, a `SignalHandle` in a thread context that blocks the number and one SIGWAIT op on
        the reactor; a delivery completes every waiting Future and goes on to a pcntl handler; a
        signal still pending at the last Future is raised again; a fork rebuilds the watches. Own
        tests `signal/016`-`023`. `exec/012`, `025` XFAIL by design. `SIGBREAK` and `SIGABRT2`
        throw (TrueAsync maps them to other numbers); `Async\signal()` throws on Windows. Moved:
        the Windows parts to S6.10 (Edmond deferred S1.5). Core requests in `RFC-CHANGES.md` 5.
- [x] S6.6 curl, mysqli, pdo_mysql without the pool.
      done: `curl`, `mysqli`, `pdo_mysql` without `--XFAIL--` except the by-design ones
      handoff: done 2026-10-07 on core `1ee473ff67b`: `pocs-dbg` 884 PASS, 8 SKIP, 20 XFAIL;
        `pocs-asan` 868 PASS, 25 SKIP, 19 XFAIL; one left out (`core:6`); nothing unexpected. No code
        changed: `curl/006` times out on a listener of its own; `curl/025`, `054`, `043` expect
        what the core's curl gives (two send warnings; no error on a multi handle before
        `curl_multi_info_read()`); `pdo_mysql/029` waits for `RFC-CHANGES.md` 6 (the cancellation
        under a driver error). Windows curl moved to S6.10; coverage lanes skip Windows-only tests.
- [ ] S6.7 IO shutdown windows (the `ts_suspend` NULL case), the seven core-tree tests,
      `io/035`-`037` (Async in a `php -r` child), `io/094`, `095` (streams fixes), the `run()`
      bailout tests S6.3 moved here (`dev/plans/S6.md` section 12), every list
      run, the by-design failures tagged `core:` against their review items, the RFC requests of
      the note's section 14 in `RFC-CHANGES.md`.
      done: Done when of S6 holds except the review
- [ ] S6.8 Stage review: Critic over S6.3-S6.7, coverage, Mull, IO chaos over 100 seeds.
      done: findings fixed or answered; chaos clean over 100 seeds
- [ ] S6.9 Security pass by `dev/SECURITY.md`.
      done: a journal entry per checklist item; findings fixed with a test or recorded
- [ ] S6.10 Windows (once S1.5 gives a Windows agent): the `proc_open()` pipe core commit
      (`dev/plans/S6.md` section 9), the Windows lane's socket expectations from S6.4.
      done: `sockets` and `openssl` load in `pocs-win`; no `xfail-on` tag or `--XFAIL--` names
        S6.10; the frozen `skip-on:pocs-win(...-until-S6.4)` and `(...-until-S6.5)` tags skip nothing

## S7 — Async object collector  [ ]

Goal: find coroutines that can never wake and the async objects only they keep alive: partial
deadlocks (a cycle of waits while other coroutines run), a Future nobody can complete, a channel
with no senders left; report them and resolve them by policy (a cancellation into the waiters, as
the global deadlock's, or a report only; `dev/plans/S7.md` section 6).
Done when: tests for each case pass on debug and ASAN; waits on IO, timers, signals and
cross-thread wakeups are never reported; a run over 10 000 parked coroutines costs a measured
time, recorded; scheduler fuzz over 100 seeds reports no false positives.
Tier: T2. Roles: Critic on S7.1, Critic after S7.4 (S7.5).
Notes: dev/plans/S7.md
Active: S7.5

- [x] S7.1 Design note: roots (runnable coroutines, pending external sources: provider ops,
      timers, signals, wakeups, main), edges (waiter → awaitable → completers), when it runs (on
      idle, on PHP GC, on demand), the policy per finding, interaction with PHP's own GC
      (suspended coroutines are GC roots through their stacks), the PHP API (report, setting).
      done: the note and the list pushed; every Critic finding fixed or answered in the note
      handoff: done 2026-10-06: `dev/plans/S7.md`. Completers cannot be enumerated, so the walk
        is PHP's trial deletion without the freeing over what parked coroutines reach, with Go's
        rule that a parked stack counts once a target is live; outside sources come from the
        reactor's lists that decide the global deadlock, and unknown holders hide findings, never
        invent one. Runs at the idle point with an interval and a back-off, and on demand
        (`get_deadlocked_coroutines()`); policy `report` by default, `cancel` (TrueAsync's
        per-waiter cancellation) by setting. `tests/lists/S7.txt` frozen with no test (the
        reference has none). The Critic's 2 critical and 5 major findings, and 2 high ones of its
        re-check, changed sections 3-6 and 10-12; nothing went to Edmond. Lanes not run: the step
        adds no code and no listed test.
- [x] S7.2 The walk, `collector_target` for COROUTINE, `get_deadlocked_coroutines()`, coroutine
      waits, the automatic run with `report` and its back-off, the fuzz oracle (S7.md 3-5, 11).
      done: S7.txt's S7.2 tests pass on `pocs-dbg` and `pocs-asan`; every list unchanged; the
        lists over 10 seeds with the oracle, `report` and the interval at 0 report no false finding
      handoff: done 2026-10-06: `src/collector.c` walks the parked coroutines in a side table
        (wake edges, count, spread) and touches no reference count; COROUTINE is the only kind with
        `collector_target`, so S7.3 adds the event kinds and the reactor lists' seeding together.
        `Async\get_deadlocked_coroutines()`, INI `true_async.partial_deadlock` (`report`, `off`;
        `cancel` refused until S7.4) and `true_async.partial_deadlock_interval`; the run sits in
        `scheduler_loop`'s idle branch. The oracle aborts a seed on a wake or a cancel of a found
        coroutine that nothing handed out (`registry_cancel()` in `src/scheduler.c`). 25 own tests
        in `collector/`; known miss: a coroutine parked inside a generator (`collector/017`).
        `module/001`, `002` list the new INI (`changed:`, the Critic judged). Critic two rounds,
        the Sage once; nothing went to Edmond. Lanes on the day: on core `async-core-io-2026-10-06` with S6.3 in,
        `pocs-dbg` 834 PASS, 8 SKIP, 47 XFAIL, nothing unexpected; on the previous core with S5.3
        in, `pocs-asan` 778 PASS, 27 SKIP, 73 XFAIL, the only unexpected results `edge_cases/016`,
        `017` skipping for a core built here without zlib. 10 seeds before
        S5.3 and 3 after: no crash, assertion, leak or oracle abort, and no partial-deadlock
        warning outside `collector/`; some seeds see an `--XFAIL--` test pass in a random order
        (`exec/002`, `dns/003`, `io/094`).
- [x] S7.3 Futures, tokens, Timeouts and `await_*` blocks; the holders' table of S7.md 10 (once
      S5.3 is on `main`).
      done: S7.txt's S7.3 tests pass on debug and ASAN, those waiting for S4.5 and S6.5 with
        `--XFAIL--` naming them
      handoff: done 2026-10-07: a future event is a walk node counted by `base.ref_count`, and
        `Future`/`FutureState` report through `async_future_collector_references()` instead of
        `get_gc`; FUTURE, the token kinds and the `await_*` trigger report their target as owned (a
        reference the wait took in C), a Timeout token makes its waiter live, the iterator kind stays
        NULL. S6.5's signal watch seeds its Futures live (`async_signal_collector_seed()`, agreed with
        S6); the reactor lists seed nothing. The walk finds nothing once the request shuts down (the
        engine's destructor pass). The fuzz oracle now checks the event wakes, excusing only a
        completer in the bailout; `TrueAsync\Test\mark_found()` and `collector/040` test it in child
        processes. Tests `collector/026`-`041`, each new report killed by a mutation. Critic three
        rounds; node size kept (simple code over bytes); nothing went to Edmond. Lanes on the day,
        core `async-core-io-2026-10-06` with S6.5 in: `pocs-dbg` 895 PASS, 8 SKIP, 25 XFAIL, one
        `dns/003` order failure that three reruns pass; `pocs-asan` 880 PASS, 25 SKIP, 24 XFAIL,
        nothing unexpected; 5 seeds: no oracle abort, crash or leak, one `reactor/011` timeout under
        the full load that 5 seeds alone pass, and `--XFAIL--` tests passing (`io/094`, `io/100`).
- [x] S7.4 The `cancel` policy; B6 (S7.md 11).
      done: S7.txt's S7.4 tests pass on debug and ASAN; B6 in `dev/BENCHMARKS.md`
      2026-10-07: `true_async.partial_deadlock=cancel` warns once and cancels every parked coroutine
        but main, as `registry_cancel()` (protection cleared, handed out for the oracle); main stays
        parked for the global deadlock's `DeadlockError`, since its uncaught cancellation ended the
        script silently. The back-off resets on a first warning or a first cancel, not on a repeated
        cancel. Tests `collector/042`-`047`, each rule killed by a mutation; `collector/020` checks
        the refusal with `kill` (DECISIONS). Critic one round: main, the back-off and a useless
        `graceful_shutdown` guard fixed. B6 on the debug build: 10 000 coroutines in pairs 24-26 ms,
        4.9 MiB; with a shared graph of 1M objects 238-262 ms, 76 MiB; 10 000 on timers 0.5 ms.
        Lanes on the day, core `1ee473ff67b`: `pocs-dbg` 902 PASS, 8 SKIP, 25 XFAIL; `pocs-asan`
        886 PASS, 25 SKIP, 24 XFAIL, nothing unexpected; 5 seeds: no oracle abort, crash or leak, only
        `--XFAIL--` tests passing (`io/094`, `io/100`).
- [ ] S7.5 Stage review: Critic after S7.2-S7.4, coverage of the collector, Mull on the stage
      diff, the fuzz oracle over 100 seeds.
      done: Done when of S7 holds on the day, the channel case aside (S9); survivors killed or
        explained
- [ ] S7.6 Security pass by `dev/SECURITY.md`.
      done: a journal entry per checklist item; findings fixed with a test or recorded

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
