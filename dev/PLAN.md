# PLAN — TrueAsync rebuilt as a regular PHP extension

Updated: 2026-10-13 · Active: per stage, under its `Tier:` line (Parallel tracks)

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
├── src/            true_async.c, scheduler.c, coroutine.c, exceptions.c, reactor.c, future.c,
│                   await.c, io_provider.c (with IO chaos), collector.c, ... (+ .h, .stub.php);
│                   internal/: circular buffers and the scheduler fuzz hook
├── tests/          <group>/*.phpt; lists/ (frozen per stage)
├── bench/          benchmark scripts of dev/BENCHMARKS.md
├── tools/          core build (async-core-io + ior), runner, coverage, Mull config
├── .github/workflows/
├── dev/            INDEX, WORKFLOW, PLAN, PRINCIPLES, DECISIONS, RFC-CHANGES, reviews/
└── README.md, CHANGELOG.md, LICENSE, .clang-format, .gitignore
```

## Fog

- Threads (S10, not in the first version): multi-thread ZTS needs a loop per thread on Poll/Ring, and
  `SignalHandle` is CLI-only under ZTS (gist:2836-2845). TrueAsync's `Thread`, `ThreadPool`,
  `ThreadChannel` and a Future sent to a thread return as an extension-level design, built the way
  ext/parallel is, with three small core needs: a drain of child threads at module shutdown,
  `sapi_module.thread_init` for Windows, WeakReference transfer (`dev/plans/S10.md` section 7).
  `signal/008`, `009`, `012` also need a process-wide signal owner, a request to bukka; until then
  `Async\signal()` on Unix outside the main thread of a threaded CLI is not refused (on Windows it
  is).
- Pool helpers (S9.32, not in layer 5): a `$pool->use(fn($r) => ...)` that releases in its own `finally`, so a
  coroutine that ends holding a resource cannot leave it out for good (TrueAsync has none; probe `p5`), and a
  cancellation token as a second parameter of `acquire()` beside TrueAsync's `int $timeout`
  (`dev/plans/S9-pool.md` section 8, item 10).
- Per-coroutine output buffers (S10, not built, Edmond 2026-10-08): the follow-up RFC promised by the
  scheduler RFC (`scheduler_rfc.md:696`) provides them; option C of `dev/plans/S10.md` section 2
  is the extension-level path if they are wanted before that. `output_buffer/001`-`006` stay excluded.
- PDO pool (S10, not in the first version): after the first version, a PDO RFC adds hooks to
  `ext/pdo` (a connection resolver, `stmt->pooled_conn`, per-binding error state), then the extension
  builds the pool over S9's `Async\Pool`, which comes first (`dev/plans/S10.md` section 5).
- pgsql and pdo_pgsql (S10, not in the first version): after the first version, both drivers wait
  through `php_io_poll()`, with cancellation and a guard against two coroutines on one `PGconn`, in
  our own php-src PR once the IO hooks RFC is merged; no PostgreSQL fixture in CI until then
  (`dev/plans/S10.md` section 6).
- `SIGWINCH` on Windows (S10.5, Edmond 2026-10-09: not now): `Async\signal(Signal::SIGWINCH)` is
  accepted and completes only through its cancellation. TrueAsync's libuv completes it on a console
  resize with two thread-pool work items (`QueueUserWorkItem()`) and a hook on conhost's window
  events (`SetWinEventHook()`, `NtQueryInformationProcess(ProcessConsoleHostProcess)`,
  `libuv/src/win/tty.c:2375-2442`); the follow-up would do the same.
- A curl multi shared per thread (S8.5, review M8): under hooks each `curl_exec()` runs a multi of its
  own (`ext/curl/interface.c:2653`), so two handles share no connection, as without the extension;
  TrueAsync shares one multi per thread (`ext/curl/curl_async.c`). A core request if reuse is wanted.
- `Zend/zend_types.h` C4146 under `/sdl` on Windows (S10, W9): our build has no `/sdl`; a php-src fix
  if a build needs the flag.
- Minor items of S10's inventory (`dev/plans/S10.md` section 9): phpdbg's `run` does not drive the
  scheduler. Whether our build can hit the bug that the fork's opcache JIT fix `#118` addresses is
  not checked. `prctl(PR_SET_VMA)` fails on every fiber stack and the failure is not cached
  (`dev/plans/S3.md` section 10); the core request is not written yet. The fork's blocking writer for
  stdout and stderr and its `zend_try` in the CLI option handlers are fixes outside both RFCs.
- An exported C API for other extensions (TrueAsync Server and others): the fork's extended
  `zend_async_API` (events, wakers, `resume_when`) may move into the extension. Not decided; S3
  keeps internal structures open to it, the first version does not promise it.
- php-src: an `exit()` in a destructor loses to the exception handling around it (S8.1d's second Critic,
  shown on a debug build without fibers): when live temporaries are freed before a `finally` block, the
  exit is held there and a `return` in that block drops it; a destructor that throws while an exit is
  pending replaces it, and an exit with an exception pending makes `UnwindExit::$previous` (a
  deprecation). A separate php-src fix if wanted; the scripts are in the S8.1d PR text's "Not changed".
- FileSystemWatcher on macOS and the BSDs (S9 layer 7, not in the first version): no CI line builds the extension
  there. The path is a kqueue descriptor on the POLL op with `EVFILT_VNODE` and a directory listing diffed per change
  to name the entry, as TrueAsync's reactor does for the BSDs (`dev/plans/S9-fs-watcher.md` section 3).
- Stream concurrency: the IO hooks freeze a whole stream (review B1); today's TrueAsync allows
  duplex and close-from-another-coroutine.

## Open questions

Waiting for Edmond's call; nothing here is being worked on.

- A channel `recv()` whose coroutine was cancelled, and whose owner scope is cancelled before it runs, throws
  `ChannelException` ("Channel closed: owner scope disposed") with the cancellation as its previous, so a
  `catch (Exception)` swallows the cancellation (found in S9.41, not fixed there). By S9.36's rule the
  cancellation would go on top. Edmond's call.

- `gc_collect_cycles()` called while a GC run is in progress (its coroutine parked waiting for destructor
  iterators) returns 0 at once, from any coroutine: the `gc_active` check meant for a call from inside the run
  (S14.6, Sage). A programmer forcing a collection gets none and no sign of why. Option: await the active run
  unless the caller is one of its destructor iterators (needs the GC to track them). Edmond's call.
- A C function that releases something after it has taken a value can lose that value to a cancel of the caller
  when the release suspends: a destructor that waits, or the core's GC wait, which since S9.38 happens only to a
  coroutine 100 000 roots past the queued run without yielding (S9.33 fixed the sites that change shared state).
  Option: let that wait defer the cancellation to the caller's next suspend, which needs a delivery point beside
  `protect()`'s and does not cover a destructor that waits (`SECURITY.md`, Open findings). Edmond's call.
- A forked child whose first reactor call is D16's arm after an `exit()` in main's script runs the rebuild with no
  frame (S14.5): a `SignalHandle` or `Io\Poll\Context` the rebuild cannot make (descriptors exhausted) throws
  from a PHP call there, a fatal error that skips every `finally`. The other throws of that rebuild are fixed
  (`reactor/054`, `signal/043`). Options: leave it (it needs a held `signal()`, a fork, an `exit()` and exhausted
  descriptors at once), or give the arm a frame or move it to the scheduler's loop, which has one. Edmond's call.
- The gate "the event embedded in a coroutine" of `tools/check-gates.py` (S3.md section 11) greps
  `->event.` and lets through only pointers named `*scope`: it failed main on a scope's own event
  twice (S9.2 `scope->event.`, fixed in the gate on 2026-10-07; S9.24 `level->event.`, fixed by
  walking the chain on `scope`), and `ancestor->event.` would fail it again. The other option:
  check that `struct _async_coroutine_s` in `src/coroutine.h` declares no `async_event_t` member,
  which the compiler then enforces for every access. Edmond's call (a change of S3's gate list).
- A `signal()` Future no coroutine awaits, when the script ends by itself (S6.8): the script waits
  for the signal, as TrueAsync; after `exit()` or an uncaught exception it ends. Edmond 2026-10-07:
  the async collector should close such a watch; to be thought over in another task.
- A coroutine woken in its own suspend tick runs on ahead of queued coroutines (S6.8, the Critic on
  `curl/010`): its suspend polls the reactor every 100 ms, and a ready socket lets it continue with
  no switch, as TrueAsync's fast return path (`scheduler.c:1578-1584`). A coroutine that spends
  over 100 ms between its IO ops on a socket that is always ready keeps the others queued until it
  ends. The other option: run on only when the queue is empty, else go to its back (one switch per
  such wake, only when someone waits). Edmond's call (a departure from TrueAsync).
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
  its block and mask around a coroutine's suspend), an S8 change-request candidate. The core takes the
  same block around tick functions (`Zend/zend_vm_def.h:8105-8109`; review section 5, S8.5), so a
  tick function that suspends is the same case, inferred: a probe did not show it. Edmond's call.
- A POLL with a near deadline on ior's IOCP backend (the Critic on the `feof()` fix, 2026-10-08): ior
  arms the ring's linked timeout when it issues the op and hands the poll to its WSAPoll thread with no
  check at issue (`ior_iocp.c` `issue_poll()`, `arm_link_timeout()`), so a timeout a few microseconds
  ahead can in principle complete as TIMEOUT on a ready socket (not seen); io_uring checks readiness at
  submit. The provider answers a passed deadline itself; a readiness probe at issue in ior, or in the ring as `php_io_ring_group_probe()`
  does for ANY members, is a change to bukka's code. Edmond's call.
- The last from_main call with a bailout still makes a new main coroutine (S10.6): a bailout in the
  destructors brings it to the call after them, which the scheduler cannot tell from a shutdown
  function's call, so an out-of-memory error in a destructor of the store's pass at shutdown is printed
  twice (`dev/SECURITY.md`, 2026-10-09). A fix needs the core to say which call it is (a flag before
  `main/main.c`'s last call, or an argument), an RFC change. Edmond's call.
- `Scope::inherit()` under a closed scope (the Critic of S9.20) is allowed, as TrueAsync's, and its
  coroutines are out of reach of any later cancel of the closed scope's ancestors; `spawn()` in the closed
  scope itself is refused, the new child accepts it. Edmond's call.
- `$channel->recvAsync()->map(...)` (the layer Critic of S9.20): the temporary Future is its event's only
  holder, so it takes the receive and the chain with it, and the child stays pending for ever. TrueAsync
  holds the source object from the child's subscription until the source completes (`future.c:1752-1756`);
  that fix here (the child holds its source) makes a pending chain nobody holds wait for the cycle
  collector (`channel/108`, `future/112` expect an immediate release). Handing
  the event to held children at the source's death works one level deep only (the second Critic).
  Edmond's call.
- A scope's second cancel while its own coroutines unwind (the second Critic of S9.20): `scope_is_completed()`
  counts a cancelled scope as completed, so the scope closes and its finally handlers start at once, as
  TrueAsync's (`scope.c:964-971`); S9.20 keeps its parent's cancel from doing that to a child. Should the
  scope's own second cancel wait for its coroutines too? Edmond's call.
- A child scope made under a cancelled scope after its cancel (the re-check Critic of S9.20): the scope's
  second cancel closes the scope and skips the child, whose coroutines run on, as TrueAsync's; so does a
  grandchild made under a cancelled child that still unwinds, and a coroutine a finally handler spawned and
  left running once its run ended (the S9.23 code Critic), which the deadline does not reach either. Since
  S9.23 a finally run's scope carries `ASYNC_SCOPE_F_FINALLY_RUN`, so cancelling such a child no longer stops
  a running handler (`scope/140`). Edmond's call.
- The S9.24 deadline flag is the scope's, for its whole subtree, so a survivor below it (a member of a
  cancelled child that goes on) loses its finally handlers once another member was interrupted; a per-scope
  flag would keep them (the S9.24 re-check Critic). Edmond agreed that a safe scope's zombies keep theirs
  (2026-10-09 10:04); whether a survivor below an interrupting deadline keeps them too is his call.
- A coroutine spawned into a scope after its deadline fired (the scope is cancelled, not closed), or into a
  `Scope::inherit()` made under it then, runs unbounded while none of its finally handlers is called (the
  S9.24 re-check Critic; read from code, not run). Close the scope at the fire, or skip only the targets
  that existed at it? Edmond's call.
- The collector gives an armed dispose timer no live reach on a cancelled scope (`src/scope.c:960-961`, S9.23),
  though the fire's cancel can still reach a member in a grandchild made under it after its cancel
  (`scope/172`): with no object left, a member parked on nothing else could be reported or cancelled as a
  deadlock before the fire (the S9.24 re-check Critic; read from code, not run). Check with a test, then
  keep only CLOSED in that exclusion.
- A handler that catches the deadline's cancellation and waits again outlives one fire (the S9.23 design
  Critic); a second `disposeAfterTimeout()` stops it (`scope/150`), and D16's exit repeats an uncatchable one.
  A destructor of what a handler holds that starts after the fire (the fire stopped the handler, or the run
  started after it) outlives it the same way (S9.25, the design Critic), while one whose run's worker the fire
  cancelled before it started ends the request (next item): after a deadline the same capture either waits
  unbounded or fails. Bound it (the Sage leans so; a cancel set aside around `__destruct()` does not reach its
  wait, so it needs a new rule) or leave it? Edmond's call.
- A coroutine cancelled before it ran finishes with no stack (`coroutine_finish_unrun()`, as TrueAsync's
  IGNORED path), so a destructor that its release runs cannot wait: one in what a `spawn()` closure captured
  fails with "Cannot switch coroutines in the current execution context", and one of a finally run whose
  worker a fire cancelled before it started fails with the scheduler-context error (S9.25, probes on the
  debug build).
  Running such a coroutine's finish on a context of its own is a scheduler change. Edmond's call.
- Fiber stacks on Windows (the Critic on `scope/058`): `zend_fiber_stack_allocate()` commits the
  whole 2 MB stack (`VirtualAlloc(MEM_COMMIT)`), as TrueAsync's core and PHP's `Fiber` do, so 20 000
  suspended coroutines need about 40 GB of commit; `collector/064` skips on Windows, and `scope/058`
  suspends fewer to fit there. Fix proposed upstream as php/php-src#24190 (2026-10-08, branch
  `win-fiber-stack-on-demand`): reserve the stack, commit 32 KiB, let the kernel grow it; 2074 to
  41 KiB per suspended coroutine on Edmond's PC. Once it is in the pinned core: `scope/058` back to
  20 000 and `collector/064` unskipped on Windows.
- `PHP_BUILD_SYSTEM` on a localized Windows (2026-10-08): php-src `win32/build/confutils.js:134`
  writes `os.Caption` into `main\config.w32.h` in the ANSI code page, and `/utf-8 /WX` stops every
  file on C4828. `tools/windows/build-core.bat` strips the non-ASCII bytes (e28ec4c); the fix itself
  is a one-line change for an official php/php-src PR. Edmond's call.
- The scope of a Future's `map()`, `catch()` and `finally()` callbacks (S9.12, the Critic): S5's
  chain drain runs them in the global scope, so their `current_context()` is the root context
  (`context/039`); TrueAsync runs the mapper in the scope captured at `map()` (`future.c:1593-1600`).
  Following it changes S5's drain (a callback per subscriber scope); Edmond's call.
- The report of a dropped group's unhandled errors (S9.28, note section 5, step 5): with no handler it
  prints `Fatal error: Uncaught Async\CompositeException in [no active file]:0` and no inner error, so the
  programmer cannot tell which task failed (`task_set/011`); an unused rejected `all()` Future warns
  `Unhandled exception in Future: ;` for the same empty composite message (`task_group/083`). The composite could carry the first error's
  message, or the report could print each inner error; Edmond's call.
- `Scope::awaitCompletion()` of a parent returns while a cancelled child scope still unwinds (S9.28): a
  cancelled scope counts as completed (`scope_is_completed()`, TrueAsync's `can_be_disposed`), so a group
  dropped in a coroutine of `$scope` with a task not yet ended lets `$scope->awaitCompletion()` return before
  that task ends and before the group's finally handlers run (probe
  `/mnt/project-files/s9/probes/s9.taskgroup/s928_await_completion_cancelled_child.php`);
  `awaitAfterCancellation()` waits for them. Edmond's call.
- A group's closing after an `exit()` (the layer Critic of S9.30): a finally run whose worker the exit cancelled
  before it ran ends at the microtask's last release in the scheduler's tick, so the closing runs there and
  releases the group's results and its own scope's values in scheduler context; a result whose `__destruct()`
  waits ends the request with `Uncaught Error: Cannot switch coroutines in the current execution context` (probe
  `/mnt/project-files/s9/probes/s9.taskgroup/s930_closing_after_exit.php`, debug build). S9.25 accepted that tick
  release after an exit for a run's handlers and target only; the group makes its whole closing such a target.
  Move the closing out of the tick, or accept it? Edmond's call.

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
| S10 | after S6 | its outcomes are RFC requests, upstream fixes or extension code (`dev/plans/S10.md`); the PDO pool needs S9's pools, the thread pool the Fog's ZTS line |

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

## S1 — Core branch `async-core-io`  [x]

Goal: the php-src the extension is built on: master + the two PoCs + ior, nothing else.

- [x] S1.1 `~/php-src2` worktree; ior `2fb12e8ce01` built into a Release and an ASAN prefix.
- [x] S1.2 Baselines of the scheduler PoC and the IO hooks PoC with the same configure line.
- [x] S1.3 `async-core-io` from `async-core`, master and the IO hooks PoC merged; diffed against S1.2.
- [x] S1.4 Rules in `dev/WORKFLOW.md` ("Branches", "Pinned core"): a core update is a new merge-only branch.
- [x] S1.5 Windows: `async-core-io` built with nmake (Debug_TS) and ior for IOCP (as the PR's
      `build-ior-windows` action does); the three suites run; per-test diff against Linux.
      handoff: done 2026-10-07 on core `8f89755d2b1`, Edmond's PC (VS 2026 Build Tools, php-sdk
        2.8.4), with `tools/windows/` (README, build-ior.ps1, build-core.bat, run-suites.bat). The
        suites (`test_scheduler`, `poll`, `streams/hooks`, 234 tests): Linux debug 229 PASS, 5 SKIP;
        Windows Debug_TS 176 PASS, 55 SKIP (each test's own Windows reason: no pcntl or posix,
        POSIX pipes and descriptors, no Edge registrations on IOCP), 2 passed on a retry
        (`provider-completes`, `registrations-edge-select`), 1 FAIL (`091_command_line_code`, the
        test's path separator; for the next core update); 4 tests Linux skips pass on Windows.
        `pocs-win` had 26 (Debug_TS) and 25 (Release_TS) unexpected on `d196cbd`: xfail tags of
        passing tests dropped, three pipe tests tagged for S6.10, tests whose subject Windows lacks
        skipped, two timing-dependent own tests fixed (DECISIONS 2026-10-07); `config.w32` gains
        `src\scope.c`, missing since S9.2. Left for S6.10 and the core: `dev/handoff.md`, S1.

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

## S4 — Reactor on Poll, Poll additions and Ring  [x]

Goal: the scheduler's idle wait and timers on one per-thread `php_io_queue` (the Ring when built
with ior, the Poll queue otherwise), coded only against `php_io_queue_ops`; the S6 provider
submits to the same queue, so completion dispatch is designed here once for both.
Done when: S3 + S4 lists pass; `delay(1000)` costs under 50 ms of user CPU; a test-only C
function wakes the loop from another pthread (through wake descriptors of the reactor's own until
the core exports the pair behind `NotifyHandle`, `RFC-CHANGES.md` 1).
Tier: T2. Roles: Critic on S4.1, Critic after S4.2 and after S4.3.

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
- [x] S4.6 Stage review: Critic after S4.2-S4.5, coverage of the reactor code, Mull on the stage
      diff, fuzz over 100 seeds; the Ring's lateness with many Timer ops (`dev/BENCHMARKS.md`,
      S4.4: a timer heap of the reactor's own, as libuv's, or a core change to the Ring's backlog).
      done: Done when of S4 holds on the day; survivors killed or explained
      handoff: done 2026-10-07: a timer heap in `src/reactor.c` (Timer ops never reach the queue;
        the wait gets the nearest deadline): the Ring's lateness was a kernel timeout per op, not
        the backlog walk; 10 000 delays wake 0.02-0.05 ms late on it instead of 46-64 ms
        (`dev/BENCHMARKS.md`). The Critic's stage round (4 major, 7 minor) and the merge round with
        S5.4 and S6.3 fixed or recorded (`dev/plans/S4.md` "As built (S4.6)"); the fork check is
        void and runs at the poll too. Fuzz 100 seeds: `reactor/011` fixed, 16 order artifacts
        read. Mull over `src/reactor.c`: 82 mutants, 20 survived, one gap closed by `reactor/041`,
        19 explained. Coverage: `src/` 92.8 %, the reactor's 73 uncovered lines explained. Done
        when on core `8f89755d2b1`, 2026-10-07: all lists on top of S5.5 `pocs-dbg` 948 PASS, 9 SKIP, 12
        XFAIL; `pocs-asan` 932 PASS, 26 SKIP, 11 XFAIL; `delay(1000)` 18-22 ms of user CPU with the
        process's start (an empty script 11-19 ms); `reactor/026`, `027` wake the loop from another
        pthread; `pocs-win` left to CI.
- [x] S4.7 Security pass by `dev/SECURITY.md`.
      done: a journal entry per checklist item; findings fixed with a test or recorded
      handoff: done 2026-10-07 (`dev/SECURITY.md`, the S4.7 entries): fixed with a test each, D16's
        graceful exit became the outcome of a coroutine woken in its own tick, and `serialize()` of
        that object crashed the process (`reactor/043`); `delay()` past 2^31 s wrapped on Windows,
        whose `timeval` seconds are 32-bit (`reactor/042`). The wake pair is made close-on-exec
        atomically with `pipe2()` where Linux's eventfd is absent, and not inheritable on Windows.
        Recorded: a thread that fires without pause does not hold the poll (measured), a `finally`
        that respawns keeps D16 refiring, the trigger's contract for a future remote holder, the
        timer heap's 2^31 bound; php-src's `socketpair_win32()` binding `INADDR_ANY` went to Edmond.
        On core `8f89755d2b1` after S7.6 and S9.1 `pocs-dbg` 972 PASS, 9 SKIP, 103 XFAIL, `pocs-asan`
        955 PASS, 30 SKIP, 99 XFAIL, 6 left out by `core:` tags, 0 unexpected. Closes S4 and S5.

## S5 — Futures, timeouts and combinators  [x]

Goal: the API the ported tests use everywhere.
Done when: S3–S5 lists pass, including the `await` group's combinator tests.
Tier: T2. Roles: Critic on S5.1, Critic after S5.2.
Notes: dev/plans/S5.md

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
- [x] S5.5 Stage review: Critic after S5.2-S5.4, coverage, Mull on the stage diff, the S3.md section
      12 benchmarks of `await_*` (N in 1, 2, 8, 100, 10 000).
      done: Done when of S5 holds on the day; survivors killed or explained; results in
        `dev/BENCHMARKS.md`
      handoff: done 2026-10-07 (S5.md section 11): the Critic's findings fixed (a refused scheduler
        registers no `Future`/`Timeout`, a Timeout's throwing subscriber left waiters parked) or
        documented; coverage of `src/` 94.1 %, the lines left listed with a reason; Mull 213 mutants on
        the three S5 files, 14 survivors explained; B9-B11 cost fewer instructions and allocations than
        TrueAsync at every N, no inline records. Found and fixed: a coroutine on a stack below the
        core's minimum crashed (`scheduler/106`, `056` at 32 KiB). On core `8f89755d2b1` after S6.7
        and S7.4 `pocs-dbg` 943 PASS, 9 SKIP, 12 XFAIL, `pocs-asan` 927 PASS, 26 SKIP, 11 XFAIL,
        6 left out by `core:` tags, 0 unexpected.
- [x] S5.6 Security pass by `dev/SECURITY.md`.
      done: a journal entry per checklist item; findings fixed with a test or recorded
      handoff: done 2026-10-07 (`dev/SECURITY.md`, three entries): fixed with a test each, a
        Traversable whose items hold its iterator coroutine leaked the wait (`await/140`), a message
        property hook leaked its value (`future/122`), and removal from a callbacks vector searched,
        so N copies of one Future in two waits or N `Async\signal()` Futures left in O(N^2) (200 000
        copies 31 s): every callback keeps its index in the vector (S3.md section 12; `await/141`,
        `signal/024`, `internal/063`; B11-10000 22,032 to 7,052 instructions a waiter). Accepted
        with a reason: recursion of long releases, the drain under graceful shutdown, 32-bit
        counters, a hook in the release. A php-src leak in `zend_exception_set_previous()` went to
        the coordinator. On core `8f89755d2b1` after S4.6
        `pocs-dbg` 953 PASS, 9 SKIP, 12 XFAIL, `pocs-asan` 937 PASS, 26 SKIP, 11 XFAIL, 6 left out by
        `core:` tags, 0 unexpected.

## S6 — IO hooks provider  [x]

Goal: blocking PHP functions suspend the coroutine through `run()` on the reactor; signals and
children through the same contract.
Done when: S3–S6 lists (from `sleep`, `io`, `stream`, `socket_ext`, `dns`, `curl`, `exec`,
`mysqli`, `pdo_mysql` without the pool, `signal`) pass on debug and ASAN; IO chaos runs clean
over 100 seeds; tests that fail because of the hooks design are listed against the review item;
`dns` counted only on the Ring configuration (the Poll queue answers Unsupported for lookups).
Tier: T2. Roles: Critic on S6.2, Critic after S6.7 (S6.8).
Active: none; stage closed with S6.10

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
- [x] S6.7 IO shutdown windows (the `ts_suspend` NULL case), the seven core-tree tests,
      `io/035`-`037` (Async in a `php -r` child), `io/094`, `095` (streams fixes), the `run()`
      bailout tests S6.3 moved here (`dev/plans/S6.md` section 12), every list
      run, the by-design failures tagged `core:` against their review items, the RFC requests of
      the note's section 14 in `RFC-CHANGES.md`.
      done: Done when of S6 holds except the review
      handoff: done 2026-10-07 on core `8f89755d2b1` (`async-core-io-2026-10-07`: `php-src-fixes`
        `6e9d801dcc5`, `io-hooks-fixes` `c43e1d5797a`, `async-core` `ae85ef88d00`), with S7.4: `pocs-dbg`
        921 PASS, 9 SKIP, 12 XFAIL; `pocs-asan` 905 PASS, 26 SKIP, 11 XFAIL; 6 left out by `core:` tags;
        nothing unexpected; the bridge 23 of 23 on both. The core launches the scheduler for
        `php -r`, `-B`, `-R`, `-E` (one main coroutine for `-B` to `-E`, the Critic); `io/094`, `095`
        pass with `php-src-fixes`; the seven core-tree tests read `$TRUE_ASYNC_CORE_SRC`; a DNS
        lookup yields before its submit (`dns/003`). By design: `io/096`, `098`, `exec/025`
        `core:12`, `io/081`, `084` `core:11`; `RFC-CHANGES.md` 7-12 filed. Own tests
        `io_provider/016`-`018`; the `ts_suspend` NULL case has no path (the Sage). Edmond
        approved the `async-core` commit for php/php-src#22561 the same day.
- [x] S6.8 Stage review: Critic over S6.3-S6.7, coverage, Mull, IO chaos over 100 seeds.
      done: findings fixed or answered; chaos clean over 100 seeds
      handoff: done 2026-10-07 on core `8f89755d2b1` (unchanged), on top of S9.2: `pocs-dbg` 1028 PASS,
        9 SKIP, 67 XFAIL; `pocs-asan` 1011 PASS, 30 SKIP, 63 XFAIL; 7 left out by `core:` tags; nothing
        unexpected; `src/` coverage 94.2 % (5799 of 6157). The Critic's findings fixed with tests
        (`io_provider/019`-`025`, `signal/024`-`026`): the `gc_new_coroutine` slot, a Done POLL or ANY
        under a cancellation answers FAILURE, a Ring op kept in flight is drained after every park,
        `signal_forward()` defers signals as Zend does; `stream_select()` with an except set is
        `RFC-CHANGES.md` 13 (`core:13`). Mull: 87 mutants, 35 survived, each answered (S6.md 17).
        A held `signal()` Future no longer keeps the script alive after `exit()` or an uncaught
        exception (Edmond's ruling, `signal/027`-`030`); a script ending by itself waits, as
        TrueAsync (Open questions). IO chaos (`random:<seed>:io`, C1-C3) fails its known answers
        armed and passes them unarmed; 100 seeds over 340 tests: no crash, leak or new diagnostic,
        23 seeds flag only `io/100` (XFAIL) passing under their order. `curl/010` waits for the
        other coroutine through the server: its order depended on load (DECISIONS). The bridge was not rerun:
        the core did not change.
- [x] S6.9 Security pass by `dev/SECURITY.md`.
      done: a journal entry per checklist item; findings fixed with a test or recorded
      handoff: done 2026-10-07 on core `3af71f889e6` (`async-core-io-2026-10-07-5`): `pocs-dbg` 927 PASS,
        9 SKIP, 12 XFAIL; `pocs-asan` 911 PASS, 26 SKIP, 11 XFAIL; 7 left out by `core:` tags; nothing
        unexpected.
        Journal entries in `SECURITY.md`. Fixed with tests: a number the reactor blocked again before a
        poll stayed blocked after its watch when the script had blocked it before `signal()`
        (`signal/031`, the registry's `reblocked`); the drain after a park no longer waits for every
        Ring record when an op has neither stream nor handle. A watch that goes inside a pcntl handler
        is blocked again by `pcntl_signal_dispatch()`'s mask restore, a php-src bug: fixed on
        `php-src-fixes` `74a581afc06` (branch `pcntl-dispatch-keeps-handler-mask`, its text in
        `notes/` for Edmond), `signal/033` XFAIL until a core update merges it. Accepted, waiting for
        `RFC-CHANGES.md` 5 (TrueAsync's `zend_async_sigaction_fn`): a delivery between `pcntl_signal()`
        and the next poll goes to the handler alone (`signal/032`), a `proc_open()` child inherits the
        reblock. CI's MySQL image pinned by digest. The Critic twice, the Sage on two disputes; Edmond
        asked for the check against PHP's global signal handler: the forward reaches `SIGG(handlers)`,
        FPM's included.
- [x] S6.10 Windows (once S1.5 gives a Windows agent): the `proc_open()` pipe core commit
      (`dev/plans/S6.md` section 9), the Windows lane's socket expectations from S6.4.
      done: `sockets` and `openssl` load in `pocs-win`; no `xfail-on` tag or `--XFAIL--` names
        S6.10; the frozen `skip-on:pocs-win(...-until-S6.4)` and `(...-until-S6.5)` tags skip nothing
      handoff: done 2026-10-08 on core `d3681ac7d41` (`async-core-io-2026-10-08-2`: bukka's head
        `566a6833eb5`, `io-hooks-fixes` `424116620a7`, `php-src-fixes` `acc6b34faa3`) and ior
        `d46649f6425` (`release-handle` in true-async/ior): `pocs-dbg` 1317 PASS, 11 SKIP, 41 XFAIL;
        `pocs-asan` 1293 PASS, 36 SKIP, 40 XFAIL; 7 left out by `core:` tags; 0 unexpected. On
        Edmond's PC, `pocs-win` Release_TS and Debug_TS each 1225 PASS, 102 SKIP, 42 XFAIL, 0
        unexpected (Debug_TS twice), with `sockets`, `openssl` and `curl` loaded (curl 69 PASS).
        S6.md 9.1 as built: overlapped named pipes when the extension asks at MINIT
        (`RFC-CHANGES.md` 18), each pipe handed to a child taken off the Ring's port before
        `CreateProcessW()` (`ior_release_handle()`) and ior's filter of foreign packets kept,
        Edmond's choice; ior also completes a partial message or datagram read with its bytes. Own
        tests `io_provider/028`-`035`. Changed: `signal/031`, `032` (bukka's pcntl keeps a watched
        signal blocked, `RFC-CHANGES.md` 5's main part), `io_provider/026` (a close before the
        peer's TLS accept broke it on Windows), `dns/005` skips on Windows (an empty host name
        resolves there, as without the extension); `stream/001`, `002` XFAIL by design;
        `collector/027`, `036` and `spawnWith/013` changed (their output order rested on timer
        timing, which load on Windows broke). PR texts for bukka and libior/ior in
        `/mnt/project-files/notes/s6-10/`, Edmond opens them. Closes S6.

## S7 — Async object collector  [x]

Goal: find coroutines that can never wake and the async objects only they keep alive: partial
deadlocks (a cycle of waits while other coroutines run), a Future nobody can complete, a channel
with no senders left; report them and resolve them by policy (a cancellation into the waiters, as
the global deadlock's, or a report only; `dev/plans/S7.md` section 6).
Done when: tests for each case pass on debug and ASAN; waits on IO, timers, signals and
cross-thread wakeups are never reported; a run over 10 000 parked coroutines costs a measured
time, recorded; scheduler fuzz over 100 seeds reports no false positives.
Tier: T2. Roles: Critic on S7.1, Critic after S7.4 (S7.5).
Notes: dev/plans/S7.md
Active: none; the channel case came with S9.19 (`channel/125`, `126`), the fuzz over it with S9.20

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
- [x] S7.5 Stage review: Critic after S7.2-S7.4, coverage of the collector, Mull on the stage
      diff, the fuzz oracle over 100 seeds.
      done: Done when of S7 holds on the day, the channel case aside (S9); survivors killed or
        explained
      2026-10-07, core `8f89755d2b10`: the Critic over the stage found a false report (a profiler's
        replaced `zend_execute_ex`, `collector/054`), a fatal error the automatic run could raise at
        `memory_limit` (it now stops and finds nothing, `collector/057`; the Sage chose that over
        persistent tables) and a stale 3.4 (seeding kept to the sources themselves, the Sage); the
        coverage showed a coroutine whose body is a method never found (`zend_call_function()`'s
        pinned `$this`, now counted, `collector/050`, `052`, `061`). Tests `collector/048`-`061`.
        Critic two rounds on the fixes. Coverage `pocs-dbg-cov` 92.5 % of `src/` (5507 of 5954),
        `collector.c` 420 of 442 before `061`: the rest are asserts, the oracle's aborts (child
        processes), out-of-memory exits and the unwinding-frame skip no known path reaches (S7.md
        3.2). Mull scoped to `tests/collector/` (DECISIONS): `collector.c` 62 mutants, 59 killed, 3
        equivalent (a record counter only tested against 0; the wake-edge pre-pass, an optimisation
        the spread repeats); the S7 lines of the hooks 6 mutants, the survivor killed by `053`. 100
        seeds over 955 tests: no oracle abort, no false report; 23 seeds counted failed, for `io/100`
        passing while `--XFAIL--` (23 seeds) and one `reactor/011` timeout under the full load (seen
        in S7.3 too); the new diagnostics are order artifacts. All 60 collector tests also pass with `zend_execute_ex` replaced. Lanes on
        main with S4.6 and S5.6: `pocs-dbg` 967 PASS, 9 SKIP, 12 XFAIL; `pocs-asan` 951 PASS, 26
        SKIP, 11 XFAIL, 6 left out, nothing unexpected. B6 again: 28.6 ms, 255.1 ms, 0.5 ms (one run each).
- [x] S7.6 Security pass by `dev/SECURITY.md`.
      done: a journal entry per checklist item; findings fixed with a test or recorded
      2026-10-07, core `8f89755d2b10`: the pass over the stage's four commits, about 25 scripts on
        the debug and ASAN builds, found no use after free; the automatic run still died at
        `memory_limit` where the wake edges (coroutines awaiting the same dead Futures,
        `collector/063`) or the candidates' registration (16 500 parked coroutines, the Critic,
        `collector/064`) grew the tables unchecked: both check the ceiling now. Edmond's interval:
        5000 ms by default, at least 1000, or a literal 0 for tests and fuzz (`collector/062`; an
        empty value, a bare `off` in php.ini, had meant 0). Tests `019` and `047` rescaled to 1000 ms
        (`changed:`). The rest recorded in `dev/SECURITY.md`. Critic two rounds. Lanes: `pocs-dbg`
        970 PASS, 9 SKIP, 12 XFAIL; `pocs-asan` 953 PASS, 27 SKIP, 11 XFAIL, 6 left out (`064`
        skips there), nothing unexpected.
- [x] S7.7 For S9.4 (`dev/plans/S9-scope.md` 6): a held `Scope` object keeps the coroutines of its
      scope and of its child scopes; the error route and a SpawnStrategy's hooks left out.
      done: the API agreed with the S9 thread, S7.md and DECISIONS updated, tests
      2026-10-07, core `8159f7baa5c`: Edmond's rule (a scope belongs to no one, a coroutine's +1 to
      the scheduler): one reach node per scope, live through a held object, edges down to the
      candidates (S7.md 10). The route and a null `provideScope()` strategy are left out; in
      test-hook builds they hand out what was found in their subtree. Tests `collector/065`-`072`,
      each caught by a mutation. Critic three rounds. Lanes on S9.4 `7f3068f`: `pocs-dbg` 1080
      PASS, 9 SKIP, 48 XFAIL; `pocs-asan` 1061 PASS, 32 SKIP, 44 XFAIL, nothing unexpected.

## S8 — Review checks and RFC change list  [x]

Goal: the review's findings measured on a real provider; requests to both RFCs written.
Done when: B1, B2, B3, M1, M4, M10, M12, M13 each have an outcome (reproduced, not reproduced,
not expressible with why); `RFC-CHANGES.md` complete; the review updated.
Tier: T1. Roles: Critic after every step.
Tests: interleaved
Base: d783cea
Active: none

Each item is run against our provider on the pinned core, on `pocs-dbg` and `pocs-asan`, over the
Ring and over the Poll queue (`TrueAsync\Test\` hook) where the item depends on the queue. Its
outcome goes to a new section 9 of `dev/reviews/io-hooks-design-review.md`: reproduced, not
reproduced (what closes it), or not expressible (why), with the script or test that showed it. A
reproduced defect gets a test of the behaviour TrueAsync gives, tagged `core:<n>` with its
`RFC-CHANGES.md` entry, as S6.7 did for B1; a defect our provider closes gets a test that checks it.

- [x] S8.1 M12 and M13: a bailout while a coroutine is parked in `run()`, and IO after the
      deactivation, against the op on the heap and the suspend predicate (`dev/plans/S6.md` 3.1,
      3.2); the scheduler side of both (review section 6, scheduler items 1 and 2) checked in
      `async-core`.
      done: both outcomes in section 9 with the tests that show them; an `RFC-CHANGES.md` entry for
        each scheduler-side change still missing in the core
      tier: T1 · role: Critic
      handoff: 2026-10-09 on core `3aa1cd120f4`: M12 and M13 do not reproduce (review section 9);
        the core's `Fiber::resume()` and `throw()` assert after the deactivation (`RFC-CHANGES.md`
        24). Tests `io_provider/036`-`041`, `fiber/036`-`038` (036, 037 `core:24`) in
        `tests/lists/S8.txt`; lanes `pocs-dbg` and `pocs-asan`: 14 of 14 S8.1 and related tests PASS.
- [x] S8.1a `Fiber::resume()` and `Fiber::throw()` of an adopted fiber refuse once async is off, on
      `async-core` (`RFC-CHANGES.md` 24), with the core update that brings it.
      done: `fiber/036`, `037` pass without their `core:` tag and `fiber/038` still passes, on debug
        and ASAN; the core suites rediffed
      tier: T1 · role: Critic
      handoff: 2026-10-09 on core `async-core-io-2026-10-09-2` `a60017a0ec0`: refused before the
        enqueue, with "Cannot resume a fiber after the scheduler has stopped" (036, 037 changed); a
        fiber destroyed while suspended is DEAD (`fiber/039`). `pocs-dbg` 1613 PASS, `pocs-asan`
        1584 PASS, 0 unexpected on both; the core's `Zend/tests/fibers` and `ext/test_scheduler`
        210 of 210 PASS on debug.
- [x] S8.1b Fiber adoption bugs the S8.1a Critic found, on `async-core`: a fiber freed while its body
      waits outside `Fiber::suspend()` leaves `EG(active_fiber)` dangling in the body; a `resume()`
      from a second coroutine while one is in flight takes the fiber over; a second `start()` after
      the starter's cancellation adopts the fiber again.
      done: a test each, on debug and ASAN
      tier: T1 · role: Critic
      handoff: 2026-10-09 on core `async-core-io-2026-10-09-3` `c53c17cfd47` (async-core `b52706c85f5`,
        `8c0d98dd7e0`, `eb84ae62df1`): `fiber/040`-`047`, each failing on the S8.1a core. Three Critic
        passes on the fixes found the yield's hand-off window, the transfer slot's leaks and a dead
        starter frame (`043`, `045`-`047`). `pocs-dbg` 1672 PASS, `pocs-asan` 1640 PASS, 0 unexpected
        on both; the core's `Zend/tests/fibers`, `ext/test_scheduler`, `ext/reflection` 0 failed on debug.
- [x] S8.1c php/php-src: `getReturn()` of a fiber destroyed while suspended and kept by a destructor
      returns UNDEF, a fatal error in a debug build, with or without the extension; a fix branch
      from `php-src-fixes`' base and a PR text for Edmond.
      done: a php-src test fails before and passes after on debug; the PR text in project files
      tier: T1 · role: Critic
      handoff: 2026-10-09 branch `fiber-getreturn-destroyed` `d3cb4decf37` on `357fcf12f4e`, picked
        onto `php-src-fixes` as `e7d2d9a25a9`; the next core update merges it. FiberError "The fiber
        was destroyed"; `Zend/tests/fibers/get-return-after-destroy-001`-`003` fail before and pass
        after on debug, `Zend/tests/fibers` 92 PASS. PR text `notes/fiber-getreturn-destroyed-pr.md`.
- [x] S8.1d php/php-src: `exit()` in the `finally` of a fiber destroyed mid-script is swallowed, and
      the script runs on past it (`zend_fibers.c` clears an unwind exit of a destroyed fiber as a
      graceful one; found by the S8.1c Critic, shown on a debug build). Find whether the clearing is
      meant for the shutdown case; if it is a bug, a fix branch and a PR text as S8.1c.
      done: the outcome in the handoff line; a fix's php-src test fails before and passes after
      tier: T1 · role: Critic
      handoff: 2026-10-12 a bug: the clearing is left from the first Fibers commit (`c276c16b6`), whose
        destructor threw an unwind exit itself on a fatal error. Branch `fiber-exit-in-destroyed-finally`
        `f0f3b660863` on `357fcf12f4e`, picked onto `php-src-fixes` as `f1e13b9eaef`: only the graceful
        exit is cleared; a pending exception of the owner is released; the try/finally dispatch reads
        the exception again after freeing a pending return value (the first Critic's use after free).
        `Zend/tests/fibers/exit-in-destroyed-fiber-001`-`004` fail before and pass after; `Zend/tests`
        and `ext/zend_test/tests` 5038 PASS on debug. PR text `notes/fiber-exit-in-destroyed-finally-pr.md`.
        With the extension the fiber's finally runs in its coroutine and its `exit()` already ends the
        script. The gaps a destructor's `exit()` has too went to "Fog".
- [x] S8.2 B1 and B2: each scenario of B1 (a writer beside a parked reader, several acceptors on
      one listener, `fclose()`, `stream_socket_shutdown()` and `proc_close()` of a stream another
      coroutine waits on, `stream_select()` beside a read of a member, two writers on one pipe) and
      each path of B2 (`$db->close()` beside a parked query, the query cancelled, a bailout while it
      is parked), on both queues.
      done: an outcome per scenario and per path; the requests in `RFC-CHANGES.md` for what
        reproduces; entry 12 names every test it waits for
      tier: T1 · role: Critic
      handoff: 2026-10-12 on core `c53c17cfd47`, debug and ASAN, each case on the Ring and the Poll
        queue: B1 reproduces in every scenario (`io_provider/042`-`059`, `core:12`); `proc_close()` does not
        throw but skips the busy pipe. B2: the close is refused and stays refused, a cancelled query
        closes the connection (`mysqli/014`-`017`); the bailout reproduces on the Poll queue (`013`,
        `core:8`). `RFC-CHANGES.md` 8 and 12 rewritten from two Critics' findings.
- [x] S8.3 B3 and M1: a cancelled read whose completion already happened (a socket and a pipe on
      the Ring and the Poll queue; a regular file with `PHP_IO_HOOKS_F_FILES` on in a test-hook
      build), an `fwrite()` cancelled after its bytes went out, a readiness followed by a
      cancellation in the wrapper ladders.
      done: outcomes in section 9; whether the core's commit-on-settle lets the provider turn
        `F_FILES` on (`dev/plans/S6.md` section 6), and if it does, a step that turns it on; the
        requests in `RFC-CHANGES.md`
      tier: T1 · role: Critic
      handoff: 2026-10-12 on core `c53c17cfd47`, debug and ASAN: B3 reproduces for `socket_read()` on
        the Ring (`io_provider/069` records the loss); streams keep their bytes (`009`, `060`-`062`), and
        on the Poll queue `socket_read()` does (`070`). M1
        reproduces on the Ring for a socket write (`063` records the send; nothing sent on the Poll queue,
        `064`), an Open question; the ladders stop on a cancellation. A cancelled file read could not be
        built (a page-cache read completes at its submit): S8.3a. Found: a cancelled `socket_accept()`
        frees its `Socket` twice (bukka's code, `RFC-CHANGES.md` 26, fix branch
        `io-hooks-socket-accept-exception`); a cancelled accept or connect warns, and in the stream
        exception mode replaces the cancellation (27); `065`-`068`, `071` wait for them.
- [x] S8.3a `F_FILES` on: what a regular-file read costs through the Ring against the thread, for a
      page-cache read and for a cold one (the core keeps file ops opt-in because the page-cache read
      loses, `main/io/php_io_ring.c:279-289`), and what a read cancelled in flight gives (the thread
      backend, or a cold read on ext4). The flag also moves pipe, FIFO and tty reads and writes from
      readiness to Ring READ and WRITE ops (`main/io/php_io_hooks.c:1671-1672`), the op types a file
      uses (on Unix only `op->registration`, set for a non-regular descriptor at `:1682`, tells them
      apart), so M1 applies to their writes; and an unbuffered file stream whose read is cancelled in
      flight is marked lost for good. Turn it on if the cold read gains, the page-cache read loses
      little and the pipe writes keep S8.3c's answer, as TrueAsync runs file IO off the thread
      (`dev/plans/S6.md` sections 6, 11).
      done: the numbers in `BENCHMARKS.md`; `F_FILES` on with the `io/` and `include/` lists and
        `io_provider/020` passing on the Ring, and `061` checking that no byte is lost (the stream
        buffer may now hold them), or the reason it stays off in section 11; `io/100` without its
        `--XFAIL--`, or the XFAIL names where it goes
      tier: T2 · role: Critic
      handoff: done 2026-10-13. `F_FILES` stays off (Edmond 02:55): with it on, the core's whole-stream freeze (B1)
        makes a second coroutine on one file handle throw "Concurrent access to a stream" (eight `io/` tests with a
        shared handle, two coroutines including from one phar), where TrueAsync queues them per handle. Until the
        core queues such a wait (`RFC-CHANGES.md` 12), files run on the thread: README "Known limitations",
        `dev/plans/S6.md` sections 11 and 14; `io/100`'s XFAIL names entry 12. Not measured on a release core,
        so no `BENCHMARKS.md` entry. The draft (provider flags, tests) and a core fix it needed (a descriptor
        cast seeking a frozen stream) are patches in `notes/s8-3a/ffiles-draft` (project files).
- [x] S8.3b The core fixes of S8.3, when the core slot is free: `RFC-CHANGES.md` 26 merged from
      `io-hooks-fixes` (done by S8.4a's core update: `067`, `068` pass), 27 as one commit on
      `io-hooks-fixes` with its tests and a PR text for bukka; a core update that carries it.
      done: `io_provider/065`-`068`, `071` pass without their `core:` tags on debug and ASAN; the
        connect half of 27 has core tests with a userland provider whose `run()` throws on the Connect
      tier: T1 · role: Critic
      handoff: done 2026-10-13. Core `async-core-io-2026-10-13-1` `5d6194ec8ac` (`io-hooks-fixes` `2455331acb1`):
        27 also covers a cancelled name lookup (`stream_socket_server()`, `stream_socket_sendto()`) and stops a
        connect's further addresses; its Critic found a cancelled TLS handshake spinning at full CPU to the
        deadline (37, `io_provider/083`). Both are two commits on `io-hooks-cancelled-accept-connect`, PR text in
        `notes/s8-3b` (project files). `065`, `066`, `071` without `core:27`.
- [x] S8.3c M1 on the Ring after Edmond's answer to the Open question: a data op that completed
      before its cancelled coroutine resumed.
      done: `io_provider/063`, `069` expect the chosen behaviour (`changed:` tags) and `009` still
        passes; WAITPID and SIGWAIT under the same race measured
      tier: T1 · role: Critic
      result 2026-10-13: no change. Edmond first chose an exception carrying the result (named
        `Async\CancellationWithResult`), with the rule «if data integrity cannot really be proven, drop it and
        do as TrueAsync: no guarantee whether the data arrived». The Critic showed one PHP call is several ops
        (`php_socket_send_all()`), so only the PHP function knows the count; on Windows a cancelled overlapped
        send leaves the socket's state undefined (Microsoft: close it), so the count cannot be proven there.
        Dropped: a cancelled `socket_write()`/`socket_read()` follows the 10-09 stream rule. `063`, `069`
        keep expecting a plain cancellation; `pcntl_waitpid()`/`pcntl_sigwaitinfo()` already fill `$status`/
        `$info` by reference.
- [x] S8.4 M4 and M10: a connect whose first address refuses and whose second accepts, on both
      queues; curl's `remove()` after libcurl closed the socket; a contended `flock()` with the lock
      holder parked on a timer.
      done: outcomes in section 9; M4's request in `RFC-CHANGES.md` if it reproduces; entry 11
        names its tests
      tier: T1 · role: Critic
      handoff: 2026-10-12 on core `c53c17cfd47`, debug and ASAN, both queues: M4's connect retry does
        not reproduce (the core unregisters before it closes a failed attempt, `main/network.c:1121-1124`;
        `io_provider/076`, `077` skip where localhost has one address, run here with a second
        `/etc/hosts` line). M4's curl case reproduces on the Poll queue: a descriptor a curl callback
        opens takes the closed socket's number, and curl's late `remove()` frees the record under it
        (`RFC-CHANGES.md` 28, `073` `core:28`; Ring `074` passes). M10 reproduces: `io/081`, `084`, new
        `io_provider/072`, `075`, all `core:11`; entry 11 now asks for TrueAsync's `LOCK_NB` and timer.
- [x] S8.4a The core fix of `RFC-CHANGES.md` 28 for bukka, when the core slot is free: the curl
      callback ends a removed socket's registrations before libcurl closes it; a branch on bukka's
      base and a commit on `io-hooks-fixes` with a core test, a PR text in `notes/`; Edmond hears of
      it first and opens the PR.
      done: `io_provider/073` passes without `core:28` on debug and ASAN; `074` still passes
      tier: T1 · role: Critic
      result 2026-10-12: core `async-core-io-2026-10-12-1` `36050e31325` (`c53c17cfd47` with
        `io-hooks-fixes` `b9bc37e11e3` merged, which also brings entry 26 of S8.3b); PR branch
        `io-hooks-curl-late-remove` `c92a03dd619` on bukka's `8e470c219fc`, PR text
        `/mnt/project-files/notes/s8-4a/io-hooks-curl-late-remove-pr.md` (Edmond opens it). The provider's
        `remove()` now runs inside libcurl as a curl callback does (`in_callback` set); its exception is
        kept for the reconcile; freeing the handle ends the registrations before `curl_multi_cleanup()`.
        Core tests `curl_exec_io_hook_remove_before_close`, `_throws`, `_reset` (skip on the Ring).
        `io_provider/067`, `068`, `073` pass without `core:` on debug and ASAN, 20 seeds clean. All lists:
        pocs-dbg 1760 PASS, 21 SKIP, 9 XFAIL; pocs-asan 1734 PASS, 48 SKIP, 8 XFAIL; 0 unexpected. Core
        suites (`ext/test_scheduler`, `poll`, `streams/hooks`, `curl`, `sockets`), old core against new:
        only the new tests differ (528 against 538 PASS on both trees, before `_reset`; with it 539
        on debug, 537 on ASAN with `--asan`, Poll queue). Not run: the `ext-scheduler-hook` bridge
        (not in this session; the change does not touch the scheduler API).
      Critic 2026-10-12: `remove()` inside libcurl could `curl_reset()` the handle under libcurl and a
        pending callback exception swallowed its failure; the free path ran it inside
        `curl_multi_cleanup()`. All three fixed (the `_reset` test); the second Critic found nothing blocking.
- [x] S8.5 `RFC-CHANGES.md` against the review: every item of the review's section 6 (both lists)
      and every section 5 defect a script on our provider shows has an entry or a reason in section
      9; the review's summary states what holds on the core each row of section 9 names.
      done: no item of section 6 without an entry or a reason; the review's section 1 updated
      tier: T1 · role: Critic
      handoff: 2026-10-12 on core `c53c17cfd47`: section 9 has a row for every finding (M2, M3, M5,
        M6, M7, M8, M9, M11, section 6 item 13 and section 5 added) and section 1 says what holds.
        New entries, each with a test (29, 31 and 32 show the defect today on debug; 30's test shows
        B1's refusal until 12 and 30 land): 29 `mysqli_poll()` (`mysqli/018`), 30 TLS
        waits outside OpenSSL (`io_provider/078`, full duplex once 12 lands too; the re-entry is
        inferred, the spin read from the code), 31 `O_NONBLOCK` only on PHP's own descriptions (`079`,
        PLAN request A), 32 a pipe write tries the syscall first (`080`, `081`: two loggers on one pipe
        or on `STDERR` collide today). Not requested, with reasons in
        section 9: M2's predicate, M8's shared multi (Fog), M6 under ZTS outside the CLI.
- [x] S8.6 Make `stream_set_timeout()` work on a pipe and on `stream_socket_recvfrom()`/`sendto()` under
      a provider (core fixes of `RFC-CHANGES.md` 3 and 4), so the five tests that wait for them run
      without `--XFAIL--`; when the core slot is free: commits on `io-hooks-fixes` with core tests and
      PR texts for bukka in `notes/`, Edmond opens them; a core update.
      done: `io/039`, `040`, `042`, `043` and `stream/030` pass without `--XFAIL--` (and `030` without its
      watchdog) on debug and ASAN
      tier: T1 · role: Critic
      handoff: done 2026-10-13. Core `async-core-io-2026-10-13-2` `8568d99bd3e` (`io-hooks-fixes` `cee42c3db37`),
        two commits on `io-hooks-stream-timeouts`, PR text in `notes/s8-6` (project files). `recvfrom()`/`sendto()` take the
        timeout only after an explicit `stream_set_timeout()` (the PoC's test keeps `default_socket_timeout` off
        them; Edmond agreed). A pipe read polls up to the timeout first, on Unix; Windows still refuses it (S8.9).
        `stream/030` is the reference file again, without the watchdog and the `changed:` tag.
- [x] S8.7 A stream given to `CURLOPT_FILE`, `INFILE`, `WRITEHEADER` or `STDERR` and closed by another
      coroutine while `curl_exec()` waits is not used after it is freed (TrueAsync's curl history, item E1
      of `/mnt/project-files/notes/s8-curl/trueasync-curl-history.md`); a core fix for bukka
      (`RFC-CHANGES.md` 34) in the core update that also restores bukka's two curl comments.
      done: `io_provider/082` and the core test fail on the old core and pass on debug and ASAN
      tier: T1 · role: Critic
      result 2026-10-12: core `async-core-io-2026-10-12-3` `634cbbf94be` (`96a52ba65f4` with `io-hooks-fixes`
        `a51797d8645` merged: bukka's two curl comments back word for word, then the fix); PR branch
        `io-hooks-curl-closed-file` `f7fbc4335fe` on bukka's `8e470c219fc`, PR text
        `/mnt/project-files/notes/s8-7/io-hooks-curl-closed-file-pr.md` (Edmond opens it). `curl_write()`,
        `curl_write_header()` and `curl_read()` fail the transfer on a closed stream with no PHP code inside libcurl,
        `curl_exec()` warns after; a closed `STDERR` is swapped for `stderr` after each wait. Core tests
        `curl_exec_io_hook_{file,infile,stderr}_closed_while_waiting` and `io_provider/082` fail on the old core (a
        debug build aborts in `munmap_chunk()`; valgrind shows the writes to the freed `FILE*`) and pass on debug and
        ASAN, Poll queue and Ring, `082` 20 seeds clean. All lists: pocs-dbg 1761 PASS, 21 SKIP, 9 XFAIL; pocs-asan
        1735 PASS, 48 SKIP, 8 XFAIL; 0 unexpected. Core suites, `96a52ba65f4` against the new core: only the three
        new tests differ (565 PASS on both trees). History item A3 (DNS on libcurl 8.20.x) does not reproduce:
        8.20.0 loaded under the debug core resolves names in `curl_exec()` within milliseconds.
      Critic 2026-10-12: a check after each wait missed yields inside libcurl (a suspending callback, a socket
        stream's cookie `FILE*`), the cancelled wait, and reset the sink mid-body; redone at the point of use. The
        second Critic: no blocker; the warning moved out of libcurl, the INFILE test waits on a 100-continue.
- [x] S8.8 The curl tests of the core and of the extension pass under the libcurl versions that broke
      TrueAsync (Edmond: «посмотри по истории у true async какие именно версии вызывали баги»): a local
      script, `tools/curl-versions.py`, not a CI job.
      done: core `ext/curl/tests` and the extension's curl tests pass under 7.87.0, 8.5.0, 8.10.1, 8.11.0,
      8.11.1, 8.13.0, 8.20.0 and 8.22.0
      tier: T1 · role: —
      result 2026-10-12: all eight pass on pocs-dbg (core `634cbbf94be`): core 190 passed (188 on 7.87.0), the
        extension's 74-75 PASS, 0 unexpected. Three to five core `curl_getinfo_CURLINFO_*` tests need the
        headers of a newer libcurl than the 8.5.0 the core is built against; the script lists them apart.
        Found: `tools/test.py` dropped `LD_LIBRARY_PATH`, so the extension's tests ran on 8.5.0 whatever the
        prefix (now passed through); `io_provider/073` and `074` named libcurl's socket by the text
        "Connected to" (8.20 logs "Established connection to") and by the only new socket (7.87.0 keeps a
        socketpair too), now by the socket gone before "Issue another request"; ASAN and 20 seeds clean.
- [x] S8.9 Windows: `stream_set_timeout()` bounds a read of an overlapped pipe (left from S8.6): a readiness
      wait up to the deadline, then a read of what is there, as on Unix; cancelling a timed-out READ op can
      lose its bytes (`RFC-CHANGES.md` 3).
      done: `io/039`, `040`, `042`, `043` pass on `pocs-win` without `xfail-on`
      tier: T1 · role: Critic
      handoff: done 2026-10-13. Core `async-core-io-2026-10-13-3` `57fed0673c0` (`io-hooks-fixes` `f2d5fd05710`), the
        third commit of `io-hooks-stream-timeouts` (`5e6ded8b09c`, PR text `notes/s8-6`). `php_io_pipe_poll()` waits as
        `stream_select()` does, an Any of the pipe's Poll and a Timer, so the Ring keeps and the provider drains the
        cancelled zero-byte read; without a provider, or on Unsupported, the thread peeks with `Sleep(1)` until the
        deadline. The read after it is non-blocking, and EAGAIN polls again with the same deadline.
      Critic 2026-10-13: a single Poll with a deadline left the zero-byte read in the kernel unkept (a handed-out pipe
        could see a false read); a readiness with no bytes made the read wait without limit; a failed Any read as a
        timeout. Fixed. Open: `php_io_pipe_read()` peeks, then reads synchronously, so a handed-out pipe whose bytes
        another process takes between the two still parks the thread (bukka's code, as before); the peek loop
        blocks other coroutines for the timeout when the pipe was handed out or the provider answers Unsupported.
      result 2026-10-13: run 38347590325 (branch `s8-9-test`, dispatched) green in every job, `windows` included. On
        Edmond's PC, Debug_TS of the new core, /WX clean: `io/039`-`043`, `io_provider/028`, `stream/033` pass; core
        streams, `hooks/`, `stream_select`, `proc_open` tests 59 PASS, 34 SKIP, 3 FAIL. The three are S8.3b's
        `hooks/connect-throws.phpt`, `connect-throws-one-attempt.phpt`, `fsockopen-throws.phpt`: on Windows the
        throwing op is a Poll, not the Connect they expect (not looked into, outside S8.9).
- [x] S8.10 The three core tests of S8.3b that fail on Windows (S8.9): `hooks/connect-throws`,
      `connect-throws-one-attempt`, `fsockopen-throws`.
      done: the three pass on the core on Linux and Windows; the one-attempt test fails when a connect goes on
        to the next address after the exception
      tier: T1 · role: Critic
      handoff: done 2026-10-13. Not a Windows matter: they fail on Linux too (`-3`: 85 hooks tests, these 3 red).
        Our connect-started fix in the core (`a6fbd0776e9`, bukka/php-src#4) waits for a connect the core started
        with a Poll op, so the throwing provider names a Poll; on `io-hooks-fixes` alone it is a Connect.
        The tests accept either (`io-hooks-fixes` `5f15ea1e701`, also the third commit of
        `io-hooks-cancelled-accept-connect` `1da1a2c173f`); core `async-core-io-2026-10-13-4` `77ddd91c4b6`, a
        test-only merge. The Critic found the one-attempt test could not fail: after the exception the core runs no op through
        the provider, so its count of ops stayed 1. It now resolves the name to its listener twice and expects at
        most one connection there; with the guard in `main/network.c` removed it fails. Open: no test pins the Poll
        that a provider without DirectData gets after the core's own connect (the alternation lets a Connect pass):
        a core test with that provider expecting exactly `Io\Operation\Poll`, on `io-hooks-connect-started`.
      result 2026-10-13: core hooks tests 83 PASS, 2 SKIP on Linux debug (`57fed0673c0` binary, test-only change);
        Windows probe run 38351449003 (branch `s8-10-test`): the same Poll. Run 38353296164 on `-4`, Windows Release_TS:
        hooks 48 PASS, 36 SKIP, 0 FAIL; `registrations-edge-select.phpt` passed on retry (not looked into).
- [x] S8.11 Edmond's review of our code on the two bukka branches: no repeated comments, `EXPECTED()`/`UNEXPECTED()`
      on the new checks, every comment we wrote plain and short.
      done: `io-hooks-cancelled-accept-connect` `e0e3cce2444`, `io-hooks-stream-timeouts` `b3742eb44e3`,
        `io-hooks-fixes` `17f52a66b46`, core `async-core-io-2026-10-13-5` `dcb1ea7d64d`; PR texts in `notes/`
      tier: T0 · role: none (comments and branch hints; one test fix)
      handoff: done 2026-10-13. The run of the core's openssl tests found `io_hooks_handshake_cancelled_client.phpt`
        red since `-4`: its provider threw on the Poll that waits for the core's own connect; it now completes a Poll
        as it does a Connect. Bukka's `stream_io_hooks.phpt` is red in the core since `-07-4` (Open questions).
      result 2026-10-13: core debug, `ext/standard/tests/streams`, `poll`, `network`, `ext/openssl/tests`: 699 PASS,
        46 SKIP, 1 FAIL (`stream_io_hooks.phpt`, the same on `-4`).
- [x] S8.12 The core takes bukka's merged connect-started fix (bukka/php-src#4, his `7022e9af819`): after its own
      `connect()` the core submits a Connect op flagged `PHP_IO_OP_F_CONNECT_STARTED` instead of a Poll op, so
      bukka's `ext/openssl/tests/stream_io_hooks.phpt` passes again.
      done: `io-hooks-fixes` `a470c7ddddc` (the pick), core `async-core-io-2026-10-13-6` `910cbcab817`
      tier: T1 · role: Critic
      handoff: done 2026-10-13. Our core had kept the first version (`dca62e39da4`, a Poll op) since `-07-4`, while
        the PR was changed and bukka merged the flagged one. The Critic found, in that merged code: a userland
        provider cannot see the flag, so one that answers a Connect Ready without waiting makes a connect still
        under way look connected (SO_ERROR is 0), where the Poll version waited for it; and the provider's early
        answer for an expired deadline covers Poll ops only (Windows IOCP, speculative). Next: S8.13.
      result 2026-10-13: core debug `ext/standard/tests/{streams,poll,network}`, `ext/openssl/tests`,
        `ext/sockets/tests`: 806 PASS, 0 FAIL; `pocs-dbg` 1994 PASS, 1 FAIL (`iterate/061`, S9.42's, passes 5 of 5
        alone).
- [x] S8.13 A started connect that a userland provider answers Ready without a wait (S8.12's Critic):
      `RFC-CHANGES.md` 38.
      done: `hooks/connect-ready-without-wait.phpt` fails before and passes after; a bukka branch and PR text
      tier: T1 · role: Critic (S8.12's)
      result 2026-10-13: core `async-core-io-2026-10-13-7` `f9c2ef8661d` (`io-hooks-fixes` `03ee93a8ce4`), branch
        `io-hooks-connect-ready-unconnected` `905e4ababe3` (bukka's head: 635 PASS, 0 FAIL); core debug suites 807
        PASS, 0 FAIL; `pocs-dbg` 1994 PASS, 1 FAIL (`reactor/009` under the full lane's load, passes 5 of 5 alone).

## S9 — Higher layers, one at a time  [in progress]

Scope, context, channels, task groups, pools, iterators, FileSystemWatcher, runtime information:
each its own plan, agreed with Edmond. FileSystemWatcher came from S10 (`dev/plans/S10.md` section 9, Edmond
2026-10-08). It has its own iterator, not the iterators layer's (TrueAsync's `fs_watcher.c:596-721`),
so the two layers do not overlap. On Linux and macOS it would wait on an inotify or kqueue descriptor
with `php_io_poll()` (not yet tried); on Windows it needs a directory-change op, which its plan will
request from bukka. 13 `fs_watcher` tests.
Runtime information came from S3's exclusions (S14.4): TrueAsync's `available_parallelism()`,
`loadavg()`, `CpuSnapshot`, `cpu_usage()` and `runtime_stats()`; 6 `common` tests in
`tests/lists/S3.excluded`.
Layer 1, Scope: `Async\Scope`, `ScopeProvider`, `SpawnStrategy`, `spawn_with()`, the global scope,
zombies, the error route through scopes, both `finally` methods on TrueAsync's iterator core.
Layer 2, Context: `Async\Context` over the core's storage, the context of a coroutine and of a scope,
`current_context()`, `coroutine_context()`, `root_context()`.
Layer 3, Channel: `Async\Channel`, `ChannelException`, `ChannelCloseReason`, the CHANNEL wait kind,
`recvAsync()`, `foreach`, the per-channel timers, the close at the global deadlock and by the owner scope.
Layer 4, TaskGroup: `Async\TaskGroup`, `Async\TaskSet`, the TASK_GROUP wait kind, the group's closing and
finally handlers, and `await_*` items and tokens narrowed to `Completable`.
Layer 5, Pool: `Async\Pool`, `PoolException`, `ServiceUnavailableException`, the circuit-breaker types, the POOL
wait kind with TrueAsync's reservations, the healthcheck in a coroutine on a soft timer.
Done when: S9.txt's layer 1 block and `await/062` pass on debug and ASAN; the S3-S7 lists pass as before.
Layer 2 done when: S9.txt's layer 2 block passes on debug and ASAN; the S3-S7 lists and layer 1 pass as before.
Layer 3 done when: S9.txt's layer 3 block passes on debug and ASAN; the S3-S7 lists and layers 1 and 2 pass as before.
Layer 4 done when: S9.txt's layer 4 block passes on debug and ASAN; the S3-S7 lists and layers 1-3 pass as before.
Layer 5 done when: S9.txt's layer 5 block passes on debug and ASAN; the S3-S7 lists and layers 1-4 pass as before.
Layer 6, iterators: `Async\iterate()` on S9.6's iterator core, the ITERATE wait kind, the caller's cancellation
(`dev/plans/S9-iterate.md`).
Layer 6 done when: S9.txt's layer 6 block passes on debug and ASAN; the S3-S7 lists and layers 1-5 pass as before.
Layer 7, FileSystemWatcher: `Async\FileSystemWatcher` over a per-request inotify descriptor on the POLL op, the
WATCHER wait kind, Windows on a directory-change op requested from bukka and ior (`dev/plans/S9-fs-watcher.md`).
Layer 7 done when: S9.txt's layer 7 block passes on debug and ASAN, and on `pocs-win` after S9.45; the S3-S7 lists
and layers 1-6 pass as before.
Runtime information has no plan yet: it gets its steps when Edmond agrees its plan, and S9 is not closed before it
has them (S14.4).
Tier: T2. Roles: Critic and Sage on S9.1, Critic after S9.6 (S9.7).
Tests: interleaved
Base: be20b82
Notes: dev/plans/S9-scope.md, dev/plans/S9-context.md, dev/plans/S9-channel.md, dev/plans/S9-taskgroup.md,
  dev/plans/S9-pool.md, dev/plans/S9-iterate.md, dev/plans/S9-fs-watcher.md

- [x] S9.1 Design note `dev/plans/S9-scope.md` and the frozen list `tests/lists/S9.txt` (layer 1).
      done: the note and the list pushed; every Critic finding fixed or answered in the note;
        Edmond's answer to section 12 recorded
      tier: T2 · role: Critic → Sage
      handoff: done 2026-10-07: 91 reference tests with `--XFAIL--` naming S9.2-S9.6 (35, 5, 13, 14,
        24); 21 lines leave `S3.excluded`, 2 leave `S6.excluded`, `scope/052` waits for Context in
        `S9.excluded`. On a debug build of the reference fork with `ext/async` at `REFERENCE` the
        candidate tests pass (161 with their groups). The Critic's 4 high findings and the Sage's 5
        rulings are in the note (sections 4, 6, 9, 11, 12); the Critic's 9 findings on the commit
        moved `cancel()` whole into S9.2, the scope waiters' error wake into S9.4, re-pinned 4
        tests and gave each step its own tests (note 10). Edmond: the global scope cancels with
        the error's origin flag, as TrueAsync (section 12, option 1).
- [x] S9.2 The scope and the global scope, spawn into a scope, `Scope` without waiting, `cancel()`
      with its cascade and the close of a scope it leaves empty (note section 5), `spawn_with` with
      `ScopeProvider` and `SpawnStrategy`, the core's and our own coroutines placed (note
      section 3), zombies in the cancel slot and `get_coroutine_count`.
      done: S9.txt's S9.2 tests and the note's S9.2 own tests (section 10) pass on debug and ASAN;
        the S3-S7 lists pass as before
      tier: T2 · role: Critic
      handoff: done 2026-10-07: `src/scope.c`; the 35 tests and `scope/053` (S9.5's, passes on
        disposal by the object's destruction) lose `--XFAIL--`; own tests `internal/064`, `065`,
        `scope/058`, `spawnWith/013`-`016`. Debug 1010 PASS, 67 XFAIL; ASAN 994 PASS, 63 XFAIL;
        0 unexpected on both. The Critic's 2 high findings (hooks that suspend, a fatal error in a
        hook) and the stand-in shared by concurrent hooks fixed with tests; a core GC bug it exposed
        (the threshold raised once per waiter of a run) reported for a core fix.
- [x] S9.3 The error route with both exception handlers and its cascade of fresh cancellations
      (note section 4).
      done: S9.txt's S9.3 tests and `p5`-`p7` of the note as own tests pass on debug and ASAN;
        the S3-S7 lists pass as before
      tier: T2 · role: Critic
      handoff: done 2026-10-07: `async_scope_catch` in `src/scope.c`, called from finalize when no
        wait record was called (`async_callbacks_notify` returns it); the 5 tests lose
        `--XFAIL--`; own tests `scope/059`-`075` (`075` XFAIL until the core carries
        php/php-src#24177); 7 `scheduler/` tests of S3 start their coroutines before the first
        failure (`changed:`). Debug 1049 PASS, 63 XFAIL; ASAN 1031 PASS, 59 XFAIL; 0 unexpected on both. The Critic's 2 high
        findings (a handler's release freeing the parent under `scope_dispose`, a fatal error in a
        handler) fixed with `scope/072`, `073`; the Sage kept the handler that cannot park (note
        9, item 9), a question for Edmond.
- [x] S9.4 `awaitCompletion()`, the SCOPE wait kind, the route's wake of the scope's waiters with
      the error (note section 4, step 2) and the `await_*` child scope (note sections 6, 8).
      done: S9.txt's S9.4 tests, `await/062` and the note's S9.4 own tests pass on debug and ASAN;
        the S3-S7 lists pass as before
      tier: T2 · role: Critic
      handoff: done 2026-10-07: the 13 tests and `await/062` lose `--XFAIL--`; own tests
        `scope/076`-`083`. Debug 1072 PASS, 48 XFAIL; ASAN 1053 PASS, 44 XFAIL; 0 unexpected on
        both, on the core with CORE_REF 8159f7baa5c. Of the Critic's 3 findings two are
        TrueAsync's behaviour and stay (the Sage: safe disposal's early wake, a second `cancel()`
        closing a running scope), one comment fixed. The collector's edges moved to S9.9.
- [x] S9.9 The collector's edges for scopes (note section 6), split from S9.4: an S7.7 reporter of
      an edge that owns no reference; Edmond questioned S7.7 on 2026-10-07, so it waits for his
      word in the S7 thread.
      done: the note's S9.9 own tests pass on debug and ASAN; the SCOPE kind reports its target;
        the S3-S7 lists pass as before
      tier: T2 · role: Critic
      handoff: done 2026-10-07: one completion node per awaited scope; the route hands out found
        waiters, the `await_*` iterator holds its scope's reach node, the oracle checks at the
        notify sites and the `cancel` policy hands out main; a by-reference `provideScope()` works.
        Own tests `scope/084`-`093`, `spawnWith/017`. Debug 1094 PASS, 48 XFAIL; ASAN 1075 PASS,
        44 XFAIL; 0 unexpected on both, on CORE_REF 3af71f889e6; 10 fuzz seeds over scope,
        collector and spawnWith, 0 failed. Four Critic passes: 4 false findings or aborts fixed.
- [x] S9.5 `dispose()`, `disposeSafely()`, `disposeAfterTimeout()`, `awaitAfterCancellation()`, the
      object's destruction (note section 5).
      done: S9.txt's S9.5 tests and `p3` of the note as an own test pass on debug and ASAN; the
        S3-S7 lists pass as before
      tier: T2 · role: Critic
      handoff: done 2026-10-07: 13 tests lose `--XFAIL--`; own tests `scope/094`-`110`, each 36
        runs under load clean. On CORE_REF 0145ca90d78 (-6): debug 1127 PASS, 35 XFAIL, 0
        unexpected; ASAN 1096 PASS, 31 XFAIL, 12 unexpected: 11 fork tests leak under LSan in this
        container on clean main too ("Running thread was not suspended"), and `io_provider/009`
        (S6's flake). Five Critic passes, the Sage once: errors that come while the
        `awaitAfterCancellation()` handler runs climb on (no scope-held intake); a closed scope
        that is not cancelled returns at once, as TrueAsync's. Left for S9.7: the route marks a
        closed scope cancelled; a forked child's `get_deadlocked_coroutines()` before its first
        suspension reads the parent's timer as armed. Tests that let members start with a
        top-level `delay(1)` flake under load (U2's short path, PLAN Open questions S6.8).
- [x] S9.6 TrueAsync's iterator core, `Scope::finally()` and `Coroutine::finally()` (note section 7),
      the bailout trace of `bailout/013`-`015` first.
      done: S9.txt's S9.6 tests pass on debug and ASAN; the S3-S7 lists pass as before
      tier: T2 · role: Critic
      handoff: done 2026-10-07: `src/iterator.c` (the port; test hook `TrueAsync\Test\iterate()`),
        both `finally()` methods; the 24 tests lose `--XFAIL--`; own tests `internal/066`-`069`,
        `scope/111`-`117`, `coroutine/040`, `bailout/016`, `017`, each 36 runs under load clean. On
        CORE_REF 0145ca90d78: debug 1165 PASS, 11 XFAIL; ASAN 1141 PASS, 10 XFAIL; 0 unexpected on
        both. The bailout trace needs no rule change (note 7, "As built"). Departures: note 9, items
        18-21 (handlers start after the route; a run's error is its last worker's; `exit()` in a
        handler ends the request). Three Critic passes: an unrun last worker's error, a walk stopped
        during a move restarting (TrueAsync's bug, `internal/069`), the DISPOSING walk fixed with tests.
- [x] S9.7 Layer review: Critic after S9.2-S9.6, coverage of `src/scope.c` and the iterator, Mull on
      the layer's diff, the fuzz oracle over 100 seeds, the measurements of note section 10.
      done: the layer's Done when holds on the day; survivors killed or explained
      tier: T2 · role: Critic
      handoff: done 2026-10-07: coverage on debug `src/scope.c` 775 of 819 lines, `src/iterator.c`
        212 of 248; Mull on the diff since `d196cbd`: 175 mutants, 26 not killed, 15 killed for
        time, each survivor killed by a later test or explained (note 10); the fuzz oracle over 100
        seeds of the 247 tests of `S9.txt` and `collector/`: 0 failed seeds, and the 29 tests whose
        diagnostics change with the order change the same way on `05037c7`; six own tests made to
        wait for their coroutines, each 100 of 100 seeds. BENCHMARKS: the scope adds about 190
        instructions per spawn; with `awaitCompletion()` ours 2,433 per member to the reference's
        3,887 at 1 000 members. Backlog: the found waiter, the zombie's back-off and the forked
        child fixed (`collector/073`-`077`); the closed scope marked cancelled and the handler's
        AsyncCancellation kept as TrueAsync (`scope/118`). Own tests `collector/073`-`078`,
        `scope/118`-`122`, `internal/070`, `071`. On CORE_REF 0145ca90d78: debug 1178 PASS, 11
        XFAIL; ASAN 1154 PASS, 10 XFAIL; 0 unexpected on both. Five Critic passes: a forked child's
        assert, an over-broad oracle excuse, a false iterator assert, a replaced outcome keeping the
        old one's marks and reaching an `await()` waiter twice, comments that contradicted each
        other. Departures: note 9, items 22-26. For S9.8: a chain of 50 000 nested scopes overflows
        the C stack (as on TrueAsync); a refused finally start inside the cancel loops.
- [x] S9.8 Security pass by `dev/SECURITY.md`.
      done: a journal entry per checklist item; findings fixed with a test or recorded
      tier: T2 · role: —
      handoff: done 2026-10-08: journal entries in `SECURITY.md`. Fixed with tests: a finally run refused
        for want of a stack released its handlers inside a cancel's cascade, whose destructors could make
        it skip a child scope (`scope/123`); a child scope searched its parent's vector to leave it
        (`scope/125`); a member's finish re-walked each parent's subtree, O(N^2) in a chain of N
        (`scope/124`). Accepted, as TrueAsync: the recursive walks (about 43 000 nested scopes in a
        coroutine on debug, a `stdClass` list about 6 000), a cascade's per-level completion test and the
        sibling rescans. On CORE_REF 0145ca90d78: debug 1181 PASS, 11 XFAIL; ASAN 1157 PASS, 10 XFAIL;
        0 unexpected on both; 20 fuzz seeds over the new tests clean. Two Critic passes: a release on the
        refusal path could start a GC run inside the walk.
- [x] S9.10 Design note `dev/plans/S9-context.md` (layer 2, Context).
      done: the note pushed; every Critic finding fixed or answered in the note; the question of its
        section 9 put to Edmond
      tier: T2 · role: Critic → Sage
      handoff: done 2026-10-08: the core already holds the context's storage (`zend_async_context_t`,
        the coroutine's `context`), so the layer adds the class over it, a `context` field per scope
        and the walk up the scope tree. Probes `c1.php`-`c14.php` on the reference
        (`/mnt/project-files/s9/probes/s9.context/`): TrueAsync collects no cycle through a context,
        a destructor at shutdown reading a context stops its debug build, its root context is empty
        in shutdown functions, and it puts a Fiber's coroutine into the current scope, which layer 1
        took for the opposite (question 1 of the note). Three Critic passes and the Sage: the context
        released after the scope walk; the scope object reports its context, and its handlers (layer
        1's bug), only while no member reaches the scope; the teardown's user values released last;
        `RFC-CHANGES.md` 16, a heap corruption in the core's string-key replace, waits for Edmond's
        word to push to `async-core`.
- [x] S9.11 The list block for layer 2, `Context` and `ContextException`, the factory slot,
      `coroutine_context()`, `Coroutine::getContext()` (note sections 2, 3, 8); after a core update
      carrying `RFC-CHANGES.md` 16.
      done: the block's S9.11 tests and the note's S9.11 own tests pass on debug and ASAN; the S3-S7
        lists and layer 1 pass as before
      tier: T2 · role: Critic
      handoff: done 2026-10-08: 17 reference tests (6 pass, 11 with `--XFAIL--` naming S9.12), own
        `context/014`-`025`; `S9.excluded` is gone, three lines leave `S3.excluded`. Three Critic
        passes: a finished coroutine is still current while its `free_obj` runs a WeakMap value's
        destructor, so `current_coroutine()` handed out an object about to be freed and a context
        made there leaked; the three entry points refuse that window (`context/020`, `024`, `025`;
        `RFC-CHANGES.md` 17 for C callers). On CORE_REF 662dfe91919: debug 1199 PASS, 9 SKIP, 22
        XFAIL, one timing FAIL of `await/069` (1 in 200 runs under load, unrelated: `timeout(1)`'s
        deadline passes before the await, D32); ASAN 1176 PASS, 34 SKIP, 21 XFAIL; 0 unexpected.
- [x] S9.12 The context of a scope, `current_context()`, `root_context()`, `request_context()`, the
      walk, the context in the scope object's `get_gc` under the rule the handlers follow since their
      fix (note sections 4, 5).
      done: the block's S9.12 tests and the note's S9.12 own tests pass on debug and ASAN; the S3-S7
        lists and layer 1 pass as before
      tier: T2 · role: Critic
      handoff: done 2026-10-08: the 11 XFAIL reference tests pass, own `context/026`-`039` and
        `scope/127`. Three Critic passes: an idle child scope the script holds kept no parent out of
        the GC, so the scope object reports its handlers and context only while every child scope can
        be disposed, and the give-back walks up (`context/037`, `scope/127`; `scope/126`'s last case
        changed on Edmond's word); `current_context()` refuses a finished coroutine that left a scope
        other than the global one (`context/038`). On CORE_REF 662dfe91919: debug 1228 PASS, 9 SKIP,
        11 XFAIL; ASAN 1202 PASS, 34 SKIP, 10 XFAIL; 0 unexpected. `collector/018` failed once in a
        loaded run and passed on retry; 80 runs alone and under load passed (not S9.12's).
- [x] S9.13 The teardown's user values released as its last step (note section 4).
      done: the note's S9.13 own test passes on debug and ASAN; the S3-S7 lists and layer 1 pass as
        before
      tier: T1 · role: Critic
      handoff: done 2026-10-08: RSHUTDOWN releases the scope values, the registry's coroutine objects
        and the unobserved exceptions after the reactor's teardown (`context/040`, `042`, `044`, with
        the test hook `print_at_teardown()`). Three Critic passes: a Future mapped in that release
        wrote into the freed registry and `disposeAfterTimeout()` made a reactor queue nothing freed;
        both now do nothing while async is not active (`context/041`, `043`). Each test fails with its
        fix taken out. On CORE_REF 662dfe91919: debug 1233 PASS, 9 SKIP, 11 XFAIL; ASAN 1209 PASS, 34
        SKIP, 10 XFAIL; 0 unexpected.
- [x] S9.14 Layer review: Critic after S9.11-S9.13, coverage, Mull on the layer's diff, the fuzz
      oracle over 100 seeds, the measurements of note section 8.
      done: the layer's Done when holds on the day; survivors killed or explained
      tier: T2 · role: Critic
      handoff: done 2026-10-08. Critic: no memory or refcount defect; `current_context()`'s refusal
        kept as TrueAsync's (DECISIONS). Coverage on pocs-dbg-cov: src 7296 of 7724 lines, the
        layer's added lines 259 of 264; `scope/128`-`130` cover the handler release paths. Mull: 38
        mutants on the layer's lines, one survivor killed by `scope/131` (`scope.c:203`), four
        explained: `scope.c:234` (active equal to zombie above 0 needs a spawn into a closed scope),
        `scope.c:239` (needs a disposable child scope left in the vector), `scope.c:1583` (S9.5's
        line, an early wake masked by the do-while re-check), `true_async.c:255` (MSHUTDOWN, which
        no phpt observes); `scope.c:439`, the give-back walk's continuation, stays uncovered: a
        scope it would reach is disposed instead. Fuzz: 100 seeds over 54 tests, 0 failed after
        `context/025` waits in a loop. B13 and B14 in `dev/BENCHMARKS.md`: `find()` 41 instructions
        per level (ref 50), a coroutine's context 1,287 instructions and 2 allocations (ref 1,232
        and 2). On CORE_REF 662dfe91919: debug 1237 PASS, 9 SKIP, 11 XFAIL; ASAN 1213 PASS, 34 SKIP,
        10 XFAIL; 0 unexpected.
- [x] S9.15 Security pass by `dev/SECURITY.md`.
      done: a journal entry per checklist item; findings fixed with a test or recorded
      tier: T2 · role: —
      handoff: done 2026-10-08: journal entries in `SECURITY.md`, by checklist item; no memory or
        refcount defect found on the debug and ASAN builds (the context's walk, the release window, the
        final release in RSHUTDOWN, destructors that rewrite the table). Recorded: a scope's context
        alone now makes its object's `get_gc` test the child scopes recursively, so a collection under
        about 43 000 nested scopes overflows the GC coroutine's stack, the limit S9.8 accepted; the
        per-scope count of busy child scopes would remove it and is not built. Docs only, no lane run.
- [x] S9.16 Design note `dev/plans/S9-channel.md` (layer 3, Channel).
      done: the note pushed; every Critic finding fixed or answered in the note; the question of its
        section 10 answered by Edmond
      tier: T2 · role: Critic → Sage
      handoff: done 2026-10-08: the pinned core has no channel type, so the layer is the extension's own,
        on S4's wait-record layer: the coroutine's waker record is the queue entry (D29). Edmond decided
        both questions (DECISIONS 2026-10-08): the owner scope closes its channels when it is cancelled
        or destroyed, not when it completes, and a CHANNEL record stays linked from its wake until its
        frame takes it out (an exception to D26, as TrueAsync's waiter). Three Critic passes and a Sage;
        the Sage ran the scope rule and the D26 case on the reference. Probes `h1.php`-`h14.php`,
        `f1a.php`-`f7.php`, `t048.php`-`t063.php` in `/mnt/project-files/s9/probes/s9.channel/`.
        93 reference tests for the list.
- [x] S9.17 The list block for layer 3, the channel, its buffer, `send()`, `sendAsync()`, `recv()`,
      `close()` and the readers, the reservations, the CHANNEL kind, cancellation tokens, the destructor,
      `ChannelException` and `ChannelCloseReason` (note sections 2, 3, 5, 9).
      done: the block's S9.17 tests and the note's S9.17 own tests pass on debug and ASAN; the S3-S7
        lists and layers 1 and 2 pass as before
      tier: T2 · role: Critic
      handoff: done 2026-10-08: `src/channel.c`, `channel.h`, `channel.stub.php` and the ring
        `src/internal/zval_circular_buffer.c`. The layer 3 block of `tests/lists/S9.txt`: 93 reference
        tests, 63 passing in S9.17, 14 with `--XFAIL--` naming S9.18 and 16 naming S9.19 (`channel/044`,
        `049`, `056`, `062` pass early, note section 9); own tests `channel/088`-`098`; `channel/058`,
        `059` wait for TaskGroup in `S9.excluded`. The record is linked by `async_wait_link_outside()`
        with `ASYNC_CALLBACK_F_FRAME_UNLINKS`, and `async_wait_end()` aborts such a record (DECISIONS).
        Until S9.18, `recvAsync()` and `getIterator()` throw and `async_await_awaitable_of()` refuses a
        channel. Note section 8 gains items 13 (a delivering wait refused before it parks withdraws its
        value) and 14 (`send()`/`recv()` refuse in scheduler context up front). The collector's oracle
        call stays in the channel's wake. Critic (one major finding fixed: stale records at
        `async_wait_end()`), Code Reviewer, two quality Critics and a re-check. On CORE_REF 662dfe91919:
        debug 1311 PASS, 9 SKIP, 41 XFAIL; ASAN 1287 PASS, 34 SKIP, 40 XFAIL, before the last
        no-behaviour edits, after which the channel, scope and bailout groups ran again on ASAN (204
        PASS, 19 SKIP, 30 XFAIL); 0 unexpected.
- [x] S9.18 `recvAsync()`, `foreach` and `getIterator()`, the channel as an `await_*` item and a token
      (note section 4).
      done: the block's S9.18 tests and the note's S9.18 own tests pass on debug and ASAN; the S3-S7
        lists and layers 1 and 2 pass as before
      tier: T2 · role: Critic
      handoff: done 2026-10-08: the 14 tests naming S9.18 pass; own tests `channel/099`-`109`, `111`, `112`. A
        pending `recvAsync()` Future is a queue record with no coroutine plus a subscriber in its event (note
        section 4, "As built"). `close()` gives each waiter its own `ChannelException` (note section 8, item
        15) and `foreach` lets a cancellation queued before an explicit close propagate (item 16); both depart
        from TrueAsync (DECISIONS 2026-10-08). `future.c` frees a dying event's subscribers first: the
        Critic's use-after-free (`channel/108`). The re-check Critic found two more: a `recvAsync()` awaiter
        found never to wake while a sleeping producer held the channel, so the collector's edge from a channel
        to its queued Futures came now, not in S9.19 (`channel/112`; a second Critic moved it to the Future's
        side, a third gave `foreach`'s iterator a `get_gc`), and the close's walks reading past a queue a
        freed Future shrank. `channel/088` counts by WeakReference, as the held close exception changed its
        collected count. Critic, two quality Critics and three re-check Critics. On CORE_REF 3e61b9fc00e:
        debug 1353 PASS, 14 SKIP, 33 XFAIL; ASAN 1328 PASS, 40 SKIP, 32 XFAIL; 0 unexpected.
- [x] S9.19 The per-channel timers, the close at the global deadlock, the owner-scope binding and the
      close of a completed or cancelled scope's channels, CHANNEL's `collector_target`, S7's channel case (note
      sections 5, 6).
      done: the block's S9.19 tests and the note's S9.19 own tests pass on debug and ASAN; the S3-S7
        lists and layers 1 and 2 pass as before
      tier: T2 · role: Critic
      handoff: done 2026-10-08: the 16 tests naming S9.19 pass; own tests `channel/113`-`129`. A timer is
        armed at a park, a wake and a wait's end; only the park's submit may throw, elsewhere a failed
        submit leaves no timer (`async_reactor_try_submit_own()`; the Code Reviewer, DECISIONS). The value
        a timer's, the deadlock's or the scope's close rolls back waits in `dropped_value` for the free
        (`channel/127`). The collector counts a `foreach` parked in its first receive and
        `iterator_to_array()` (`CHANNEL_RECORD_F_HOLDS_CHANNEL`) and, in its frame walk, `FE_RESET_R`'s
        TMP operand, which the live ranges leave out (`channel/125`, DECISIONS); a user `Iterator`
        whose `rewind()` parks stays a miss. The scope's completion node is shared with the channel's
        reach (the Code Reviewer). The Critic's timer for a Future left starving at a reserved receiver's
        exit: `channel/128`. The re-check Critic's false report, a cancelled scope's free waiting for a
        child scope's object: `channel/129`. Format fixed for S9.18's lines too. Code Reviewer, Critic,
        two quality Critics, a re-check Critic. On CORE_REF 77dbfc061f3:
        debug 1388 PASS, 14 SKIP, 17 XFAIL; ASAN 1363 PASS, 40 SKIP, 16 XFAIL; 0 unexpected.
- [x] S9.20 Layer review: Critic after S9.17-S9.19, coverage, Mull on the layer's diff, the fuzz
      oracle over 100 seeds, the measurements of note section 9.
      done: the layer's Done when holds on the day; survivors killed or explained
      tier: T2 · role: Critic
      handoff: done 2026-10-09. Edmond's two answers built: a closed channel keeps only its reason, and
        `await_outcome()` gives each `await_*` reader a `ChannelException` of its own (option Б,
        `channel/131`-`134`); the cancel or dispose of a completed scope reaches its child scopes, a cancelled
        one whose coroutines unwind left open (`scope/132`-`141`, `channel/130`, `136`; the free of an idle
        parent's object closes a held child, `scope/141`; a finally handler's run scope is left to finish,
        `scope/140`). `foreach` leaves the next value in the channel after a throwing destructor
        (`channel/135`); `await/142`, `143` cover the owned outcome. Coverage (pocs-dbg-cov): src 94.8 % (8212
        of 8665 lines), the layer's lines 957 of 991, the misses failure paths. Mull on the layer's diff:
        channel.c 105 mutants, 4 survivors, loop steps of the delivering sender's queue walk (the sender is
        always first) and of two test-hook helpers; scope.c survivors at 562 and 592 killed by `channel/136`
        and `scope/139`; 1541 (the collector's walk of one child's holders) and 1558 (a reach that only adds)
        explained; await, future, collector, reactor, scheduler 1 survivor, `future.c:278` (a second channel
        waiter's subscriber). Fuzz, 100 seeds over `channel/` and `collector/` (211 tests): the failed seeds
        were `channel/117` and `129` cancelling their member before it started, on `ba6412c` too, fixed
        (DECISIONS 2026-10-09); the 33 tests with a new diagnostic are the same 33 on `ba6412c`, order
        artifacts; 100 seeds over the 24 new and changed tests: 0 failed. B15 0.793 and B16 0.949 of the
        reference's instructions, no allocation per message (`dev/BENCHMARKS.md`). Layer Critic, a second
        Critic, two quality Critics, the final Critic and a re-check. Open for S9.21: a stale CHANNEL record
        left by a bailout a shutdown function caught takes the next `send()`'s value at the next wait's
        `async_wait_end()`, whose abort wakes nobody (the layer Critic, traced, not run). On CORE_REF
        77dbfc061f3: debug 1406 PASS, 14 SKIP, 17 XFAIL and `bailout/017`'s race (DECISIONS 2026-10-09, fixed
        after the lane); ASAN 1379 PASS, 40 SKIP, 16 XFAIL, 2 passed on retry, and `signal/024` at its time
        limit while busy loops ran beside it (passes alone); then the 24 new and changed tests pass on debug.
- [x] S9.21 Security pass by `dev/SECURITY.md`.
      done: a journal entry per checklist item; findings fixed with a test or recorded
      tier: T2 · role: —
      handoff: done 2026-10-09: journal entries in `SECURITY.md`, by checklist item, scripts run on the debug
        and ASAN builds. Fixed: the channel's, the Future's, the FutureState's and the Timeout's `free_obj`
        run `zend_object_std_dtor()` first, so a destructor of a value they release no longer reaches the
        object through a `WeakReference` (a use after free on ASAN; `channel/137`, `138`, `future/123`, `124`,
        `await/144`); one iterator stepped by two coroutines frees each value once (`channel/139`, `140`). The
        stale CHANNEL record after a caught bailout does not survive main's finish. Recorded, as TrueAsync's:
        the queues' O(N^2) for N pending `recvAsync()` Futures, cancelled or reserved receivers. Critic on the
        fixes, two quality Critics and a re-check. On CORE_REF 77dbfc061f3: debug 1414 PASS, 14 SKIP, 17
        XFAIL, 0 unexpected; ASAN 1387 PASS, 40 SKIP, 16 XFAIL, 1 passed on retry, and `signal/024` at its
        time limit while orphaned test processes of earlier runs held two of four cores (passes alone, three
        runs of three).
- [x] S9.22 A dropped `recvAsync()` Future takes the channel's timer along when no side starves.
      done: an idle channel stays open after its last pending Future is dropped, with a test
      tier: T1 · role: Critic
      handoff: done 2026-10-09: Edmond's answer to the PLAN open question (06:38, "если это баг конечно нужно
        исправить"): a dropped `recvAsync()` Future and a parked waiter's abort disarm the timer through
        `channel_timer_disarm_if_idle()` when its side starves no more; neither arms one, so a Future queued
        still arms nothing (`channel/141`). A departure from TrueAsync, whose dispose leaves the timer
        (`channel.c:297-320`). The wrong-side timer the helper now also drops (a reserved receiver not yet
        run, a dropped Future, a parked sender) has no own test: it lasts only until the woken receiver runs
        (the Critic). Edmond kept the queues' arrays (06:40). On CORE_REF 77dbfc061f3, before the Critic's
        fixes: debug 1415 PASS, 14 SKIP, 17 XFAIL; ASAN 1390 PASS, 40 SKIP, 16 XFAIL; 0 unexpected on both.
        After them: `channel/`, `await/` and `collector/` pass on debug (357) and ASAN (356, 1 SKIP), and 30
        fuzz seeds over `channel/141` fail none.

- [x] S9.23 A running finally handler outlives every scope cancel; only a deadline stops it.
      done: the shield and the deadline built with tests; Critic on the design, the code and the fixes
      tier: T2 · role: Critic
      handoff: done 2026-10-09: Edmond 06:50 ("a separate flag for a special Scope, which a person cannot
        set"); design in `dev/plans/S9-scope.md` section 13, departure section 9 item 29.
        `ASYNC_SCOPE_F_FINALLY_RUN` on a finally run's scope, from its start to its last worker's leave
        (`async_scope_finally_run_end()`); every scope cancel and the error route skip it,
        `async_scope_cancel()` of a flagged scope returns, and a dispose timer's fire cancels every coroutine
        of the runs below it, never safely, closing them; a close keeps the timer while a run is below. Own
        tests `scope/142`-`157`; `scope/117` (ours, `changed:2026-10-09`) stops its handler with the deadline.
        Open for Edmond (PLAN open questions): a finally run that starts after the deadline fired, a handler
        that catches the deadline and waits again, and a coroutine a handler left running under a cancelled
        scope. Critics: two on the design, one on the code, a re-check, two quality Critics. On CORE_REF
        77dbfc061f3: debug 1430 PASS, 14 SKIP, 17 XFAIL; ASAN 1405 PASS, 40 SKIP, 16 XFAIL; 0 unexpected on
        both; after the quality fixes `scope/`, `coroutine/`, `spawnWith/` and `collector/` pass on debug
        (291) and ASAN (288, 3 SKIP), and 30 fuzz seeds over the 18 new and changed tests fail none.

- [x] S9.24 After a scope's deadline interrupts its members, no finally handler is called under it.
      done: the flag built with tests; Critic on the design, the code, re-checks, two quality Critics
      tier: T2 · role: Critic
      handoff: done 2026-10-09: Edmond 07:57 ("the flag says no other finally starts any more"); design in
        `dev/plans/S9-scope.md` section 14. `ASYNC_SCOPE_F_DEADLINE_PASSED`, set by a dispose timer's fire on
        an unsafe, not request-lifetime scope whose cancel interrupts a member, and by the fire's walk on every
        finally run it stops; `finally_handler_call()` calls nothing under it, and the run still releases the
        handlers where it did. Own tests `scope/158`-`172`; `scope/151` (ours, `changed:2026-10-09`) now
        expects the idle child's handler not called. Edmond agreed the safe-scope default (2026-10-09 10:04).
        Open for Edmond (PLAN open questions): the subtree-wide default, a coroutine spawned into the scope
        after the fire. Found on the way: a finally handler's captured object's destructor cannot wait (S9.25).
        Critics: one on the design, one on the code, three re-checks, two quality Critics. On CORE_REF
        77dbfc061f3, over S10.4 (17b6d11): debug 1456 PASS, 14 SKIP, 11 XFAIL; ASAN 1431 PASS, 40 SKIP, 10
        XFAIL; 0 unexpected on both; 30 fuzz seeds over the 16 new and changed tests fail none.

- [x] S9.25 A finally run releases its handlers where a destructor may wait.
      done: a destructor of an object a finally handler holds may suspend, with and without a deadline
      tier: T1 · role: Critic
      handoff: done 2026-10-09; design in `dev/plans/S9-scope.md` section 15. The last worker of a run that ran
        lets go of what the walk holds in its body (`iterator_dispose()`), before the run ends and before the
        walk's error is thrown, so a destructor there may suspend and no scope cancel interrupts it; after an
        `exit()` the tick lets go, so that a destructor's error cannot take the exit's place. Before S9.25
        the microtask's release in the tick ran such a destructor in scheduler context, as TrueAsync's does
        (probed on ours and on TrueAsync). Own tests `scope/173`-`184`. Open for Edmond (PLAN open questions): a destructor
        that starts after the fire is bounded only by a second timer; a coroutine cancelled before it ran
        releases what it holds where nothing can suspend. Found on the way and fixed:
        a handler's `exit()` after a wait was lost when a destructor of what it held threw (`scope/184`).
        Critics: one on the design, one on the code, three re-checks, two quality Critics, the Sage. On CORE_REF
        77dbfc061f3, over 8b9ba78: debug 1468 PASS, 19 SKIP, 11 XFAIL; ASAN 1443 PASS, 45 SKIP, 10 XFAIL; 0
        unexpected on both; 30 fuzz seeds over the 12 new tests fail none.

- [x] S9.26 Design note `dev/plans/S9-taskgroup.md` (layer 4, TaskGroup and TaskSet).
      done: the note pushed; every Critic finding fixed or answered in the note; the questions of its
        section 10 answered by Edmond
      tier: T2 · role: Critic
      handoff: done 2026-10-09: the layer is TrueAsync's `task_group.c` on our wait model, with 30 departures
        (section 8), decided by Edmond one question at a time but the internal ones, which he accepted together
        at 07:26 (DECISIONS 2026-10-09 S9.26); 38 questions answered. A task does not hold the group, so dropping it cancels its
        tasks; the destructor never waits: it seals the group, and the closing holds the object until the
        finally handlers, started once at the completion, have ended and the unhandled errors are reported. `await_*` items and tokens
        take `Completable` only (S9.27), so the channel stops being one. Two Critics on the note. Probes in
        `/mnt/project-files/s9/probes/s9.taskgroup/`. 71 reference tests for the list; `channel/058` stays
        excluded. The finally run's end builds on S9.25's release point.
- [x] S9.27 `await_*` items and tokens take `Completable` only.
      done: the item check and the token ZPP take `Completable`; the channel's item and token code gone;
        `channel/100`, `103`, `111`, `132`-`134` and `await/128` changed with `changed:` and DECISIONS
      tier: T1 · role: Critic
      handoff: done 2026-10-09: the six `await_*`, `Scope::awaitCompletion()` and `awaitAfterCancellation()` take
        `Completable` tokens, `timeout()` returns `Completable`, the item message names `Completable`; the channel's
        type bit, its branches in `await.c` and the notify of its event vector are gone, with debug asserts in
        their place (DECISIONS 2026-10-09 S9.27). Own tests `channel/142`-`144`, `await/145` open S9.txt's layer
        4 block; the group's refusals come in S9.28. Critic, Sage and two quality Critics on the commit;
        `channel/134`, which only overlapped `channel/132` and `await/115`, removed (Edmond, 12:48).
- [x] S9.28 The layer 4 list block extended and `src/task_group.c` without its waits.
      done: after S9.25; the layer 4 block S9.27 opened in `S9.txt` extended, with `--XFAIL--` naming S9.28 or
        S9.29, `channel/059` out
        of `S9.excluded`, `task_group/040` into it, `channel/058`'s reason changed; the group, its tasks,
        results, errors, closing, finally handlers and destructor built with tests (note sections 2, 3, 5, 6);
        a cancel leaving a coroutine whose body has finished alone, for every scope (section 8, item 28)
      tier: T2 · role: Critic
      handoff: done 2026-10-09: `src/task_group.c` ports TrueAsync's group onto our scopes, Futures and
        callbacks with the note's departures; the destructor never waits, the closing ends at the finally
        run's end, the errors no read took are reported from a reporter coroutine. A coroutine whose body
        returned keeps its outcome when cancelled (`ASYNC_COROUTINE_F_BODY_RETURNED`); `map()` on a Future
        nobody else holds completes (as TrueAsync, Edmond 13:49); a numeric string key is the integer key
        (Edmond 13:56). S9.txt's layer 4 block: 71 reference tests (12 `--XFAIL--` naming S9.29,
        `task_set/011` changed) and 68 own; `channel/059` in, `task_group/040` out. Debug 1594 PASS, ASAN
        1566 PASS, 0 unexpected. Critic, a second Critic, two quality Critics and a re-check Critic on the
        commit (DECISIONS 2026-10-09 S9.28). For Edmond: the two Open questions S9.28 added.
- [x] S9.29 The TASK_GROUP wait kind: `awaitCompletion()`, `foreach`, `spawn()` on a full queue, the collector.
      done: the S9.29 XFAILs pass; the collector finds a coroutine parked on a group nobody else reaches
      tier: T2 · role: Critic
      handoff: done 2026-10-09: the group's two wait queues (`slot_waiters`, `waiters`) hold its waiters' records
        with the TASK_GROUP kind (info, unlink, collector target); the channel's queue became the shared
        `async_wait_queue_t`. Spawners take rooms in order: a newcomer waits behind a parked or woken one; a
        scope stopped from outside seals the group at any task end; `awaitCompletion()` in an own task throws
        (DECISIONS 2026-10-09 S9.29). The 12 S9.29 XFAILs pass, `task_group/035` changed; own tests
        `task_group/099`-`137`. Debug 1654 PASS, ASAN 1622 PASS, 0 unexpected. Critic, two quality Critics and a
        re-check Critic on the commit.
- [x] S9.30 Layer 4 review and its documentation in `true-async-doc`.
      done: Critic over S9.27-S9.29, coverage, Mull, 100 fuzz seeds, measurements B17 and B18, the doc pushed
      tier: T2 · role: Critic
      handoff: done 2026-10-09. Bugs fixed (DECISIONS 2026-10-09 S9.30): a use after free of the report made under
        the graceful shutdown (`task_group/138`); a reporter queued before an exit lost its errors, now handed over
        by a finish handler, not the dispose, which a holder can delay past the request (`139`, `146`, `147`); an
        `exit()` in a task stored UNDEF (`145`). The six bailout tests run on ASAN again: run-tests `--asan` sets
        `USE_TRACKED_ALLOC`. Coverage (pocs-dbg-cov): src 94.6 % (9425 of 9958 lines), the layer's lines 1182 of
        1254, the misses failure paths. Mull on `task_group.c` since `5b7f9a3`: 148 mutants, 36 survivors; 22
        killed by `task_group/140`, `143`, `148`-`158` and `task_set/035` (7 on ASAN only, Mull builds debug),
        14 explained (lines of the commit): 1214 equivalent, 1910 unkillable (needs about 4e9 queued tasks),
        422 `<=` equivalent while the waiter is listed, 436 needs a group freed with
        reads pending (only the request's end), 510 the collector's reach from a Future to its group (the group
        is held elsewhere), 671 a refused enqueue, 691-695 room counts (a pass wakes more or fewer spawners, each
        re-checks), 1080 a dispose after an earlier callback threw (no test reaches it), 1363 several child
        scopes. Fuzz: 100 seeds over 260
        layer tests, 0 failed, the 30 tests with a new diagnostic the same on `11a9776`; 100 seeds over the 36
        new and changed tests, 0 failed, 2 diagnostics, both also on `11a9776`. B17 0.005 and B18 0.003 of the
        reference's instructions at 10 000 tasks, 6 allocations per task against 8 (`dev/BENCHMARKS.md`). Docs:
        `true-async-doc` `ru/concepts/task-group.md`, `task-set.md`, links from scope, channels, await and
        coroutines. Layer Critic, a Critic on the fixes, two quality Critics. Open: Edmond's call on the
        closing after `exit()` (Open questions); the other lists' bailout tests keep `skip-on:*-asan` (S3, S5,
        S9 layers 1-3, S10), a step of their own. On CORE_REF 3aa1cd120f4: debug 1683 PASS, ASAN 1657 PASS, 0
        unexpected.
- [x] S9.31 Layer 4 security pass by `dev/SECURITY.md`.
      done: a journal entry per checklist item of the note's section 7; findings fixed with a test or recorded
      tier: T2 · role: —
      handoff: done 2026-10-12: journal entries in `SECURITY.md` by checklist item and the note's six points,
        scripts run on the debug and ASAN builds, a second reader of the code. Fixed (DECISIONS 2026-10-12
        S9.31): a GC run that a release starts lets other coroutines run while the caller waits, so `all()` and
        `any()` walked a table a spawn grew meanwhile (a use after free on ASAN; `all()`: `task_group/159`) and
        `joinAll()` and `joinAny()` took entries spawned meanwhile, running ones included (`joinAll()`:
        `task_set/036`); a constructor re-entered from its warning handler leaked a `Scope` reference (`160`);
        after `exit()` the drain started queued tasks (`161`-`164`, note section 8 item 31); by reading, the
        drain's release of a refused start and the walks over the pending Futures. Seen, the open question's class: a destructor that waits where a finished task's
        teardown releases. Open: the earlier layers' walks against a GC run (S9.33).
        On CORE_REF 3aa1cd120f4: debug 1690 PASS, ASAN 1664 PASS, 0 unexpected. Two Critics on the design and
        two on quality; each finding fixed or answered in `SECURITY.md`.
- [x] S9.32 Design note `dev/plans/S9-pool.md` (layer 5, pools).
      done: the note pushed; every Critic finding fixed or answered in the note; its questions answered by
        Edmond
      tier: T2 · role: Critic → Sage
      handoff: done 2026-10-12: TrueAsync's `Async\Pool` on our wait model with no core change, 14 departures
        (note section 8, DECISIONS 2026-10-12 S9.32). Found in TrueAsync by probes on its debug build
        (`/mnt/project-files/s9/probes/s9.pool/`): a healthcheck that suspends ends the request with `DeadlockError`;
        a double `release()` hands one object to two coroutines and wraps `activeCount()` to 4294967295; hook,
        healthcheck and strategy exceptions vanish, cancellations inside hooks too; `shouldRecover()` is never
        called. Edmond answered the three questions at 18:38-18:39: C (an `Exception` rejects, an `Error`
        passes), B (handed-out objects checked), B (`shouldRecover()` asked). Critic: 4 high findings fixed (the
        check's error route, its reservations, the GC's `free_obj` order); the Sage kept `acquire(int $timeout)`.
- [x] S9.34 The layer 5 block of `tests/lists/S9.txt` and `src/pool.c` without its waits (note section 7).
      done: the 70 reference tests listed, those needing S9.35 with `--XFAIL--`, the changed ones with
        `changed:` and DECISIONS lines; the pool, its hooks, the circuit breaker and the release check built
        with own tests
      tier: T2 · role: Critic
      handoff: done 2026-10-12 (DECISIONS 2026-10-12 S9.34): of the 70 reference tests 46 pass and 24 are
        `--XFAIL--` for S9.35 (the waits, the healthcheck); 6 changed (`024`, `048`, `049`, `054`, `056`, `068`).
        35 own tests (`070`-`104`). The release check (question 2) is about 60 lines with comments: a weak-key
        count per object, a count per PHP resource handle, a number for other values. Two Critics: a use after
        free when the pool closed during the factory, `release(null)` passing while an object was out, chaining
        into a shared cancellation, by-reference factories and hooks, exits lost in `close()`; all fixed with
        tests. `048` and `049` as XFAIL each run 10 s until S9.35 (their keeper's `delay(10000)`). On CORE_REF
        `d06ab339297`: debug 1855 PASS, 0 unexpected; the pool's 105 tests on ASAN 81 PASS, 24 XFAIL.
- [x] S9.35 The POOL wait kind, the healthcheck coroutine and the collector (note sections 4-6).
      done: the S9.35 XFAILs pass; the collector finds a waiter on a pool nobody reaches
      tier: T2 · role: Critic
      handoff: done 2026-10-12 (DECISIONS 2026-10-12 S9.35): the 24 XFAILs pass, all 70 reference tests now pass;
        19 own tests (`105`-`123`). The timeout is a Timer record beside the POOL record
        (`async_reactor_timeout_link()`), one deadline per `acquire()` call (note section 8, item 15). Critic:
        the check's last release sent a destructor's exception into the scope route, a rejection then a circuit
        refusal left a waiter beside a free place, a hook swallowing close()'s cancellation kept the check going;
        all fixed with tests; the second Critic: a swallowed graceful shutdown (`123`). On CORE_REF `46369b6a6a5`:
        debug 1897 PASS, 0 unexpected, and pool, collector and channel on ASAN 353 PASS before that last fix; the
        pool's 124 tests after it, debug and ASAN, 0 unexpected.
- [x] S9.36 Layer 5 review and its documentation in `true-async-doc`.
      done: Critic over S9.34-S9.35, coverage, Mull, 100 fuzz seeds, B21 and B22, the doc pages corrected (note
        section 9)
      tier: T2 · role: Critic
      handoff: done 2026-10-13 (DECISIONS 2026-10-12 S9.36). Critics: an exception in flight went beneath the
        destructors' (a pre-warm factory's exception, a cancellation, a strategy's exception in `release()`), and a
        pool closed while `shouldRecover()` suspended still called the factory; fixed by one rule,
        `pool_exception_settle()` (`pool/129`-`135`). Coverage on `bc889c5`: src 95.6 %, the layer's lines 633 of
        668; `124`-`128` cover five misses. Mull on `pool.c`: 83 mutants, 12 survivors, 7 killed by `136`-`143`,
        the mutants at lines 205, 741, 779, 1105 and 1210 explained in DECISIONS' S9.36 Mull entry. Fuzz: 100 seeds
        over the 144 pool tests, 0 failed. B21 1.067 and B22 1.566 of the reference's instructions
        (`dev/BENCHMARKS.md`). Docs: `true-async-doc` `ru/concepts/pool.md`. On CORE_REF `46369b6a6a5`: debug 1918
        PASS, ASAN 1892 PASS, 0 unexpected; the pool's 144 tests on both after the last change.
- [x] S9.37 Layer 5 security pass by `dev/SECURITY.md`.
      done: a journal entry per item of the note's section 7; findings fixed with a test or recorded
      tier: T2 · role: —
      handoff: done 2026-10-13 (`SECURITY.md` journal, DECISIONS 2026-10-13 S9.37): scripts by checklist item and the
        note's six points on the debug and ASAN builds, a second reader with 300 seeds of a random hook driver (140
        on ASAN). Fixed: the constructor's pre-warm, whose factory reaches the pool through `debug_backtrace()`, went
        past `max` when the factory acquired (an assertion of the idle buffer, `pool/144`), kept filling a pool
        the factory closed (`145`) and woke no waiter parked meanwhile (`146`, the Critic); it works as the
        healthcheck's refill does. Kept: `close()` destroys the other idle resources after an `exit()` in the
        destructor callable. On CORE_REF `46369b6a6a5`: debug 1921 PASS, ASAN 1895 PASS, 0 unexpected.
- [x] S9.33 The C walks of `scope.c`, `channel.c`, `future.c`, `await.c` and `context.c` checked against a GC
      run that a release inside them starts, and against the caller's cancellation during its wait
      (`SECURITY.md`, Open findings, S9.31).
      done: each walk fixed with a test, or recorded in `SECURITY.md` with the reason
      tier: T2 · role: Critic
      handoff: done 2026-10-12 (`SECURITY.md` journal, DECISIONS 2026-10-12 S9.33): five uses after free on ASAN,
        fixed with tests: the scope cancel's walks (`scope/191`, `192`, `task_group/166`) and the close of a channel
        by its owner scope's free (`scope/190`, `channel/155`) run in scheduler context; a key a Traversable gives
        twice releases the replaced result after the store (`await/148`). Six behaviour defects: `recvAsync()` and a
        `send()` to a pending one decrement the taken value (`channel/156`, `150`); a served Future's hold is
        released last and a promised slot is marked committed at the promise (`152`, `153`, S14.1's finding);
        `recv()` decrements a coroutine token (`151`; the VM's release of `$this` keeps the window, `154` XFAIL);
        `await_any_or_fail()` chains in scheduler context (`await/149`). By reading, `iterator.c` stores a worker's
        exception before chaining it; `context.c` and `future.c` clean. Open: whether the GC wait should defer a
        cancellation (Open questions). On CORE_REF 96a52ba65f4: debug 1772 PASS, ASAN 1746 PASS, 0 unexpected;
        100 fuzz seeds over the new tests, 0 failed. Critics and Sage.
- [x] S9.38 The core's automatic GC run in a coroutine does not wait: a root that finds the buffer full queues
      the run and goes on; a coroutine 100 000 roots past the queued run without yielding waits; an explicit
      `gc_collect_cycles()` waits as before (Edmond 2026-10-12, a Critic agreeing).
      done: the core change with tests on a new `async-core-io` pin; `channel/154` passes; the RFC's GC text and
        DECISIONS say so
      tier: T2 · role: Critic
      result 2026-10-12: core `async-core` `b318dcd834c`, `async-core-io-2026-10-12-4` `d06ab339297` (with S8's
        comments `c50c23baf28`); the count is per coroutine and restarts after any wait (Critic). Core tests 317
        debug, 311 ASAN pass (`ext/test_scheduler`, `Zend/tests/gc*`, fibers). Debug 1774 PASS, ASAN 1748 PASS, 0
        unexpected; 100 fuzz seeds over the 20 changed tests, 0 failed, 0 new diagnostics after `task_set/036`'s
        fix; ten of them fail on ASAN with S9.33's fixes reverted. RFC text in php-async-core-rfc. Two Critics, Sage.
- [x] S9.39 Design note `dev/plans/S9-iterate.md` (layer 6, iterators).
      done: the note pushed; every Critic finding fixed or answered in the note; its question answered by Edmond
      tier: T2 · role: Critic
      handoff: done 2026-10-13 (DECISIONS 2026-10-13 S9.39): the layer is TrueAsync's `iterate()` alone, on S9.6's
        core, no core change. Probes on the reference build (`/mnt/project-files/s9/probes/s9.iterate/`, i1-i22):
        a caller's cancel waits for the whole walk with `cancelPending: false`, `exit()` in a callback is swallowed,
        a spawned coroutine's error during the walk ends the request, two leaks. Critic: 7 findings fixed in the note
        (DISPOSE_SAFELY cleared as `async.c:1016`, the record's target and owner, a bailout during the wait, the
        exception merge). Edmond answered question 1 at 03:38: B, a cancel of the caller stops the walk.
- [x] S9.40 The layer 6 block of `tests/lists/S9.txt` and `iterate()` (note sections 3, 5).
      done: the 20 reference tests pass on debug and ASAN; own tests for each row of note sections 2 and 6 and
        for question 1's answer
      tier: T2 · role: Critic
      handoff: done 2026-10-13 (DECISIONS 2026-10-13 S9.40, note section 3 "As built"): 20 reference tests and 32
        own (`tests/iterate/020`-`051`) on core `async-core-io-2026-10-13-2`. Debug 1982 PASS, ASAN 1956 PASS, 0
        unexpected. First Critic: a child's error while the cancelled caller waits in the queue ended the request
        (fixed by a scope subscriber with the new `ASYNC_CALLBACK_F_TAKES_ERROR`, `049`), `$cancelPending` after a
        safe cancel from above could hang (`050`); two findings did not reproduce. Second Critic: a freed scope
        read on a refused spawn, `$cancelPending` skipped when the last worker ended cancelled (`051`), note text.
- [x] S9.41 Layer 6 review and its documentation in `true-async-doc`.
      done: Critic over S9.40, coverage, Mull, 100 fuzz seeds, B23, a doc page with note section 6's errors corrected
      tier: T2 · role: Critic
      handoff: done 2026-10-13 (DECISIONS 2026-10-13 S9.41, note section 3 "As built"). Critics: a destructor's error
        at the walk's release hit the settle assertion (`iterate/054`); a caller's cancel or a child's error left the
        walk taking elements when a callback caught its cancellation (`055`, `056`); `$cancelPending` re-cancelled a
        child cleaning up after the walk's cancel (`057`) and, once spared, a child spawned after it (`058`).
        Coverage (iterate tests): the layer's lines 118 of 132 in `iterator.c`; `052`, `053` cover two misses; the
        rest need a refused worker start, a bailout holding a route error, a scope without a file name, an
        exception pending at entry or an iterator refused without one. Mull on `iterator.c` and `true_async.c`:
        12 mutants on the changed lines, 2 survivors explained in DECISIONS. Fuzz: 100 seeds over 54 tests, 0
        failed (`049` made null-safe for a fuzzed order). B23 1.058, B23-10 1.060 of the reference's instructions
        (`dev/BENCHMARKS.md`). Docs: `true-async-doc` `ru/reference/functions/iterate.md`. On CORE_REF
        `57fed0673c0`: debug 1988 PASS, ASAN 1962 PASS, 0 unexpected, before the second Critic's fix; iterate,
        scope, channel and task_group 573 PASS on debug and iterate with task_group 228 PASS on ASAN after it.
        Follow-up (Edmond 2026-10-13 06:00, B): a cancellation a callback throws itself, its worker not cancelled,
        is the walk's exception (`iterate/059`, `060`); debug 1991 PASS, ASAN 1965 PASS before the Critic's merge
        fix, iterate, pool, internal, scope and task_group 642 PASS on debug and 377 on ASAN after it.
- [x] S9.42 Layer 6 security pass by `dev/SECURITY.md` (note section 5).
      done: a journal entry per checklist item; findings fixed with a test or recorded
      tier: T2 · role: —
      handoff: done 2026-10-13 (`SECURITY.md` journal, DECISIONS 2026-10-13 S9.42): about 40 scripts by checklist item
        and the note's section 5 on the debug and ASAN builds, a second reader with 32 scripts and 720 seeds of a
        random driver on ASAN, 20 fuzz seeds over the layer's tests on ASAN; no memory defect. Fixed: an exception
        the caller reaches through the one it caught (rethrown by a coroutine a callback spawned, or by a pool hook and
        the destructor) also printed "Uncaught" with exit code 255 (`iterate/061`-`063`, `pool/147`). Known limit: a
        second route error a later walk exception wraps still prints. On CORE_REF `dcb1ea7d64d`: debug 1995 PASS,
        ASAN 1969 PASS, 0 unexpected; iterate and pool 212 PASS on debug after the second Critic's fixes.
- [x] S9.43 Design note `dev/plans/S9-fs-watcher.md` (layer 7, FileSystemWatcher).
      done: the note pushed; every Critic finding fixed or answered in the note
      tier: T2 · role: Critic
      handoff: done 2026-10-13 (DECISIONS 2026-10-13 S9.43): Linux reads inotify itself on the POLL op, one
        descriptor per request; Windows needs a directory-change op from bukka and ior (S9.45); macOS and the BSDs
        refused in the first version. Probes on the reference build (`/mnt/project-files/s9/probes/s9.fswatch/`,
        f1-f16): an open watcher keeps the request alive (a deadlock, the script's end and `exit()` hang), a second
        constructor leaks a watch, a removed root leaves `foreach` waiting, `$extensions` works only with debounce.
        Departures in note section 6 decided by Edmond's rules. Critic: 10 findings fixed in the note (parked
        iterators counted as waits instead of moving the op between lists, the fork rebuild before `own`
        is resubmitted, the teardown after a fatal error, a shared watch descriptor per watcher, prefix and root,
        subdirectories' own events dropped, the overflow event's rules); `$extensions` without a debounce became
        question 1. Second Critic: the counter of its own (the fork zeroes the triggers'), the fork
        re-arms the same op as the wakeup. The note waits for Edmond's agreement and his answer.
- [x] S9.44 The layer 7 block of `tests/lists/S9.txt` and `Async\FileSystemWatcher` on Linux (note sections 3, 6).
      done: the 13 reference tests and an own test per row of note sections 2 and 6 pass on debug and ASAN
      tier: T2 · role: Critic
      handoff: done 2026-10-13 (DECISIONS 2026-10-13 S9.44): Edmond agreed the note and answered question 1 with B
        (`$extensions` in every mode). The reactor takes a completed event off its list before a custom completion;
        a forked child rebuilds before a watch is added or an iterator waits, and removes no watch before that. Own
        tests `fs_watcher/015`-`052`. Critics: a watcher freed after a fatal error left the registry pointing at it
        (`049`, ASAN), a debounced burst was lost on close (`050`), a shared iterator leaked an event (`051`), the
        coalesce pop scanned from index 0. Other platforms throw; the tests that construct a watcher carry
        `xfail-on:pocs-win(S9.45)` (the first push gave them `skip-on`, which only allows a SKIPIF: Windows red).
- [ ] S9.45 Windows: the directory-change op in bukka's API and ior, the watcher on it (note section 3, "Windows").
      done: the layer's tests run on `pocs-win`; bukka PRs opened by Edmond; told to Edmond before the core slot
      tier: T2 · role: Critic
      handoff: 2026-10-13 core `async-core-io-2026-10-13-8` `244b1a2ecf3`, ior `dir-changes` `59b918ef189`
        (RFC-CHANGES 39, PR texts `/mnt/project-files/notes/s9-45/`); the watcher's Windows branch, the xfail-on tags
        removed; told to Edmond 08:30. Open: the PRs, opened by Edmond.
- [ ] S9.46 Layer 7 review and its documentation in `true-async-doc` (note section 5).
      done: Critic over S9.44 and S9.45, coverage, Mull, 100 fuzz seeds, B24, a doc page with note section 6
      tier: T2 · role: Critic
- [ ] S9.47 Layer 7 security pass by `dev/SECURITY.md` (note section 5).
      done: a journal entry per checklist item; findings fixed with a test or recorded
      tier: T2 · role: —

## S10 — Beyond the RFCs  [x]

One decision per fork feature that needs core changes: PDO pool, per-coroutine output buffers,
pgsql, `Fiber::getCoroutine()`, `zend_sigaction` hook, thread pool, Windows, and what the inventory
of the fork's core diff found besides (`dev/plans/S10.md` section 9). Each becomes an RFC
change, an upstream fix, an extension-level design, or "not in the first version".
Done when: every item of the note's section 1 has its outcome in `DECISIONS.md` (2026-10-08); the outcomes
built in S10 pass on debug and ASAN with the S3-S9 lists as before; every reference group of the note's
section 10 is in a list or an exclusion with its reason (`iterate`, `task_group`, `task_set` and `pool`
are S9's later layers).
Tier: T2. Roles: Critic and Sage on S10.1, Critic after every coding step.
Tests: interleaved
Base: 3c859b5
Notes: dev/plans/S10.md
Active: none; stage closed with S10.6

- [x] S10.1 Decision note `dev/plans/S10.md`: per item what TrueAsync does, what the pinned core has,
      the options and the proposed outcome; the inventory of the fork's other core changes.
      done: the note pushed; every Critic finding fixed or answered in the note; Edmond's answers to
        the 11 questions of section 11 recorded in `DECISIONS.md`
      tier: T2 · role: Critic → Sage
      Critic 2026-10-08 round 1: 3 high (output buffers on a private core branch against P1.1; the INI
        handlers start before the launch; TrueAsync's bypass against our listed tests and Fibers), 6
        medium (the headers leak is php-src's, the M9 request, the lists, the questions). Fixed.
      Sage 2026-10-08: output buffers in the extension (C) over an RFC change (A), which would widen a
        public RFC's scope; C keeps its state in our own fields, not a hack; `Fiber::getCoroutine()`
        added by the extension and Windows signals in the extension accepted. Final.
      Critic 2026-10-08 round 2: write-through has no defined model and C gives bypass only; fibers
        need their resumer's stack. Section 2 rewritten: C, bypass, fibers on their resumer's stack.
      Critic 2026-10-08 round 3 (section 2): a fiber runs after its caller is switched out, so its
        stack follows `caller_coroutine`; the invariant and the one swap helper stated; a fatal error's
        message flushed, not discarded; `scheduler/053` and `io_provider/011` change too. Fixed; four
        design points left to the output buffers' own note.
      handoff: done 2026-10-08: Edmond approved the note (17:40); output buffers are not built
        (`DECISIONS.md` 2026-10-08), so their step is gone. The research reports are in
        `/mnt/project-files/notes/s10/`.
- [x] S10.2 The frozen lists `tests/lists/S10.txt` and `S10.excluded` (note section 10).
      done: `check-lists.py` passes; every reference group of note section 10 is in a list or an
        exclusion with its reason
      tier: T1 · role: Critic
      result 2026-10-08: `S10.txt` 16 tests (`cleanup` 4, `fork` 2, `socket` 3, `fiber` 7),
        `S10.excluded` 318; `output_buffer/007`, `008` appended to `S6.txt`; the PDO pool and thread
        lines of `S6.excluded` and the output buffer lines of `S3.excluded` cite the 2026-10-08
        outcomes. `S10.txt` and `output_buffer/007`, `008`: pocs-dbg 9 PASS, 3 SKIP (no IPv6 here),
        6 XFAIL (S10.4); pocs-asan 8 PASS, 4 SKIP (and `007`'s `--XLEAK--`), 6 XFAIL; 0 unexpected.
        `fiber/025` passes already, its parent is cancelled before the fiber starts. `socket/001`-`003` changed (DECISIONS) and
        skip without IPv6, as in this container; CI runs them.
      Critic 2026-10-08: `fork/002`, `003` skip on Windows and `output_buffer/007` (`--XLEAK--`) under
        ASAN, untagged; the socket SKIPIF borks without IPv6; `signal/008`, `009`, `012` still named
        S10; `socket/004` and `cleanup/005` had the wrong reason; the count and the done line were
        false; `fiber/025` proves nothing about `getCoroutine()`. Fixed; S10.4 adds its own test.
- [x] S10.3 The fixes that need no decision (note section 12): the HTTP headers leak on
      `php-src-fixes` (a core update), the signal handle count (moved to S10.3a), the run of note section 4 (b), the fiber
      frames reproducer; `socket/001`-`003` seen passing on CI.
      done: each fix with its test passes on debug and ASAN; `socket/001`-`003` PASS on the CI
        `pocs-dbg` and `pocs-asan` lanes; the S3-S9 lists pass as before; the core
        update's checks of WORKFLOW pass (S1 suites diffed, the bridge); Edmond's answer on `signal/031`
        after the run recorded
      tier: T2 · role: Critic
      result 2026-10-08: core `async-core-io-2026-10-08-3` `3e61b9fc00e` (-2 with `php-src-fixes`
        `68f790cb2d4` merged): the HTTP headers leak and the fiber GC of a file included from a
        function (the reproducer the note asked for), each with a `PHP-8.4` test red without the fix.
        Section 4 (b) run: confirmed, and a block during a watch is lost too; Edmond 18:06 «ок сделай
        я потом рассмотрю». The fix (which also settles the handle count of (c)) waits on his answer
        about `$old` (18:47 «я подумаю об этом...»), so it moved to S10.3a (the coordinator, 18:47).
        All lists: pocs-dbg 1326 PASS, 14 SKIP, 47 XFAIL; pocs-asan 1301 PASS, 40 SKIP, 46 XFAIL; 0
        unexpected. S1 suites, old core against new: no difference (debug 236, ASAN 235 PASS);
        `ext-scheduler-hook` 23 of 23 on debug and ASAN. `socket/001`-`003` on CI (3d6cc85, old core): the run
        prints no per-test lines, so from the counts: CI has 3 more PASS and 3 fewer SKIP than here
        on both lanes (pocs-dbg 1329 and 11, pocs-asan 1304 and 37), 0 unexpected; here the extra 3
        are these tests (no IPv6), and the other 11 dbg skips are 10 Windows-only tests and `curl/071`
        (libcurl below 8.11.1, as on ubuntu-24.04, which ci.yml pins). The step closes on these local
        runs and the old core's CI run; CI on this commit, the first on the new core (Windows
        included), follows the push.
        Found: `io_provider/032` loses bytes under load on the old core too (`php_stream_read()` drops
        what it took when the refill's wait is cancelled); its own thread. The php-src fix was dropped
        (Edmond, 2026-10-09): a cancelled read or write leaves gaps in the data, documented in
        true-async.github.io, and `032` now checks the cancellation and the close (`DECISIONS.md`, 2026-10-09).
      Critic 2026-10-08: the first signal fix returned the real mask in `$old`, which left a watched
        number blocked after a block-and-restore during the watch (run). Changed to the script's mask;
        the choice went to Edmond as a decision card.
- [x] S10.3a The signal fix of note section 4 (b) and (c), on the small path (Edmond 19:22): the
      script's unblock of a watched number kept for the last removal, in bukka's code (`io-hooks-fixes`
      and a PR branch to bukka, a core update), the extension's `reblocked` record removed.
      done: Edmond's answers recorded; `signal/018`, `031`, `033`-`035` and the core's
        `poll_signal_handle_script_unblock*.phpt` pass on debug and ASAN; the core update's checks pass
      tier: T2 · role: Critic
      result 2026-10-08: core `async-core-io-2026-10-08-4` `77dbfc061f3` (-3 with `io-hooks-fixes`
        `2a74924668c`); PR branch `signal-unblock-at-removal` `c44eccf72a` on bukka's `566a6833eb5`, PR
        text `/mnt/project-files/notes/signal-unblock-at-removal-pr.md` (Edmond opens it). `$old` stays
        the real mask (Edmond 19:22, «пока вообще не трогать маску»); a later block by the script takes
        a recorded unblock back (Edmond 19:46, card «Отменять»), so no script ends worse than before.
        Left to bukka as a question: an unblock followed by a save-and-restore, and a block during the
        watch of a number the handle blocked itself.
        All lists: pocs-dbg 1328 PASS, 14 SKIP, 47 XFAIL; pocs-asan 1303 PASS, 40 SKIP, 46 XFAIL; 0
        unexpected. S1 suites against -3: only the two new tests differ (debug 238, ASAN 237 PASS);
        `ext-scheduler-hook` 23 of 23 on debug and ASAN. The PR branch alone (debug ZTS): `poll` and
        `pcntl` tests 137 PASS, 0 FAIL. These runs were on `11b34448352`, which differs from the pushed core
        only in comments and one equivalent local in `pcntl_sigprocmask()`; on the pushed core the
        core's `poll` and `pcntl` tests (debug 142, ASAN 138 PASS) and the 33 `signal` tests pass again.
      Critic 2026-10-08: the first rule (an unblock always kept) unblocked a number the script blocked
        again during the watch, a regression (run). The take-back rule fixed it; the second Critic found
        the extension's record of a `zend_sigaction()` unblock could be taken back by a save-and-restore
        (`signal/034` with the `SIG_SETMASK` probe, red), so that record goes through
        `php_io_poll_signal_reblocked()`, which no script request takes back. The second Critic's
        scenario through `pcntl_signal_dispatch()` does not occur on this core: `acc6b34faa3`
        (`php-src-fixes`) runs the handlers under the script's mask.
- [x] S10.4 `Fiber::getCoroutine()` added to `Fiber` by the extension.
      done: `fiber/019`, `023`-`028` pass on debug and ASAN, with an own test that cancels the
        coroutine of a suspended fiber, then its parent
      tier: T1 · role: Critic
      result 2026-10-09: `src/coroutine.c` registers the method on `zend_ce_fiber` at MINIT, only with
        the scheduler registered, and removes it with its arg_info at MSHUTDOWN. Null before `start()`
        (the note's departure). Own tests `fiber/031` (the fiber's coroutine cancelled, then its parent
        while the parent waits in `resume()`), `032` (null before `start()`), `module/006`, `007` (no method
        with the extension disabled or its scheduler refused). All lists on S9.23: pocs-dbg 1441 PASS, 14
        SKIP, 11 XFAIL; pocs-asan 1416 PASS, 40 SKIP, 10 XFAIL; 0 unexpected.
      Critic 2026-10-09: the unregister leaked the method's arg_info, which `zend_function_dtor()`
        leaves to the class (ASAN, every test); freed before the delete. `module/006`, `007` added;
        `031` shows that the fiber's cancellation is dropped, not chained.
      Critic 2026-10-09 round 2 and two quality Critics: the fix sound; a failed registration now
        fails MINIT like the other registrations; wording of the comments, the notice, `031`'s title
        and the DECISIONS entry.
- [x] S10.5 The requests of the note written into `RFC-CHANGES.md`; Windows signals in the extension;
      the items left out of the first version recorded with their follow-up paths; FileSystemWatcher
      added to S9's layers.
      done: one entry per request; `signal/001` passes on Windows if built; a Fog line per item left out
      tier: T2 · role: Critic
      result 2026-10-09: RFC-CHANGES 20 (the `zend_sigaction()` hook, superseding 5), 21 (exec family
        on Windows; 10 found done on Unix by `bdfa5fa7a12`), 22 (a console read on Windows), 23 (DNS
        in ext/sockets, `php_pollfd_for_ms()` callers in ftp and pgsql). `Async\signal()` on Windows
        (`src/os_signal.c`): a `SetConsoleCtrlHandler()` handler fires a trigger of the main thread;
        Ctrl+C, Ctrl+Break and the close arrive as SIGINT, SIGBREAK, SIGHUP; only in the CLI's main
        thread. Own tests `signal/036`-`039`, `collector/079`; `signal/001`, `024`, `027`, `028` run on
        Windows again. Fog: threads, per-coroutine output buffers, PDO pool, pgsql, `SIGWINCH` on
        Windows (Edmond 2026-10-09), C4146, the minor inventory items. FileSystemWatcher is an S9
        layer with its own iterator; no overlap with the iterators layer. pocs-dbg 1441 PASS, 19 SKIP,
        11 XFAIL, 0 unexpected. Windows Debug_TS on Edmond's PC, no
        warning under `/WX`: `signal` and `collector/079` 11 PASS, 27 SKIP (Unix-only); full pocs-win 1369
        PASS, 103 SKIP, 12 XFAIL, 3 unexpected outside S10.5: `channel/101` timed out (10 100 rounds of
        `timeout(1)` at Windows' 15.6 ms timer resolution; reworked in 3eec9c1), `channel/124` needs
        pcntl (skipped since effaa20), `scope/170` failed only under the parallel run (reworked in
        c6be9aa); all three fixed on main meanwhile.
      Critic 2026-10-09: the wrong commit credited for 10; the close and teardown paths of the Windows
        watch untested (024/027/028 unskipped); the console handler's flag and trigger read without
        the lock; the Ctrl+C ignore flag and the refusal order undocumented. Round 2 and two quality
        Critics: watch open/close shared by both platforms, one `signal_watch_settle()`, the handler
        installed last in MINIT and failing it when Windows refuses; "console CLI" was wrong (the
        core's check takes `cli` and `cli-server`), the texts reworded.
- [x] S10.6 Security pass by `dev/SECURITY.md`.
      done: a journal entry per checklist item; findings fixed with a test or recorded
      tier: T2 · role: —
      result 2026-10-09: journal 2026-10-09 (S10.6). Fixed in the core, `async-core-io-2026-10-09-1`
        (`3aa1cd120f4`): a fiber whose coroutine was cancelled before its body ran left its starter
        asleep in `start()` and, after a second `start()`, a heap-use-after-free (`060bc7e104e`, Edmond
        10:53, option A; `fiber/033`-`035`); and, shown by that fix in `test_scheduler/035`, an
        out-of-memory fatal error printed twice, because the from_main call after the destructors
        made a new main (`5610980dc8f` and `src/scheduler.c`, Edmond 11:30; `scheduler/108`,
        `test_scheduler/095`). Accepted: three Windows console limits, as libuv. Left: the double print
        after a bailout in the destructors (open questions). An older leak of a printed exception
        whose `__toString()` throws went with the print's move into a coroutine. The request's last print of uncaught exceptions runs in a
        coroutine of the last pass, so async works in its `__toString()` (Edmond 13:09;
        `context/044` changed, `scheduler/109`-`112`, `reactor/044`). Core suites (test_scheduler,
        poll, stream hooks, Zend/tests/fibers): debug 360 PASS, 5 SKIP; ASAN 354 PASS, 11 SKIP; only
        the new test differs.
        Critic (last print): a bailout caught in the print let what `__toString()` spawned run on,
        and D16's deadline cancelled a print that waited, silently; both fixed with tests.

## S11 — Namespace `Async` renamed to `TrueAsync`  [ ]

Edmond 2026-10-08: the extension's PHP namespace becomes `TrueAsync` instead of `Async`. Recorded
only: no design, no start gate yet.
Where `Async\` is used (counted 2026-10-08 on `78f87d6`): the stubs `src/*.stub.php` (7 files) and
their `*_arginfo.h`; 95 class and function names in C strings under `src/`; 1216 of the 1238 tests
under `tests/`; 26 files in `dev/` and `README.md`; the docs `true-async-doc` (18 files), the site
`true-async.github.io` (2194 files), the RFC text `php-async-core-rfc` (5 files); two comments in
the core's `Zend/zend_async_API.h`. The bridge `ext-scheduler-hook` not looked at.

## S13 — The provider from the request's start, after bukka's pipe refactor  [ ]

Recorded 2026-10-10, not started. The install point is open and still to be discussed with Edmond
(S13.1).

bukka merged our overlapped-pipe change (bukka/php-src#5) into php/php-src#23997 as `6c25ec48cfb`
and refactored it in `665acf84cc1` and `4e58da87214`: the global `php_io_overlapped_pipes` is gone;
on Windows `proc_open()` makes an overlapped pipe only when `FG(io_hooks)` is set at that moment or
`PHP_IO_OVERLAPPED_PIPES=1` is in the environment; pipe creation moved to `main/io/php_io_pipe*.c`.
Edmond agreed with both changes. On bukka's head merged into `3aa1cd120f4` (2026-10-10): the Linux
core suites pass 1542 of 1542, `pocs-dbg` 1665 PASS with 0 unexpected; on Windows Debug_TS
`pocs-win` has 1567 PASS and 3 FAIL, `io_provider/029`, `032`, `035`, whose pipes are made in main
before the first spawn and stay anonymous (all three pass with `PHP_IO_OVERLAPPED_PIPES=1`).
`src/true_async.c:170-175` sets the removed global and does not build against that head.

Install in RINIT, measured 2026-10-10 (release ZTS, io_uring, a script with main only): no change
on ready data; +2 to 4.5 µs per op that waits (a pipe read 3.9 → 8.4 µs, a TCP read 9.8 → 11.7 µs);
`stream_select()` and `usleep(1)` faster. Against it: requests A and B below reach every script
instead of the scripts that spawn; on Windows a registered provider moves a script without
coroutines onto the hooks paths (`curl_exec()` through curl_multi, `stream_select()` through the
core's queue, a write cancelled under a pending exception; read from the code, not run);
`Io\Hooks\set_hooks()` always throws while the extension is enabled. Alternatives on the table:
register in RINIT and answer Unsupported until the first spawn; keep the lazy install and ask bukka
for overlapped pipes by default, or for a MINIT flag ORed into `php_io_pipe_wanted()`. Of the
~8 400 extra instructions per waiting op (cachegrind, 2026-10-10), the two fiber switches and the
scheduler's tick take about 600; the rest is the ior ring, the wait record and its notify, and six
`getpid()` calls. Edmond 2026-10-10: `sleep()` that does not return early on a handled signal is not
a defect.

Requests to bukka, one PR each, each told to Edmond first (WORKFLOW "Branches"):

- A. `O_NONBLOCK` (`main/streams/plain_wrapper.c:543-566`) only on descriptions PHP opened: never on
  `php://stdin`, `stdout`, `stderr`, `fd`, the stdio constants or a stream handed to a child; wait
  for readiness before the syscall instead, writes cut to `PIPE_BUF`; `stream_set_blocking(false)`
  unchanged. Reproduced 2026-10-10: `fgets(STDIN); passthru('cat')` under the provider, cat exits 1
  with EAGAIN.
- B. `pcntl_fork()` under `FG(io_ops_in_flight)` (`ext/pcntl/pcntl.c:278-282`) asks the provider,
  which allows it when every parked op is a readiness or a timer, never with a data op in the kernel.
- C. `php_io_pipe_stream()` returns NULL on an `_open_osfhandle()` failure, and `proc_open()` passes
  it to `php_stream_set_option()`; the handle leaks on that path.
- D. `php_select()` takes every pipe out of the wait for every script, so sockets with a pipe poll
  in 1 ms slices (`win32/select.c:95-97, 147`); back off towards 100 ms while no pipe is ready.
- E. A stream whose `stream_cast()` gives an overlapped pipe (a user wrapper) is neither released
  nor marked handed out before the child gets it.
- F. An overlapped pipe's stream mode reads `r` where its descriptor is binary; EBADF read as 0
  differs from an anonymous pipe under `blocking_pipes`; no provider can take a `stream_select()`
  member that is not a socket.

For php-src (`php-src-fixes`): a socket in `proc_open()`'s descriptorspec on Windows goes through
`_get_osfhandle()` of its `SOCKET` and the child gets a wrong handle without a warning (the child
read `''` on 2026-10-10). `dev/plans/S6.md` section 2 says TrueAsync's streams go async only after
its scheduler launches; its plain streams get a libuv handle from RINIT (the fork's
`main/streams/plain_wrapper.c:235-293`).

- [ ] S13.1 The provider's install point agreed with Edmond: RINIT, RINIT with Unsupported until the
      first spawn, or the lazy install with a request to bukka for the pipes.
      done: the decision and its reason in `DECISIONS.md`; the tests it changes named
      tier: T2 · role: Critic
- [ ] S13.2 The core moves to bukka's head with our extension building on Windows: the global
      dropped from `src/true_async.c`, S13.1's decision built, `io_provider/029`, `032`, `035` passing
      without `PHP_IO_OVERLAPPED_PIPES`.
      done: `pocs-dbg`, `pocs-asan` and `pocs-win` rediffed against the current core per WORKFLOW
        "Core update"; `CORE_REF` and `IOR_REF` moved
      tier: T2 · role: Critic → Sage
- [ ] S13.3 Requests A-F as PRs to bukka and the socket fix on `php-src-fixes`, each told to Edmond
      first.
      done: each PR's link and state in `RFC-CHANGES.md`; `S6.md` section 2 corrected
      tier: T1 · role: Critic

## S14 — Health check fixes  [ ]

The open findings of the health check of 2026-10-12 (`dev/HEALTH.md`). Edmond 2026-10-12 12:54: all of them are
fixed at once, ahead of S8, S9.31 and later stages. S14.1 is a crash and a leak on main. S14.1's fiber part and
S14.6 are core changes and take the core slot.
Tier: T2. Roles: Critic, Sage.
Tests: interleaved

- [x] S14.1 Crash and leak under a random scheduling seed: `channel/145` (SIGSEGV) and `fiber/035` (the body
      runs after the `exit()`, two leaks): cause found and fixed, with a test that fails without the fix in
      plain FIFO order where one can be built.
      done: `tools/test.py --seeds 100` over `channel/` and `fiber/` with no failed seed on `pocs-dbg` and
        `pocs-asan`; the S9 and S10 lists pass as before
      tier: T2 · role: Critic → Sage
      handoff: done 2026-10-12 on core `c53c17cfd47`. `channel/145`: the GC freed a dropped `recvAsync()->map()`
        chain the channel could still complete; a channel, group or signal wait now holds a chained Future and
        the channel and the group report it (Edmond 14:31; DECISIONS 2026-10-12 S14.1); FIFO reproducer
        `channel/148`, tests `149`, `task_group/165`, `signal/044`, `045`. `fiber/035`: fixed by S8's core
        (`8c0d98dd7e0`). 100 seeds over `channel/` and `fiber/` (192 tests): 0 failed on pocs-dbg and pocs-asan;
        full pocs-dbg 1753 PASS, pocs-asan 1727 PASS, 0 unexpected. Two Critics; Sage not needed. The GC run a
        release can start inside `channel_put()` is S9.33's (SECURITY.md).
- [x] S14.2 Nightly CI with random seeds: the run completes, means something and is watched: a seed run that
      cannot parse an output reports it and goes on, and the test that writes the stray `  thrown` is found;
      `io/100`'s verdict no longer follows the queue order; `stream/030` fails fast instead of waiting for
      run-tests' timeout; `--show-slow` in `tools/test.py`; an `--io-chaos` entry in the `seeds` matrix;
      WORKFLOW names who looks at the scheduled run.
      done: one scheduled run green on dbg and asan with all of its seeds (since S14.7 a dispatched run: no
        nightly run, Edmond 2026-10-12 16:22); the suite's ten slowest tests in
        HEALTH's `Suite:` line
      tier: T1 · role: Critic
      handoff: 2026-10-12, the fixes are in; open until a scheduled run is green, which waits for S14.1
        (`channel/145`, `fiber/035` are the only failed seeds: 8 dbg, 3 dbg `--io-chaos`, 2 asan, locally). The
        stray text came from `scheduler/084`, `085`; a lost result line is reported and MISSING; run-tests
        retries no crash or hang and nothing under a seed; a crashing XFAIL fails; `stream/030` ends in 2 s
        (DECISIONS 2026-10-12 S14.2). The dbg suite runs 38.5 s locally, was 150 s. Close: after S14.1, the
        next nightly run with every `seeds` job green (a dispatched run has the same jobs and counts).
        Ticked 2026-10-12 (fd1f9c4, f340508): `channel/145` and `fiber/035`, the only failed seeds, are fixed by
        S14.1 (cd89af6, core `c53c17cfd47`). The seeds run came after: run 38282314790, dispatched 2026-10-12
        on 89c944d (Edmond 17:07), all 15 `seeds` jobs green (the push `linux (asan)` job's red is outside
        the seeds, S14.7).
- [x] S14.3 Tests without wall-clock waits: the own tests HEALTH lists under "wall-clock delays" and "wall-clock
      bound" wait on the event they test instead of on time; `collector/063`, `064` check the walk's outcome at
      each margin; the cases `fiber/023`, `028` and `scope/043` leave unchecked get own tests. Ported tests
      change only with a `changed:` tag and a DECISIONS entry (P2.2).
      done: each listed test passes under `--jobs 16` on 4 cores and over 20 seeds; none compares elapsed time
        except where the deadline itself is the behaviour
      tier: T1 · role: Critic
      handoff: done 2026-10-12 (DECISIONS 2026-10-12 S14.3), except `scope/124` and `125`, which keep
        `max_execution_time` until Edmond's call (Open questions). 50 tests changed: events, Futures or turns
        instead of delays; timer pairs whose later timer is longer; a line after a wait that must not end;
        armed timers from `reactor_state()`; new test-build hooks `Test\callback_searches()` (`await/141`,
        `signal/024`) and `Test\collector_idle_runs()` (`collector/063`, `064`, whose margin is measured on
        the build's heap). Eight kept with reasons. New: `fiber/048`, `scope/188`, `189` (`tests/lists/S14.txt`).
        Seeds found tests that hung or hit the limit when a `suspend()` left a new coroutine unstarted, now
        fixed. `collector/069`-`073` (S14.2's Sage) wait for the collector's run or a flag; the full lanes on
        core `c53c17cfd47` found `io/046`, `await/119`, `scope/084`, `collector/042`, `043`, `io_provider/013`,
        `scope/070`. The wider class (a top-level delay as sync point, about 50 tests) is S14.9's.
        Critic twice (14 findings), Sage on two.
- [x] S14.4 Tests and plan rows with no owner: `gc/020`, `023` (XFAIL "S8 candidate") get an S8 step or a
      DECISIONS outcome; `S3.excluded` lines 4, 7 (`component:S6`) and 13-18 (`component:S9`) get a stage that
      ports them or an exclusion reason that holds; `dev/plans/S2.md` section 4's scenarios, allocator-fault and
      network-chaos rows get a step or a recorded drop; PLAN's layout drops `src/chaos.c` and `fuzz/`.
      done: `check-lists.py` passes; no list line or XFAIL names a closed stage
      tier: T1 · role: Critic
      handoff: done 2026-10-12: `gc/020`, `023` and the reference's `gc/018` are `changed:` ports that expect
        the awaited collection (DECISIONS 2026-10-02, 2026-10-12); `include/008` passes as written; both new
        ports in S6.txt; the six `common` tests name a new S9 layer, runtime information; S2's scenarios,
        allocator faults and network chaos are S15.1-S15.3, its TSAN row waits for threads, `tests/deps/`
        is recorded as never created; the layout lists what `src/` holds. `check-lists.py`: 1741 listed, 0
        violations; the four tests pass on `pocs-dbg` and `pocs-asan` and over 20 seeds. Critic and Sage:
        a permanent XFAIL passes a crash or a leak in its test; the runner's fix goes to S14.2.
- [x] S14.5 Fault tests of the extension: fault seams for the reactor's queue (creation, submit, wait, the wake
      pair) and tests through them and through `fiber.stack_size` for the paths HEALTH lists under pass 6; fault
      tests for the two incidents of `dev/SECURITY.md` (the drain with no owner, the trigger that starves
      timers).
      done: each listed path reached by a test on `pocs-dbg` and `pocs-asan`; coverage of those lines recorded
      tier: T2 · role: Critic → Sage
      handoff: done 2026-10-12. `TrueAsync\Test\fail_io()` (DECISIONS 2026-10-12 S14.5), 31 tests in
        `tests/lists/S14.txt`, PASS on pocs-dbg, pocs-asan and 20 seeds. Coverage (pocs-dbg-cov, the 31 tests
        alone): 138 of 156 executable lines of HEALTH's ranges; the misses are the drain with an owner and the
        queue's EINTR and EDEADLK answers (other tests), the wakeup's inline resubmit, "Cannot poll the wake
        descriptors" and a forked child's signal context or handle that cannot be made (not reached, DECISIONS),
        and an exit() thrown by a release inside the refused drain's rejection. Bugs fixed: a frameless throw on
        D16's arm (main's exit, a forked child's rebuild), the refused drain's pending children, the refused
        TaskGroup reporter's lost errors, a failed wait that spun the idle loop. Left for Edmond: the rebuild's
        PHP calls on D16's frameless arm (open questions). Full pocs-dbg over S9.31: 1725 PASS.
- [x] S14.6 Fault tests of the core: the core's test scheduler can fail an enqueue or a cancel at any status and
      refuse a finish handler; tests for the GC coroutine's and the Fiber's paths HEALTH lists, and for "The
      scheduler failed to launch".
      done: the new core tests pass on dbg and ASAN; `CORE_REF` moved by WORKFLOW "Core update"
      tier: T2 · role: Critic → Sage
      handoff: done 2026-10-12. Core `async-core-io-2026-10-12-2` `96a52ba65f4` (`async-core` `6bfa8ed44de`,
        seven commits, RFC-CHANGES 33). The test scheduler fails a wake, a cancel, a finish handler, a defer and
        the launch; `ext/test_scheduler/tests/096`-`118`, 26 tests use the seams; PASS on dbg and ASAN, each
        commit on dbg. Bugs fixed: `Fiber::suspend()` parked before waking its caller, `start()` stored its
        handler unchecked, a refused wake released a body's error, a refused finish handler freed a fiber
        under its body, the GC's refused hand-off leaked its microtask, refusal errors surfaced in unrelated
        code; three Critics found that the GC's automatic run during an unwind lost the throw position or an
        `exit()`, walked freed temporaries and, cancelled, dropped live ones from the root buffer (DECISIONS
        2026-10-12 S14.6). Extension on the new core: pocs-dbg 1760 PASS, pocs-asan 1734 PASS, 0 unexpected.
- [x] S14.7 Every CI job ends within 30 minutes: each line of the `seeds` job (dbg 100 seeds, asan
      20, dbg `--io-chaos` 50) runs its seeds in five parallel jobs (`tools/test.py --shard K/M`, a seed keeps
      its number); every job has `timeout-minutes` (30, `lists` 10); no nightly run, the seeds run when CI is
      dispatched by hand. Edmond 2026-10-12 16:05: CI tests do not run long; 16:22: no nightly checks for now.
      done: a dispatched run in which no job or step hits its time limit; each line's longest shard recorded
      tier: T1 · role: Critic
      handoff: 2026-10-12, the change is in. One job ran a whole line, 51-65 min (run 38266038345), 2 h 52 min
        for asan at 9bbd906 (run 38230954460): S3.12 picked 100 and 20 seeds for 259 tests, the suite has
        ~1 780. Close: a dispatched run in which no job or step hits its time limit.
        Done 2026-10-12: run 38282314790 on 89c944d (dispatched with Edmond's OK, 17:07) took 21 min from
        dispatch to its last job; the longest shard of each line, its `Seeds` step: asan 14 min 32 s, dbg
        13 min 20 s, dbg `--io-chaos` 6 min 35 s; no job or step hit its limit, all 15 `seeds` jobs green.
        The run's one red is outside the seeds: the push `linux (asan)` job (https://github.com/true-async/true-async/actions/runs/38282314790/job/114922292796),
        `scope/074`, a top-level `delay()` used to wait for queued work; it joins HEALTH's finding on that class.
- [x] S14.8 No performance tests in CI: `scope/124` and `125`, the last tests whose verdict is a run time under
      `max_execution_time` (S14.3 made `await/141` and `signal/024` count searches), keep only the mechanism at a
      small size; their "linear time" claim becomes a benchmark of `bench/`, checked by hand with
      `tools/bench.py --count`. Settles the open question on them: Edmond 2026-10-12 16:05, no performance tests
      in CI.
      done: no listed test has a time bound as its verdict; the two claims are benchmarks of `bench/`
      tier: T1 · role: Critic
      handoff: done 2026-10-12. `scope/124` (200 nested scopes) and `scope/125` (2 000 child scopes) keep their
        checks without `max_execution_time` (`changed:2026-10-12`); `B19`, `B20` (`dev/BENCHMARKS.md`): 7,782
        and 6,271 instructions per operation on debug, flat as N doubles. The open question on them is settled.
- [x] S14.9 Tests that use `delay()` as a sync point wait for the event instead: the own tests whose top-level
      `delay()` waits for work of other coroutines (S14.3's survey, project file `s14/delay-sync-survey.md`, and
      HEALTH's finding, with `scope/074` that failed the asan push job of CI run 38282314790) loop on a flag, a
      count, `isCompleted()` or the collector's idle run, or await the coroutine; a test whose case is the timer
      keeps its delay.
      done: 25 ASAN runs of the changed tests under `--jobs 16` with four busy loops beside, 10 more after the
        review's fixes, and 20 seeds on `pocs-dbg` and `pocs-asan`, with no failure (the seeds' one find, a hang
        of `scope/083`, is fixed); the full lanes pass with 0 unexpected
      tier: T1 · role: Critic
      handoff: done 2026-10-12, 87 tests (DECISIONS 2026-10-12 S14.9 lists them, the idioms and the tests kept).
        A loop on an event turns a regression into run-tests' timeout instead of a wrong line.
- [x] S14.10 S9.42's `iterate/061`, `062` and `063` wait for the spawned child's park instead of `delay(1)`
      (`iterate/061` failed once in S8.12's full `pocs-dbg` run).
      done: with gdb stopping the process 150 ms before each timer poll of a park, the old tests fail on every
        run and the new ones pass; `tests/iterate` 64 PASS on `pocs-dbg`, the three clean on 100 seeds
      tier: T1 · role: Critic
      handoff: done 2026-10-13 (DECISIONS 2026-10-13 S14.10).
- [x] S14.11 `reactor/009` and `010` wait for the spawned waiter's park instead of `reactor_wait(10)`, `signal/020`
      and `025` for the sleeper's instead of `delay(1)` (`reactor/009` failed once in S8.13's full local run).
      done: with gdb stopping the process 50 ms before each timer poll, old 009 and 010 fail on every run, old 020
        forks before the sleeper parks; the new ones pass; `tests/reactor` and `tests/signal` 93 PASS, 4 SKIP on
        `pocs-dbg`, the four clean on 100 seeds
      tier: T1 · role: Critic
      handoff: done 2026-10-13 (DECISIONS 2026-10-13 S14.11).
- [x] S14.12 The seed jobs move to `.github/workflows/seeds.yml`, dispatched by hand, which reads `CORE_REF` and
      `IOR_REF` from `ci.yml` (Edmond 2026-10-13 07:48: a push run showed the skipped job's unexpanded name).
      done: a push run has no seeds job; both files parse; the pin step tried on `ci.yml`
      tier: T1 · role: —
      handoff: done 2026-10-13 (DECISIONS 2026-10-13 S14.12).
- [x] S14.13 Own tests whose output depends on the clock wait for the event (Edmond 2026-10-13: check the tests
      again; a stop of the process at any point must not change a result).
      done: a local harness stopping the process 120 ms before a park's timer poll; the 25 changed tests pass with
        the first 3 and 15 polls stopped, with 500 ms stops, with none, and on 20 seeds on `pocs-dbg`
      tier: T1 · role: Critic
      handoff: done 2026-10-13 (DECISIONS 2026-10-13 S14.13). Next: the stall as a test hook (Edmond 08:39 yes), a
        test clock for `await/121`, `126`, `reactor/025`, `scope/153`, `io_provider/002` (asked), the channel bug of
        `channel/126`, TrueAsync's 15 tests, 60 tests that hang only when every poll stops, tests that cannot fail
        (project files `s14/audit-2026-10-13/`).
- [x] S14.14 The stall of S14.13's harness as a test hook: `true_async.test_stall_ms`, `test_stall_polls` and
      `test_stall_every`, run by hand through `TEST_PHP_ARGS` (dev/WORKFLOW.md).
      done: `internal/074` passes with the hook and prints the other order without it; old `scope/079` fails
        with `polls=3`, the new one passes; `tests/module` passes with and without the stall
      tier: T1 · role: Critic
      handoff: done 2026-10-13 (DECISIONS 2026-10-13 S14.14).
- [x] S14.15 TrueAsync's 15 tests of S14.13, `internal/069` and `socket_ext/005` pass the stall runs of
      dev/WORKFLOW.md.
      done: the 17 pass the three stall runs, a 500 ms stall on the first 5 polls, no stall and 20 seeds on core -8
      tier: T1 · role: Critic
      handoff: done 2026-10-13 (DECISIONS 2026-10-13 S14.15).
- [ ] S14.16 The rest of the 2026-10-13 test audit (project files `s14/audit-2026-10-13/`): `reactor/017` and
      `scope/156` pass the stall runs; `channel/126` with the channel bug it shows; tests that cannot fail and tests with several cases; the five that need a test clock wait for
      Edmond's answer.
      tier: T1 · role: Critic
- [x] S14.17 The test servers (`php -n -S`) start with `opcache.enable=0`: on Windows two starting at once could
      crash in opcache's startup (CI run 38366407824, `cleanup/001`). The four curl tests on the core's
      `server.inc` keep the race: its helper takes no arguments.
      done: a dump of the crash (Release_TS, core f9c2ef86) shows `accel_post_startup()` reading a NULL
        `accel_shared_globals`; the 69 tests that start a server pass
      tier: T1 · role: Critic
      handoff: done 2026-10-13 (DECISIONS 2026-10-13 S14.17).
- [x] S14.18 The 61 tests that fail only when every tick stalls: two depended on the clock and hold no timer now.
      done: `iterate/016` and `pool/024` pass and fail on the bugs put back by hand; `collector/018` shows why the
        rest never end under such a stall
      tier: T1 · role: Critic
      handoff: done 2026-10-13 (DECISIONS 2026-10-13 S14.18).
- [x] S14.19 26 TrueAsync tests that could not fail check the effect their title names.
      done: each passes, fails on its regression put back by hand where that is one line, and passes the stall runs
        and 20 seeds
      tier: T1 · role: Critic
      handoff: done 2026-10-13 (DECISIONS 2026-10-13 S14.19).

## S15 — Test layers of S2 not built yet  [ ]

Recorded 2026-10-12 by S14.4, not started; no design yet. `dev/plans/S2.md` section 4 planned three
layers that no step built: scenarios (TrueAsync's `fuzzy-tests/`, 104 `.feature` files at
`tests/lists/REFERENCE`, which its CI regenerates and runs), allocator faults
(`zend_mm_set_custom_handlers`, a test-only hook) and network chaos (`tc netem` or Toxiproxy between
the extension and its MySQL and HTTP fixtures). The Scope, Context and Channel plans of S9 left their
fuzzy features unported for want of the first.
Tier: T2. Roles: Critic.
Tests: interleaved

- [ ] S15.1 Scenarios: TrueAsync's `fuzzy-tests/` ported with its generator, `_generated/` gitignored
      and regenerated before the run, as its CI does; features of groups this extension does not build
      (threads, pools, remote futures) excluded with a reason.
      done: every `.feature` of a built group listed or excluded; the generated tests pass on
        `pocs-dbg` and `pocs-asan` and over 20 seeds
      tier: T2 · role: Critic
- [ ] S15.2 Allocator faults: a test-only hook ends the request at the n-th allocation as an exhausted
      `memory_limit` does, first checked to be fit for this (S2 section 4 left it unverified); the
      lists run under it with n from a seed.
      done: the hook's known answer; no crash, assertion, sanitizer report or leak over 20 seeds on
        `pocs-dbg` and `pocs-asan`
      tier: T2 · role: Critic
- [ ] S15.3 Network chaos: delays, drops and resets between the extension and its MySQL and HTTP
      fixtures, in the nightly run, with attempts, successes and failures counted.
      done: one scheduled run green with the counts recorded
      tier: T1 · role: Critic
