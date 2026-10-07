# Security

What True Async must not let happen, how every stage is checked for it, and the decisions taken.
Newest journal entry last.

## Threat model

True Async runs inside the PHP process, in every request, under user code. The defect classes:

1. **Memory safety.** Use after free, double free, overflow in the extension's C code; the risky
   places are coroutine lifetimes across a switch, a bailout and a request end. Consequence: a crash
   or code execution in the PHP process.
2. **State leaking between coroutines.** Engine state that belongs to one coroutine and is seen by
   another: `EG(error_handling)`, the current exception, output buffers, the current coroutine.
   Precedent: in TrueAsync the EH_THROW window was process-wide, and a warning raised in one
   coroutine surfaced as a `PDOException` in another.
3. **Denial of service.** Unbounded queues and handler vectors, spawn storms, the cost of deadlock
   detection, stack memory per coroutine, a scheduler loop that spins with nothing to run.
4. **Cancellation bypass.** A cancellation that skips `finally` handlers or leaks out of `protect()`
   leaves locks held and resources open.
5. **Supply chain and CI.** Secrets written into artifacts, downloads not pinned, a workflow token
   with write rights.

Out of scope: defects in php-src and ior themselves. They are reported to their owners: php-src
under its security policy (the core's `SECURITY.md`), bukka's projects through his repositories.

## The security pass

Every T2 stage closes with a security pass after Code Reviewer (`WORKFLOW.md`). A subagent gets
this file and the stage diff and checks:

- the lifetime of every object across a switch, a bailout and a request end (a green ASAN lane is
  necessary, not sufficient: it sees only the paths the tests take);
- every refcount pair on the paths an exception or a bailout takes;
- every piece of engine state saved and restored per coroutine;
- every size or count that user code controls has a bound or a documented reason to have none;
- every new INI entry: who can set it (`PHP_INI_SYSTEM` or `PHP_INI_ALL`) and what it enables;
- test-only code (known-answer functions, fuzz hooks) compiled out of a default build;
- CI: new downloads pinned by revision or hash, no secrets, read-only permissions.

The main model answers each finding: fixed with a test, or recorded below with the reason. A
finding left open gets an owner step in `PLAN.md`.

## Journal

- 2026-10-01 The runner removes secret-looking variables from the tests' environment. Why: run-tests
  writes the whole environment into a `.sh` next to every failed test; 45 such files holding a
  GitHub token were found in the local core trees and deleted.
- 2026-10-01 The CI workflow token is read-only (`permissions: contents: read`). Why: no job
  writes to the repository.
- 2026-10-01 CI downloads are pinned: the core and ior by full commit, Mull's package by sha256
  (equal to the release's published digest). Why: a moved tag or a replaced asset would change
  what CI builds and runs.
- 2026-10-01 The planted known-answer functions are built only with
  `--enable-true-async-known-answer`. Why: they are PHP-callable functions with no purpose in a
  default build.
- 2026-10-01 `true_async.enable` is `PHP_INI_SYSTEM`. Why: user code must not switch the
  process-wide scheduler on or off at run time.
- 2026-10-05 The runner passes the MySQL fixture's `MYSQL_TEST_*` variables, the password
  included, to the tests (S6.1). Why: the tests connect with them; the password is the throwaway
  `root` of a server that lives for one run: CI's service, or the local private mysqld, which
  listens on 127.0.0.1 only and is removed with its data after the run.

- 2026-10-03 Security pass of stage S3 (S3.14) over `128c48a..8d792fa`, by checklist item.
  Lifetimes: two use-after-free fixed with tests, `$this` of a class-string callable given to
  `spawn()` (`spawn/021`) and main freed while queued after a bailout that a shutdown function's
  zend_try caught (`internal/048`); the stale current coroutine after such a bailout is open below.
  Refcounts on exception and bailout paths: clean (waker errors, cancellation, the exit exception's
  moves, finalize's own references). Engine state: the EH_THROW window of a suspending coroutine
  covered what the suspend runs on its stack (switch handlers, tick, pop), so another coroutine's
  warning became its exception class; the suspend now leaves the window behind as the core's switch
  does (`scheduler/077`); an `@` it suspends inside silenced that code the same way, and the suspend
  gives it the INI value (`scheduler/078`); VM state, `EG(active_fiber)` and `EG(exception)` clean.
  `await()` and `getResult()` handed a body's by-reference result out as a reference (`await/096`).
  Sizes user code controls: under `memory_limit`, except the stacks and the exit chain below. INI
  entries: unchanged; `debug_deadlock` is open below. Test-only code: compiled out on Linux, and on
  Windows after the snapshot fix below. CI: the entries below. The scheduler coroutine's stack also
  gets the room of its first VM page, as every context's does.
- 2026-10-03 run-tests gets only the variables `tools/test.py` lists (`TEST_ENV_NAMES`, the
  `TEST_PHP` and `SKIP_` prefixes), which closes the open finding of 2026-10-01. Why: run-tests
  writes its whole environment into the `.sh` of every failed test and CI uploads `results/`; the
  old filter matched names, and `DATABASE_URL`, `AWS_ACCESS_KEY_ID` and proxy URLs passed it. Known
  answer: a planted `DATABASE_URL` reached the `.sh` with the old filter (124 variables) and not
  with the list (18).
- 2026-10-03 `--enable-true-async-test-hooks` defaults to `""` on Windows, and only `yes` builds the
  hooks. Why: php-src's snapshot build (`--enable-snapshot-build`, `win32/build/confutils.js`)
  turns on every switch whose default is `no`, which put `TrueAsync\Test\*` into a snapshot
  build. A build that wants them passes `--enable-true-async-test-hooks=yes`: the bare switch
  reads as `no` with this default.
- 2026-10-03 Every `actions/checkout` sets `persist-credentials: false`, and the Windows lane builds
  ior at `IOR_REF`. Why: no step uses the job token after the checkout; the ior action's default
  commit would have stayed behind an `IOR_REF` bump.
- 2026-10-03 GitHub's own actions stay pinned by major tag, not by commit. Why: they are
  first-party, the token is read-only and no job has a secret.
- 2026-10-03 The Windows lane takes php-sdk-binary-tools by the tag `php-sdk-2.8.4` and the
  dependency archives from windows.php.net without a hash, as php-src's own Windows CI does; the
  2026-10-01 entry "CI downloads are pinned" holds for the Linux lanes only. Why: `build.bat` clones
  the SDK with `--branch`, which takes no commit; pinning needs our own copy of php-src's build
  scripts.
- 2026-10-03 Coroutine stacks are mapped memory outside `memory_limit`, as a Fiber's are in the
  core and in TrueAsync; `fiber.stack_size` and the OS limits bound them. Output buffers and
  `BG()` state are shared between coroutines, as between Fibers.
- 2026-10-03 The exit exception chains every unobserved exception through
  `zend_exception_set_previous`, which walks the chain: N failing coroutines cost O(N²) (8000 took
  7.4 s on the debug build). Kept as TrueAsync (`scheduler.c:1027`): user code pays for its own
  failures, and `max_execution_time` bounds it.
- 2026-10-03 Accepted without a fix, unreachable today: the callbacks vector's capacity reaches its
  `ASYNC_CALLBACKS_F_NOTIFYING` bit at 2^31 entries; `circular_buffer_dtor` leaves `head`, `tail` and
  `capacity`, and nothing pops after it; no CI lane builds the default configuration, and the
  guards of the test-only code were checked by reading.
- 2026-10-03 The three open findings of S3.14 closed on Edmond's word (S3.18's core items done
  ahead, the core not pinned yet). Core `async-core` `2cb30e538e4`: `Fiber::__construct` and
  `ZEND_ASYNC_FCALL_DEFINE` hold the object a class-string callable resolved to (upstream `Fiber` too:
  reproduced on PHP 8.3.6 and master; the upstream report waits for Edmond); `ZEND_ASYNC_API_VERSION`
  20261003, and `coroutine_object_gc` reports that object from that version (`gc/023`). Core
  `c43060ea12d`: `php_call_shutdown_functions` ends the scheduler as a bailout when a shutdown
  function bails out, `exit()` included, so no finished coroutine stays current. The deadlock report
  shows only where its error does (`scheduler/083`-`085`).

- 2026-10-07 Security pass of stage S5 (S5.6) over the S5 commits `756e6c3`, `266f092`, `1be2667`,
  `4268e50` (`src/future.c`, `await.c`, `timeout.c` and their parts of `true_async.c`,
  `scheduler.c`), by checklist item, with scripts run on the debug and ASAN builds. Lifetimes: no
  use after free found (a NULL-state Future from `unserialize()`, clone and reflection refused,
  exit() and fatal errors in mappers, iterators and items, GC mid-wait, fork with an armed timer, a
  Timeout cancelled with 2 000 waiters parked); one leak fixed: a Traversable whose items hold the
  coroutine walking it, directly, as a result or as a Future's value, held that coroutine and the
  wait's context in a cycle the GC cannot see; the iterator coroutine lets go of the context when
  its walk ends (`await/140`). Refcounts on exception and bailout paths: balanced; the
  unobserved-exception report leaked the message a property hook builds (`future/122`). Engine
  state: the drain saves and chains what a release throws, and an exit is not chained. Sizes:
  chains, the drain's FIFO, the vectors and the result tables are bounded by `memory_limit`;
  completion and the drain iterate (a `map()` chain of 10^6 links completes); one Future repeated N
  times in an `await_*` array cost O(N^2) to unlink (200 000 copies 8 s), and two waits over the
  same copies 31 s; N `Async\signal()` Futures on one signal, dropped, 200 000 in 8.3 s; a callback
  keeps its index in its vector, so its removal searches nothing (`await/141`, `signal/024`,
  `internal/063`); the per-wait table of reservations hashed aligned addresses into few buckets
  (10^6 items linked in 1.9 s before the fix, 0.3 s after). `$count` of `await_any_of*` is clamped
  to `UINT32_MAX` and a count of 0 or less waits for all. INI entries: none new. Test-only code:
  `add_throwing_subscriber` and its call sit under `TRUE_ASYNC_TEST_HOOKS`. CI: no change.
- 2026-10-07 Accepted (S5.6): releasing a long object graph recurses in the engine, so a chain of
  Futures held through re-constructed children or mapper closures overflows the C stack at about
  30 000 links, the depth at which a plain linked list of `stdClass` does on the same build. A drain
  coroutine cancelled by a graceful shutdown leaves its queued children uncompleted, so a `finally`
  that awaits one ends in the deadlock report, as TrueAsync's iterator coroutine does. The counters
  of an `await_*` wait are 32-bit: a Traversable that yields 2^32 items wraps them, which takes
  hours and no memory. A message property hook runs user code while a Future is freed, as the
  core's own uncaught-exception report does.
- 2026-10-07 A php-src leak (S5.6): `zend_exception_set_previous()` does not release an exception
  already in the chain, so `throw new LogicException("y", 0, $e)` in a `finally` that `$e` left
  leaks it in plain PHP; `Future::finally()` reaches it the same way. Sent to the coordinator for
  `php-src-fixes` (WORKFLOW, the php-src bug rule); no workaround here.
- 2026-10-07 Security pass of stage S7 (S7.6) over the S7 commits `cb6d213`, `96449e3`, `f483f57`,
  `be20b82` (`src/collector.c` and its parts of `scheduler.c`, `future.c`, `await.c`,
  `true_async.c`, `test_hooks.c`), by checklist item, with about 25 scripts run on the debug and
  ASAN builds. Lifetimes: no use after free found (a warning handler that cancels, drops, collects,
  throws, exits, dies with a fatal error, blocks or calls the registry; the walk inside a Fiber, a
  generator, a destructor during GC and at request end; fork between runs; holders from SPL, DOM,
  PDO, curl, SQLite3 and others; stacks parked in `eval`, `include`, `extract` and `$$var` tables).
  The handler cannot reach a found coroutine, and `cancel` only enqueues. Refcounts: the references
  taken around the report and the cancel are released on every return path and die with the request
  on a bailout. Engine state: `error_reporting` and the exception are restored around the warning.
  Sizes: the walk iterates (10^6-deep arrays found in 0.9 s); the memory ceiling of the automatic
  run was checked only when the node table doubled during the count, so coroutines awaiting the
  same dead Futures, whose wake edges outnumber the nodes, ended the request with a fatal error at
  `memory_limit` (`collector/063`), and so did 16 500 parked coroutines, whose registration as
  candidates doubled the node table unchecked (`collector/064`, the Critic): both check it now, and
  a table that would pass 2^31 entries stops with a fatal error instead of wrapping its position.
  INI: both entries are `PHP_INI_ALL` and per request; the interval is now 5000 ms by default and at
  least 1000 ms, or a literal 0: an empty value, which php.ini makes of a bare `off`, parsed as 0
  and walked at every idle point (`collector/062`).
  Test-only code: the hooks and the oracle's flags sit under `TRUE_ASYNC_TEST_HOOKS`; every S7 file
  compiles without it. CI: no change but the fuzz seeds' interval 0.
- 2026-10-07 Accepted (S7.6): a walk costs time and memory in proportion to the stuck graph (a stuck
  pair holding 10^6 objects: 164 ms and 76 bytes a node per run on the debug build), and under
  `report` the stuck coroutines stay, so each first warning resets the back-off and the next run
  walks them all again. The interval and `memory_limit` bound the rate and the size; interval 0 and
  `memory_limit=-1` are the script's own choice. The engine's `get_gc` buffer and the frames' buffer
  (a parked frame's arguments and variables) grow outside the ceiling (S7.md 3.5). The test-only `replace_execute_ex()` restores the executor only at request
  end, so a run after an extension put `execute_ex` back mid-request was not tried; the extensions
  that replace it do so at module startup.

- 2026-10-07 Security pass of stage S4 (S4.7) over the S4 commits `81dfeeb`, `b46918a`, `d2ff382`,
  `e04f515`, `bbe516b` (`src/reactor.c`, the wait-record layer in `true_async_API.c`, the idle wait,
  deadlock and D16 in `scheduler.c`), by checklist item, with scripts run on the debug and ASAN
  builds. Lifetimes: one defect fixed: a coroutine woken in its own suspend's tick (U2) while D16
  fired in the same poll took D16's graceful exit as its outcome and ran on, so `getException()`
  and `await()` handed out the core's internal exit object, and `serialize()` of it crashed the
  process in a default build; the exit is now thrown by that `suspend()` (`reactor/043`). Records
  live in the waker, every bailout exit aborts and unlinks them; 20 000 delays with random cancels,
  then `exit()`, a fatal error or `pcntl_fork()`, ran clean on ASAN. Refcounts on exception and
  bailout paths: balanced (a failed submit, a cancelled delay, a notify that throws or bails out).
  Engine state: the idle wait and its notifies run in scheduler context with the S3 save and
  restore; `EINTR` returns only with an interrupt pending. Sizes: the timer heap is O(log N) a
  timer (30 000 cancelled delays 0.5 s), deadlock resolution linear (20 000 parked 0.6 s);
  `delay()` and `timeout()` took the core's `php_io_deadline_from_ms()`, whose `timeval` seconds
  are 32-bit on Windows: past 2^31 s a debug build aborted on an assertion and a release one woke
  early (a Windows Debug_TS build failed `reactor/022` and `await/122` on that assertion); both now
  count nanoseconds (`reactor/042`). A thread that fires a trigger without pause
  ended the poll's loop only when it lost a race (a test thread firing for 2 s: `delay(30)` took
  30.1 ms, its waiter woke 1 134 times); the loop now ends after the wakeup's completion, one pass
  as libuv's.
  Descriptors: the eventfd is close-on-exec; off Linux the pipe takes `pipe2()` where the core
  found it, which closes the window in which another thread's `proc_open()` inherited the pair; on
  Windows the socket pair is made not inheritable. INI entries: none new. Test-only code: the
  trigger hooks and their thread sit under `TRUE_ASYNC_TEST_HOOKS`, absent from a default build
  (checked). CI: `--with-zlib` in the core build, no new download.
- 2026-10-07 Accepted (S4.7): a coroutine whose `finally` spawns the next one that waits keeps D16
  refiring every 100 ms, so such a chain never lets the graceful shutdown end; only
  `max_execution_time` on a ZTS build (wall time) bounds it, as it bounds a shutdown function that
  loops on `sleep()`, and TrueAsync's `finally_shutdown` has no bound either. A trigger is request
  memory with a plain refcount: a future holder fired by other threads (S10's remote Future) must
  stop and join them before its last release and before RSHUTDOWN. The timer heap's capacity
  doubles in 32 bits and wraps at 2^31 timers, unreachable under `memory_limit` like the callbacks
  vector's bound. macOS has no `pipe2()`, so its pipe keeps the window between `pipe()` and
  `fcntl()`.
- 2026-10-07 A php-src defect (S4.7): `socketpair_win32()` (`win32/sockets.c:25-90`) binds its
  listener to `INADDR_ANY` and accepts the first connection without checking the peer, and makes
  its sockets inheritable until the caller changes them; the reactor's Windows wake pair and
  php-src's own `stream_socket_pair()` use it. Sent to Edmond for php-src's security policy; no
  workaround here.

## Open findings

None.

