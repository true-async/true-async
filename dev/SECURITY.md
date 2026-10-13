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

- 2026-10-07 Security pass of stage S6 (S6.9) over the S6 commits `8e94038`, `409aa2e`, `7b5a2aa`,
  `f977fd6`, `06a5af4`, `b3ec701`, `7e4ba19`, `134e133` (`src/io_provider.c`, `src/os_signal.c`, the
  IO event parts of `src/reactor.c`, `tools/test.py`, the CI workflow), by checklist item, with
  scripts run on the debug and ASAN builds. Lifetimes: no use after free found (a stream closed by
  another coroutine while one is parked reading it, which the core's freeze refuses with "Concurrent
  access to a stream"; a cancelled read read again at once; `exit()` in another coroutine during a
  park; `pcntl_fork()` while parked, which the core refuses with ops in flight; `stream_select()` over
  400 streams cancelled mid-wait; held `signal()` Futures at `exit()`, an uncaught exception, a fatal
  error and past the request's shutdown). The drain after a park no longer runs for an op with neither
  a stream nor a handle: a NULL owner matches every record of the Ring (`php_io_ring_drain()`), which
  would block the thread until all of them settle; the Ring answers such an op (a CONNECT of
  `php_network_connect_socket()` without a stream, ext/ftp) with an early Timeout while its own
  completion is pending (`php_io_ring_deliver_one()` leaves `in_flight` set), by reading; a run with
  ext/ftp did not reach it. Refcounts on exception and bailout paths: balanced (a failed arm, a
  Timeout that fires in its subscribe, the IGNORED release, the rebuild's held references). Engine
  state: one defect fixed, the thread's signal mask. `async_signal_reblock()` blocks the watched
  numbers again before every poll, since `zend_sigaction()` unblocks the number it installs a handler
  for; the core's `SignalHandle` records only a block it took itself, so a number the script had
  blocked when `Async\signal()` was called (with `pcntl_sigprocmask()`, or by calling it inside
  `pcntl_signal_dispatch()`, which blocks every signal) and unblocked later stayed blocked in the
  thread after its watch went: a `pcntl_signal()` handler for it never ran again. The registry now
  records the numbers its own reblock blocked and unblocks them with their watch, before re-raising
  what nobody took (`signal/031`). A watch that went inside a pcntl handler (its Future completed,
  cancelled or dropped there) left its number blocked too, the block taken by the handle or by the
  reblock alike: `pcntl_signal_dispatch()` blocks every signal around the handlers and then restores
  the whole mask it found, which undoes the unblock at the watch's end. The cause is php-src's: a
  handler's own `pcntl_sigprocmask()` is undone the same way without async. Fixed on `php-src-fixes`
  `74a581afc06` with its tests (each handler runs under the thread's own mask, so a change it makes
  stays, and a signal arriving meanwhile is queued, not lost), its pull request for Edmond to open;
  `signal/033` is XFAIL until a core update merges it. The forward into the Zend handler table (`SIGG(handlers)`) was run with a pcntl
  handler installed before and after the watch and after the watch went, and with the execution
  timeout firing while a watch lives; FPM's own worker handlers are in that table during a request
  (`zend_signal_init()` saves them, each request copies them), so the forward reaches them too.
  `exec()`, `shell_exec()`, `popen()`, `proc_open()` and `pcntl_exec()` children start with the
  handle's blocks lifted (`php_io_poll_signal_child_mask()`). Sizes: the copy of an ANY op is one
  block whose member count the core allocated first (`safe_emalloc`); 200 000 `signal()` Futures on
  one number cost 82 MiB and left in 0.1 s; watches are at most `PHP_NSIG`. INI entries: none new.
  Test-only code: the S6 test hooks sit under `TRUE_ASYNC_TEST_HOOKS` and IO chaos under
  `TRUE_ASYNC_FUZZ`; a build of the default configuration has no `TrueAsync\Test\` function and no
  test or fuzz symbol (checked). CI: the MySQL service came by the mutable tag `mysql:8.3` and is now
  pinned by digest; the fixture's password and its loopback binding are the entry of 2026-10-05; the
  HTTP fixture listens on `localhost:0` and removes its document root.
- 2026-10-07 Accepted (S6.9): a delivery between `pcntl_signal()` and the reactor's next poll reaches
  the pcntl handler alone and the Future waits for the next one (`signal/032`); a `proc_open()` child
  started while a watch's number is blocked by the reblock alone (the case of `signal/031`) inherits
  the block, since the core's child mask knows only the handle's record: both wait for
  `RFC-CHANGES.md` 5, the hook TrueAsync's core has (`zend_async_sigaction_fn`). A `pcntl_fork()`
  child keeps the parent's watches and their blocked numbers, so a signal sent to a child that never
  waits for it stays pending. `signal_forward()` neither resets a handler installed with
  `SA_RESETHAND` nor performs `SIG_DFL`'s action, as TrueAsync's `libuv_global_signal_callback()`;
  no PHP function installs `SA_RESETHAND`, and what nobody waited for is raised again when the watch
  goes. A handler a C extension installs with a raw `sigaction()` during a request, outside the Zend
  table, does not see a watched number while the watch lives. A build without `ZEND_SIGNALS` has no
  forward: a pcntl handler for a watched number runs only at the watch's end (`dev/plans/S6.md`
  section 8). The registry's unblock undoes a block the script took itself after a reblock, which it
  cannot tell apart, and a script's own `Io\Poll\Context` watching the same number through a
  `SignalHandle` loses its block with it: the core's count of handles per number is not a PHPAPI
  (`RFC-CHANGES.md` 5).

- 2026-10-08 Security pass of the Scope layer (S9.8) over the S9 commits `9567b02`, `73a8469`,
  `7f3068f`, `5f6982c`, `0e8ce93`, `49f90a2`, `05037c7`, `2f78732` (`src/scope.c`, `src/iterator.c`,
  their parts of `coroutine.c`, `scheduler.c`, `await.c`, `true_async.c`, `collector.c`), by checklist
  item, with scripts run on the debug and ASAN builds. Lifetimes: one defect fixed. A finally run the
  scheduler refused for want of a stack (no scheduler coroutine yet, `fiber.stack_size` unmappable)
  released its handlers inside `async_scope_cancel()`'s cascade, so a closure's destructor ran PHP code
  in the loop: one that dropped the object of the scope at the loop's index disposed it, the last
  sibling took its place, and the cascade skipped it, left open. The handlers now stay with the scope,
  whose disposal starts them again or releases them after its walk (`scope/123`). The refused start may
  still release scope objects given back to the GC (S9-scope.md 9 item 28), but no PHP code runs from
  it: a refusal means the scheduler coroutine could not be made, so the GC coroutine cannot be either,
  and a full root buffer only defers the collection (2026-10-08). No use after free found: route handlers
  that drop every object of the route's scopes, dispose and cancel them, spawn into them and replace
  themselves; finally handlers that add handlers to their own scope, dispose its parent and throw;
  `SpawnStrategy` hooks that cancel their scope, drop it and suspend between the hooks; a fatal error
  with handlers' closures whose destructors spawn and make scopes. Refcounts on exception and bailout
  paths: balanced (the route's handler that throws, rethrows or exits, a hook that throws, a refused
  spawn). Engine state: no exception is pending at a handler's call; its error is taken and cleared,
  and an exit ends the request. Sizes: two costs fixed. A child scope searched its parent's vector to
  leave it, so 100 000 child scopes freed newest first took 5.8 s on the debug build; it keeps its index
  now, 0.03 s for the same 100 000 (`scope/125`). A member's finish walked each parent's whole subtree again, so in the
  innermost of 20 000 nested scopes one finish took 6.4 s; the walk up skips the child it came from
  (`scope/124`, 0.001 s). Scope counts and the finally handlers are bounded by `memory_limit`; the
  vectors' capacities are 32-bit and wrap at 2^31 entries, unreachable under it, as the callbacks
  vector's. INI entries: none new. Test-only code: the oracle's hand-out and checks and
  `TrueAsync\Test\iterate()` sit under `TRUE_ASYNC_TEST_HOOKS`; a build of the default configuration has
  no `TrueAsync\Test\` string (checked). CI: no change of S9's own; `tools/windows/` takes the SDK by tag
  and the dependencies without a hash, as the entry of 2026-10-03 says for the Windows lane.
- 2026-10-08 Accepted (S9.8): the subtree walks recurse, as TrueAsync's: nested scopes overflow a
  coroutine's C stack at about 43 000 levels on the debug build, where a plain linked list of
  `stdClass` released in a coroutine overflows at about 6 000; walking by `parent_scope` and the child
  index in O(1) space would remove the limit and is not built. `cancel()` tests each scope of its
  cascade for completion over its subtree, as TrueAsync's, so a cascade down a chain of N scopes with a
  member at the bottom costs O(N^2) (8 000: 0.67 s), and so does releasing N nested `Scope` objects
  outermost first (20 000: 6.6 s). A completion test walks the child scopes until one runs: M members
  that each complete their own child scope rescan N empty siblings ahead of them (20 000 each: 1.5 s),
  and `awaitAfterCancellation()`'s wake rescans a cancelled subtree at every member's end. User code
  pays for the shape it builds, which `memory_limit` bounds.
- 2026-10-08 Accepted: the scope object's `get_gc` walks its subtree for a coroutine when the scope has
  handlers (S9-scope.md 9 item 28), so a collection recurses down a deep chain under such a scope on
  whatever stack runs it, with the limit of the entry above, and a chain of N nested scopes that all
  have handlers and roots in the buffer costs O(N^2) per collection. A coroutine's removal walks up the
  scopes it leaves without coroutines with the same subtree test, the cost shape of the completion
  walk above. A per-scope count of busy child scopes would make both O(1) and is not built.
- 2026-10-08 Security pass of the Context layer (S9.15) over the S9 commits `c3c4fd8`, `e222071`,
  `7e82c15` (`src/context.c`, their parts of `scope.c`, `coroutine.c`, `scheduler.c`, `future.c`,
  `true_async.c`, `test_hooks.c`), by checklist item, with scripts run on the debug and ASAN builds.
  Lifetimes: clean. Only a scope and a coroutine reach a Context by pointer, each owning a reference it
  clears before the release; a freed scope detaches its subtree's contexts first, so `find()`'s walk
  reads live scopes only (a held grandchild's context finds its grandparent's key, then nothing after
  the chain's disposal and collection). A finished coroutine's release window refuses
  `current_context()` and switches and puts a `spawn()` in the global scope; the final release of
  RSHUTDOWN runs after async is deactivated, so every context, spawn, await and signal call there
  throws, and values made there and kept in a static are freed in `zend_deactivate` cleanly.
  Destructors that suspend, rewrite or unset keys during `set()`, `unset()` and the table's free give
  the right output (the core replaces the slot, then releases the old value). Refcounts on exception
  paths: balanced (a throwing old value on `set()` and `unset()`, two throwing values chained on free;
  each scope has one object, so its context is reported once). Engine state: none added. Sizes: keys
  and values under `memory_limit`; `find()` walks in a loop, O(depth) per call. INI entries: none new.
  Test-only code: `print_at_teardown()` and its flag sit under `TRUE_ASYNC_TEST_HOOKS`; a build of the
  default configuration has no `TrueAsync\Test` string (checked). CI: no change.
- 2026-10-08 Accepted (S9.15), the entry above on `get_gc` widened: since S9.12 a scope's context
  alone, made by one `current_context()`, makes the scope object's `get_gc` test its child scopes
  (`scope_can_be_disposed()`, recursive), so a collection with that object in the root buffer
  overflows the GC coroutine's stack under a chain of about 43 000 nested scopes on the debug build
  (50 000 crashed; building them took 87 s through the cancel cascade's O(N^2)). TrueAsync's
  `get_gc` does not recurse, but its other subtree walks overflow at the same depth (S9.8). The fix
  is the per-scope count of busy child scopes named above, not built.
- 2026-10-08 Seen in S9.15, not a defect of ours: an object with `__destruct` made in RSHUTDOWN's final
  release and kept in a static gets "Couldn't execute method ...::__destruct" from `zend_deactivate`,
  as one made in an output handler does without the extension.
- 2026-10-08 S6.10 changes what a Windows `proc_open()` exposes when the extension is loaded: each
  `'pipe'` descriptor is a named pipe (`io-hooks-fixes` `424116620a7`, `create_overlapped_pipe()` in
  `ext/standard/proc_open.c`). Another process cannot take the child's end: the name carries 64
  random bits and the pid, the pipe allows one instance and is made with
  `FILE_FLAG_FIRST_PIPE_INSTANCE` (a name already taken fails and a new one is drawn),
  `PIPE_REJECT_REMOTE_CLIENTS`; the child's end is opened right after, and a client that got there
  first makes `proc_open()` fail. A pipe
  handed to a child is taken off the Ring's completion port before `CreateProcessW()`, and ior drops
  any packet whose `OVERLAPPED` is not one of its ops with a request in flight, so a child's own I/O
  cannot make ior write through an address of the child's (the packet filter's limit is in
  `dev/plans/S6.md` 9.1). CI: the Windows lane loads `php_sockets.dll`, `php_openssl.dll`,
  `php_curl.dll` from the build directory.

- 2026-10-09 Security pass of the Channel layer (S9.21) over the S9 commits `dbeb13c`, `818ea24`,
  `37d49c1`, `ba6412c`, `bba9890` (`src/channel.c`, `src/internal/zval_circular_buffer.c`, their parts of
  `await.c`, `scope.c`, `future.c`, `collector.c`, `true_async_API.c`, `reactor.c`), by checklist item,
  with scripts run on the debug and ASAN builds. Lifetimes: two defects, fixed. One release order, in four
  `free_obj` handlers: the channel's released its buffered values, the rendezvous value and the value a
  close rolled back before `zend_object_std_dtor()`, so a value's destructor reached the channel through a
  `WeakReference` while it was freed (heap-use-after-free on ASAN); `zend_object_std_dtor()` now runs
  first, as in the Context's `free_obj` (`channel/137`, `138`). The same order in `Future`'s and
  `FutureState`'s, code of ours since S5, let a destructor run by the release of the mapper's captures or
  of the result reach the object being freed (heap-use-after-free on ASAN; `future/123`, `124`), and in
  `Timeout`'s, since S5, the destructor of a cancellation passed to `cancel()` (a NULL dereference in
  `isCancelled()`; `await/144`). The coroutine's and the scope's `free_obj` release values before it only
  when their `dtor_obj` did not run, after a bailout, when no destructor runs. And the iterator's step:
  one iterator stepped by two coroutines at once overwrote the first value with the second and leaked it,
  and a released value whose destructor suspended was released again by the other coroutine's step
  (heap-use-after-free on ASAN); each step now receives into its own slot and takes a value out of the
  iterator before releasing it (`channel/139`, `140`). Clean: destructors that suspend, throw, close or
  unset the channel at each release (buffer, close's rollback, a timer's rolled-back value, the iterator's
  value), a value's destructor that steps the same iterator, armed timers across a fatal error, `exit()`,
  `memory_limit` and an uncaught exception, pending `recvAsync()` Futures held in statics to the request's
  end. The stale CHANNEL record the layer Critic traced does not survive: main's finish as a bailout
  aborts it before the destructors, output handlers and RSHUTDOWN (`async_coroutine_finalize()`), a later
  shutdown function does not run after that bailout, and an output handler or a header callback sees no
  queued record. Refcounts on exception paths: balanced (`await_*` over closed channels with chained
  previous exceptions, a send failed by close, a token or a cancel, faults injected at the enqueues of a
  close and a delivery). Engine state: none added. Sizes: the capacity is checked (above `INT32_MAX` a
  `ValueError`) and the buffer grows within `memory_limit` before any state changes; 200 000 channels
  bound to one scope and 20 000 timers cost linear time; timer values are capped at `INT32_MAX` ms. INI
  entries: none new. Test-only code: the channel's hooks sit under `TRUE_ASYNC_TEST_HOOKS`; a build of the
  default configuration has no `TrueAsync\Test` string (checked). CI: no change.
- 2026-10-09 Accepted (S9.21), as TrueAsync's (`channel.c:286-294`): a channel's queues are arrays, so
  serving or dropping N pending `recvAsync()` Futures costs O(N^2) (debug: 40 000 in 2.1 s, 160 000 in 35
  s and 69 MB, within the default `memory_limit`), and so do cancelling N parked receivers (30 000: 1.4 s)
  and waking N parked receivers, each promised a value (30 000: 3.5 s), both bounded by the number of
  coroutines. A linked list through the queue records would make each step O(1) and is not built (Edmond,
  2026-10-09: a rare case). A fatal error inside `close()` leaves the channel closed with its queued
  Futures pending and, on a rendezvous channel, the killed sender's value receivable, the class of
  S9-channel.md 8 item 8.

- 2026-10-09 Security pass of S10 (S10.6) over `fad3680`, `3d6cc85`, `6b482cb`, `3d6089c`, `17b6d11`,
  `8b9ba78` (`src/coroutine.c`, `src/os_signal.c`, `src/scheduler.c`, `src/true_async_API.c`, CI) and the
  core commits they pin (`a203a1259e8`, `68f790cb2d4`, `2a74924668c`), by checklist item, with scripts run
  on the debug and ASAN builds. Lifetimes: one defect, fixed in the core. A fiber whose coroutine was
  cancelled before its body ran, by `Fiber::getCoroutine()->cancel()` since S10.4 or by an `exit()` in
  another coroutine between `start()` and the body before S10, left the coroutine waiting in `start()`
  asleep for good (the deadlock report), and a second `start()` left a coroutine pointing at the
  freed `Fiber` (heap-use-after-free in `zend_fiber_coroutine_dispose()` on ASAN): the scheduler
  finishes such a coroutine without the fiber's entry point, the only code that woke the starter.
  `async-core` `060bc7e104e`: a finish handler added at `start()` ends the fiber and wakes the
  starter with the error, which `start()` throws, as in TrueAsync (`fiber/033`-`035`). The fix
  moved where `test_scheduler/035`'s out-of-memory error falls and showed an older defect: an
  out-of-memory fatal error in the script was printed twice, the second time "in Unknown on line 0",
  because the from_main call after the destructors made a new main coroutine in an object store that
  reuses no handle after the destructors and had no memory to grow. That call now makes none, in our
  scheduler (`src/scheduler.c`) and in `test_scheduler` (`5610980dc8f`), and returns async to READY
  as TrueAsync does (`scheduler/108`, `test_scheduler/095`). Clean: a Windows watch and its trigger
  across `exit()`, an uncaught exception and RSHUTDOWN (`signal/024`, `027`, `028`); the console
  handler reads the trigger and the watched numbers under the lock and the arrived numbers through an
  atomic; `Fiber::getCoroutine()` before `start()` (`fiber/032`) and with the fiber's coroutine
  cancelled while it waits (`fiber/031`). Refcounts on exception paths: balanced (the woken starter
  takes the coroutine's exception, and the coroutine keeps none). Engine state: one field,
  `zend_fiber.start_handler_id`, at the end of the struct. Sizes: watched numbers bounded by
  `ASYNC_SIGNAL_SLOTS`, at most 32 on Windows (a `static_assert`). INI entries: none new. Test-only
  code: under `TRUE_ASYNC_TEST_HOOKS`, as before. CI: `CORE_REF` bumps only; permissions read-only.
- 2026-10-09 S10.6 (Edmond, 13:09): the request's last print of uncaught exceptions runs in a
  coroutine of the last pass (`DECISIONS.md`, 2026-10-09). Exposure checked: an `exit()` or a fatal
  error in its `__toString()` still ends the request, and nothing it spawned runs after
  (`scheduler/109`, `110`); the deadline of the drain before it does not cut it (`reactor/044`), so a
  `__toString()` that waits holds the request as it did when `main.c` printed, until a graceful
  shutdown started during the print arms D16 again; after a bailout no class's `__toString()` prints, the exit
  exception included (`scheduler/112`; before, `main.c` printed it with its class's `__toString()`).
- 2026-10-09 Accepted (S10.6), as TrueAsync's libuv, which accepts every number: on Windows a watch
  on a number the console never delivers (`SIGTERM`, say) keeps the scheduler waiting instead of
  resolving a deadlock for as long as the watch lives, as a submitted wait does on Unix. While a watch
  waits for SIGINT, SIGBREAK or SIGHUP, the handler hides that event from the handlers registered
  before it and from the default one, and on a console close it blocks Windows' handler thread in
  `Sleep(INFINITE)` to give the script the seconds Windows allows, as libuv does.
- 2026-10-09 Accepted (S10.6): between `SetConsoleCtrlHandler(handler, FALSE)` at MSHUTDOWN and the
  unload of a shared build, a handler call already running on Windows' thread may still be inside the
  handler; it reads only process-wide state that MSHUTDOWN leaves in place, and no later event reaches
  it.
- 2026-10-09 Seen in S10.6, not a defect: `Fiber::getCoroutine()` registered on `Fiber` by a MINIT that
  fails afterwards stays registered, but a failed MINIT ends the process
  (`zend_error_noreturn(E_CORE_ERROR, "Unable to start %s module")`, `Zend/zend_API.c:2511-2513`).
- 2026-10-09 Left (S10.6), not a security defect: an out-of-memory fatal error in a destructor of the
  store's pass at shutdown (an object in a static property, say) reaches the last from_main call with
  a bailout, which still makes a new main coroutine, and the message is printed twice (probe on the
  debug build). Telling that call from a shutdown function's, which always has a bailout too, needs
  the core to say which call it is; `PLAN.md` S10, open questions.
- 2026-10-09 Found in S10.6 on `8b9ba78` and fixed by the step: an exception printed as uncaught at the
  request's end whose `__toString()` throws leaked (152 bytes, LeakSanitizer on the ASAN build with
  `USE_ZEND_ALLOC=0`): `main.c` printed the exit exception, and the throw with no frame bailed out
  before the print released it. The print coroutine prints it through `scheduler_print_uncaught()`,
  which releases it on a bailout; the same script reports no leak now (`scheduler/110` on ASAN).
- 2026-10-12 S14.5: the two incidents above have fault tests now: the drain of an op with no owner
  (`io_provider/043`, which hangs with the fix reverted) and the trigger fired without pause (`reactor/051`,
  which runs out its million refires with the fix reverted). The seams `fail_io()`, `read_without_owner()` and
  `trigger_refire()` are test hooks under `TRUE_ASYNC_TEST_HOOKS`, absent from a default build. Found and fixed
  by the step: an `exit()` in main's script whose exit deadline could not be armed, or whose forked child could not
  resubmit the reactor's own ops or a signal wait, ended in a fatal error with no frame, which skipped every
  `finally` block (`reactor/052`, `054`, `signal/043`). Left open: the same rebuild's PHP calls for a signal
  context or handle (PLAN open questions).

- 2026-10-12 Security pass of the TaskGroup layer (S9.31) over the S9 commits `25b3431`, `507e2c6`, `4066d82`,
  `11a9776`, `6c30875` (`src/task_group.c`, `task_group.h`, their parts of `scope.c`, `coroutine.c`, `future.c`,
  `collector.c`, `await.c`, `scheduler.c`), by checklist item and the six points of `dev/plans/S9-taskgroup.md`
  section 7. Scripts ran on the debug and ASAN builds; a second reader went over the code; three Critics reviewed
  the fixes.

  Lifetimes, three defects with tests. A release that does not free a value can start a GC run, and a live
  coroutine waits for that run in the GC coroutine while other coroutines run (the pinned core's `zend_gc.c`,
  `zend_gc_collect_cycles()`). `all()` built its composite inside its walk over the tasks, so a spawn during that
  wait grew the table under the walk: a heap-use-after-free on ASAN (`task_group/159`). `joinAll()` built it
  before it took the set's entries, so it also took a running and a queued task spawned during the wait: an
  assertion on debug, and on a release build a freed entry left in the queue (`task_set/036`). Both now collect
  the errors first and build after; a set's read takes its entries before the build. `any()` and a failed
  `joinAny()` run the same code and have no test of their own. A warning handler of the constructor that reached
  the group through `debug_backtrace()` could construct it again, and the outer call then took a second `Scope`
  reference, which leaked (`task_group/160`).

  Lifetimes, two defects by reading, without a test. The drain released a queued call through its entry after a
  refused start had settled it, and a destructor run by that release can take the entry out of a `TaskSet` and
  free it (only when the scheduler cannot make a stack); the call now leaves the entry first. The walks over the
  pending Futures went on by index after an answer, so a waiter that another coroutine removed meanwhile moved
  the next waiter past the walk; they now start again after such a removal.

  Cancellation. After `exit()` or an uncaught error the drain kept starting queued tasks: 30 queued tasks that
  wait held the request 7.4 s on the debug build. A task queued before one of the shutdown's cancels now ends
  unstarted with a new `AsyncCancellation("Graceful shutdown")`, as that cancel ends a queued coroutine unrun,
  and the group goes on: 0.03 s (`task_group/161`). A task queued after the last cancel, as in a shutdown
  function, starts (`162`). The Critics found two faults in the first versions of this fix: cancelling the whole
  group broke groups made in shutdown functions and cut short the running tasks' `finally`; and an unstarted
  call leaked its `__call` trampoline (`163`). One flag for "the shutdown began" also let the tasks queued after
  `Async\graceful_shutdown()` start after a later `exit()`; a count of the cancels fixed it (`164`).

  Answered, no change. A constructor that throws gets no destructor, PHP's rule for any object: a group that the
  handler constructed before the outer call threw is not cancelled when it is dropped, and its task runs on (no
  leak; a script on debug). A cancellation of the caller during the GC wait inside `all()` returns with the
  cancellation pending: the dropped Future warns of its unhandled composite, so no error is lost without a word
  (a script, debug and ASAN). Tests 159 and 036 print whether the collection ran inside the call.

  Clean, by scripts: the destructors of queued calls that a cancel releases (they throw, `exit()`, spawn into the
  group, drop it, cancel and dispose it, call `gc_collect_cycles()`); a destructor that reaches the closing group
  through a `WeakReference`, calls every method, `finally()` included, and keeps the group; the Futures of
  `all()`, `race()`, `any()`, `joinNext()`, `joinAny()` and `joinAll()` freed after the group, before it, and held
  in a static past `exit()`, a fatal error and an uncaught error; a finally handler that exits, fails fatally,
  throws, waits for good, cancels itself or exhausts `memory_limit` (after a bailout the closing's reference is
  freed with the request); a finally run refused for want of a stack.

  Refcounts on exception and bailout paths: balanced. Engine state: `EG(exception)` is saved and restored around
  the task's end, the destructor and the closing's end; nothing else. Sizes: nothing is allocated by
  `$concurrency` or `$queueLimit`; a queued task costs 293 bytes on the debug build, and 100 000 of them spawn in
  0.08 s, cancel in 0.02 s and answer `all()` in 0.03 s; the room count sums two 32-bit counts and wraps only
  past 2^31 tasks behind a parked spawner. INI entries: none new. Test-only code: one call under
  `TRUE_ASYNC_TEST_HOOKS`; a build of the default configuration has no `TrueAsync\Test` string and no test
  symbol (checked). CI: no change of the layer's.
- 2026-10-12 Seen in S9.31, the class of the open question on a coroutine cancelled before it ran (`PLAN.md`):
  where a finished task's teardown releases values, nothing can wait, so a `__destruct()` that waits fails with
  `Cannot switch coroutines in the current execution context` (a result freed with the group when its closing
  ends at the last task's end, a queued call the drain's cancel releases); an unheld coroutine's result does the
  same without the layer. After a pending `exit()` the closing ends in the scheduler's tick (`PLAN.md`, Open
  questions, S9.30).

- 2026-10-12 The walks of the earlier layers against a GC run that a release inside them starts (S9.33, the open
  finding of S9.31 and S14.1's on `channel_put()`), over `scope.c`, `channel.c`, `future.c`, `await.c` and
  `context.c`, with `iterator.c` and the TaskGroup's `task_group_complete()` the open finding named. Three readers
  went over the files, every finding ran as a script on the debug and ASAN builds before its fix, a Critic and the
  Sage reviewed the fixes.

  Lifetimes, five defects with six tests, each a use after free on ASAN. `Scope::cancel()`, `dispose()`,
  `disposeSafely()` and a scope object's destruction walk the child scopes and coroutines and release a cancellation
  on the way (a coroutine's earlier one, a zombie's); during the wait a coroutine finished and freed itself and the
  scope (`scope/191`, `192`). The TaskGroup destructor's cancel of what remains in an external scope did the same
  (`task_group/166`). A child scope's free closes the channels made in it and releases the rejected Futures'
  exceptions: another coroutine freed the parent, which the free read next (`scope/190`), or dropped the channel,
  which a pending `recvAsync()` Future does not hold (`channel/155`). Both walks and the close by the owner scope now
  run in scheduler context, as `scheduler_cancel_all()` does: a GC run defers to the next tick instead of parking
  the walk. By reading, what they free is exceptions the C code made and timer events; a refused finally start keeps
  its handlers for the scope's disposal, so no destructor that could wait runs there.
  A Traversable giving a key twice made `zend_hash_update()` release the earlier result in place; its destructor
  suspended the walk, the wait ended and freed the results table, and the store wrote into it
  (`await/148`). The replaced value is now released after the store, and the walk wakes no waiter once the wait
  has ended.

  The channel's state, four defects with tests. `recvAsync()` and a `send()` to a pending one released the taken
  value before the channel woke the sender and marked the slot: a sender run during the wait put its value into the
  free slot, so a parked sender was never woken (`channel/156`), or the later value was marked committed and was
  received after its `send()` threw (`150`). The Future holds the value, so the release is a plain decrement now,
  as `await.c`'s walk does. S14.1's hold on a Future with a `map()` child was released by the waiter's dispose in
  the same place: the later value was marked committed again (`152`), and a buffered slot freed by a value handed
  on from a cancelled receiver went to a later sender before the one waiting (`153`). The serve releases the hold
  last, and the slot is marked committed when a receiver is promised it, not by `channel_put()` after the wake.

  Cancellation, two defects with tests. The GC wait returns with the caller's cancellation thrown in the middle of
  C code. `recv()` with a coroutine as its token released the token after the take, so the cancel dropped the
  message (`channel/151`); the caller's argument holds that coroutine, so the release is a plain decrement too, and
  an event token's release that does not free it starts no run. This closes the window inside the C code only: the
  VM's release of `$this` when the call ends can start the run too, and the cancel then drops the message as before
  (`154`, XFAIL, the open finding below). Accepted: a Future token whose event the release
  frees, after a second `Future::__construct()` during the call, can run a destructor that suspends there.
  `await_any_or_fail()` chaining a later item's error over an earlier one that its chain held already released it,
  and the throw that followed chained the cancellation into the items' stored errors (`await/149`); that call runs
  in scheduler context now.

  By reading, without a test: `iterator.c` chained a worker's exception over the iterator's before storing it, and
  the chain's release of the earlier one could let another worker chain over the reference given away; the new one
  is stored first. Clean, by reading: `context.c` (lookups only; the core's set and unset release the old value
  last), `future.c`'s chain free (a detached stack copy), drain (copies by value, re-reads its head) and reporting
  walks, `await.c`'s rest and array walks (unique keys, no release inside), `Channel::close()`'s receivers walk
  (holds `$this`, re-reads the length), the scope's completion, give-back and error-route walks (scheduler context, or
  a finished coroutine, which cannot wait). `task_group_complete()` reads a refused finally start right: the start
  returns false without its own exception only when async is off, where no GC run waits. Accepted: a drain coroutine
  cancelled by the shutdown during a GC wait after a mapper reads the cancellation as the mapper's throw, as it does
  for a mapper that suspends; a channel iterator step that finds a value another step stored meanwhile releases it
  after its own, so a cancel there leaves that value unseen.

- 2026-10-12 S9.38: the core's automatic GC run no longer parks the coroutine whose release fills the root buffer
  (DECISIONS 2026-10-12 S9.38), which narrows the GC's share of the S9.33 open finding: a release, the VM's of
  `$this` included, lets a cancel in only when its coroutine is 100 000 roots of its own past the queued run, and
  `channel/154`, whose release fills the buffer, passes. S9.33's tests reach their
  windows 100 000 roots past the threshold (ten fail on ASAN with S9.33's fixes reverted), except `channel/153`:
  a receiver resumed by its cancel cannot get that far, so the hold released last has no reproducer now. What is
  left is the open finding below, narrowed.

- 2026-10-13 Security pass of the pool layer (S9.37) over `f83c93d`, `bc889c5`, `dafdcdc` (`src/pool.c`, `pool.h`), by
  checklist item and the six points of `dev/plans/S9-pool.md` section 7. Scripts ran on the debug and ASAN builds;
  a second reader went over the code with scripts of its own and 300 seeds of a random hook driver (140 on ASAN).

  Lifetimes, three defects with tests, all in the constructor's pre-warm, whose factory reaches the pool through
  `debug_backtrace()`. A factory that acquired from the pool took a place the pre-warm still filled: count() read
  3 with `max` 2, and the release that followed failed the idle buffer's assertion; a build without assertions
  overwrites the buffer's oldest slot, which leaks, and counts past its size, so two pops return one zval and
  release it twice (by reading) (`pool/144`). A factory that closed the pool left the next resources idle in the
  closed pool, freed without the destructor callable (`145`). A waiter that parked while the factory suspended
  was not woken for the resource the pre-warm then made idle, and deadlocked (`146`; the Critic). The pre-warm
  now stops as the healthcheck's refill does, at `min` resources, at no free place, or at a close, which destroys
  the resource just made; it wakes a waiter for each resource made, and calls the factory at most `min` times.

  Section 7. `$min` and `$max`: clamped to `UINT32_MAX`; `max` up to `PHP_INT_MAX` sizes nothing up front (the
  buffer doubles), `min` up to it ends in `memory_limit`'s fatal error and a clean free; the interval and the
  timeout saturate. The destructors run by `close()` and a free: each resource leaves the buffer first; a
  destructor that throws, exits, suspends, calls `gc_collect_cycles()`, `close()`, `acquire()` or `release()` gets
  a closed pool; the free releases the buffer before the weak-key table. A hook that closes the pool: covered by
  `074` and S9.35's tests; a check that closes its own pool stops at its cancellation. A hook or a destructor
  that drops the last reference: the method's `$this`, the check's reference and `dtor_obj`'s hold keep the pool.
  A hook that re-enters `acquire()` or `release()`: a `beforeAcquire` that acquires recurses until PHP's stack
  limit throws its `Error`, which passes; a second release of the resource in `beforeRelease` throws; a hook
  acquiring from its own full pool ends in the deadlock report. The check and a free: the check holds the pool,
  and its last release closes it in the check coroutine; a request end and a fatal error with the check
  suspended are clean. PHP's GC during `close()`'s loops: a pool in a cycle with its resources, a destructor that
  suspends and collects again, other coroutines reaching the pool through a `WeakReference` meanwhile: clean.

  Refcounts on exception and bailout paths: balanced (the rejection, the strategy's holds, the failure object,
  the refill's factory error); a bailout with waiters parked, with and without a promise, is clean. Engine state:
  `EG(exception)` is cleared before user code and settled after, as S9.36's rule says. Sizes: a check every 1 ms
  adds no memory; the check's errors are kept until the request ends (Edmond's answer C, S9.35). INI entries:
  none new. Test-only code: the collector's wake check sits under `TRUE_ASYNC_TEST_HOOKS`. CI: no change.
  Answered, no change: after `exit()` in a destructor callable `close()` still destroys the other idle
  resources (DECISIONS 2026-10-13 S9.37); PHP's GC runs the resources' `__destruct` before the pool's destructor
  callable gets them, PHP's order for a cycle.

- 2026-10-13 Security pass of the iterate() layer (S9.42) over `b3751b0`, `d272317`, `32af4ab` (`src/iterator.c` from
  "iterate()", `Async_iterate` in `true_async.c`, `async_exception_merge()`), by checklist item and the points of
  `dev/plans/S9-iterate.md` section 5. About 40 scripts ran on the debug and ASAN builds; a second reader went over the
  code with 32 scripts of its own and 720 seeds of a random driver on ASAN (callbacks and iterators that throw, suspend,
  cancel their worker or the caller, drop scopes, re-enter the source, nest `iterate()`, exit); the layer's tests ran
  under 20 fuzz seeds on ASAN.

  One defect, of reporting, with tests; no memory defect. An exception the caller reaches through the one it caught was
  also added to the unobserved exceptions, printed "Uncaught" at the request's end and set exit code 255: a callback's
  exception that a coroutine it spawned rethrew from a `finally` run by the walk's cancel, arriving as the walk's
  exception and as the route error (`iterate/061`, the second reader); one rethrown by two such coroutines, a second
  route error (`062`, the Critic); the previous of the callback's exception rethrown so (`063`, the Critic); the same
  `Error` from a pool's `beforeRelease` hook and its destructor (`pool/147`). The merge saw the object held elsewhere
  and set it aside. `async_exception_merge()` now releases an exception set aside that is already in the pending one's
  chain, and the route subscriber ignores a second route error already in the chain of the walk's exception or of the
  first route error (`async_exception_in_chain()`). Left: a second route error that a callback later wraps into the
  walk's exception (it awaited the failed coroutine) still prints; a `previous` chain made cyclic through
  `Closure::bind()` hangs the walk, as php-src's `zend_exception_set_previous()` does.

  Section 5. The iterator's scope disposed from a callback: it has no object, and `getChildScopes()` lists only scopes
  that have one; a `Scope::inherit()` child disposed or disposed safely, the caller's `Scope` disposed, cancelled or
  dropped from a callback: clean. `iterate()`, `foreach`, `next()` or `send()` over the walked generator from its own
  callback, also while another worker is suspended in the generator's body: the core throws "already run", "already
  running" or "already closed"; the walk's move guard holds. A GC run that a release in the walk starts: the core's wait
  was reached in the release of a callback's result (`iterator.c`, `zval_ptr_dtor(&retval)`, seen with gdb) with the
  caller cancelled during it, one and three workers: clean; elements, keys, results and the walked object whose
  destructors suspend, throw or collect cycles, at the walk's end and at the caller's release: clean. The caller leaving
  while it waits: a second cancel, `exit()`, a fatal error, a deadlock report and the request's end with workers parked
  or inside a generator's move: clean; ASAN reports leaks only after a bailout, as a script without `iterate()` does.

  Lifetimes: the walk's references (the creator's, the caller's, one per worker, one per queued microtask) and the scope
  subscriber's removal against `iterate_scope_freed()` traced; the last worker always sets FINISHED, so a microtask
  queued later spawns nothing into a freed scope. Refcounts on exception and bailout paths: balanced; every caller of
  the merge passes references it owns. Engine state: `EG(exception)` is saved around the `$cancelPending` cancel; each
  worker calls with its own copy of the arguments. Sizes: `$concurrency` 0 over an endless generator with callbacks that
  suspend adds a worker per element until a fatal error, `memory_limit`'s or the fiber stack's `mprotect` at
  `vm.max_map_count` (32 631 workers here), as `spawn()` in a loop does; the limit is the caller's, as in TrueAsync. The
  array copy is bounded by `memory_limit`. INI entries: none new. Test-only code: `TrueAsync\Test\iterate()` stays under
  `TRUE_ASYNC_TEST_HOOKS`. CI: no change. Answered, no change (DECISIONS 2026-10-13 S9.42): a callback's exception held
  elsewhere takes no previous, so a spawned coroutine's later error outside its chain prints at the request's end
  (S9.36's rule); a later exception of the walk still gets the earlier one as its previous, a user's object included, as
  in TrueAsync. Seen, not a defect: with the core's GC wait in a worker, the next callback started before the queued
  caller ran its cancel, which reached that callback at its first suspension. A result whose destructor throws is seen
  at the next step: an array walk consumes one element of its private copy, a `Traversable` walk stops at the `valid()`
  check, and a user iterator's methods are not called with the exception pending (by reading and a script).

- 2026-10-13 Security pass of the FileSystemWatcher layer (S9.47) over `95924f0`, `57bc535`, `a04ece7`, `770d95e`
  (`src/fs_watcher.c`), by the checklist and the items of `dev/plans/S9-fs-watcher.md` section 5. `open_basedir` and
  paths: `$path` by `Z_PARAM_PATH` (a NUL is a `ValueError`), checked and expanded against the virtual current
  directory; the recursive walk skips symlinks (`DT_LNK`, `lstat()` for `DT_UNKNOWN`) and adds a subdirectory with
  `IN_ONLYDIR | IN_DONT_FOLLOW`; a directory created or moved in is added by its path under the checked root. Only
  another process can swap a middle component for a link, PHP's `symlink()` and `rename()` check `open_basedir`; the
  root's check-then-watch window is the one every PHP file function has. Names reported are the kernel's names under
  the root or the root's basename. The raw buffer nobody reads: on the request allocator, so bounded by `memory_limit`
  only (section 6 item 6). The 8192-directory cap: the root counts, the rest stays unwatched without an error; a full
  walk compares paths O(n^2) at the cap, bounded by it. Fixed: the walk recursed in C with one open `DIR` per level,
  so a tree 1000 levels deep crashed a coroutine with `fiber.stack_size=128K` (SIGSEGV on the debug and the release
  build, `fs_watcher/071`); it now reads breadth first over the watcher's own nodes, one directory open at a time.
  Fixed, no test: a bailout (`memory_limit`) out of an allocation leaked a descriptor past the request, the walk's
  open `DIR`s, an `inotify` instance made before the registry's allocation, and on Windows the directory handle and
  the wide path made before the node's; the registry now holds the walk's directory and closes it when freed or at the
  next walk, and the allocations come before the descriptors. A test would need the fatal error inside the walk and a
  look at the descriptors after the request, which a `.phpt` cannot take. `close()` or the destructor from inside the
  drain: the drain runs no PHP code (the wake only queues), dispatch re-checks each node against the live users,
  `draining` keeps the registry; a `FileSystemEvent` is not a GC root (500 raw events added none), so the drain cannot
  reach a GC run. A shared watch descriptor: one node per user, `inotify_rm_watch()` with the last one only
  (`fs_watcher/043`). The fork rebuild: a refused root or subdirectory closes or drops its node; a child that closes
  before it rebuilds makes no system call on the parent's queue (strace, io_uring and the Poll queue;
  `fs_watcher/048`, `052`). Refcounts on exception and bailout paths: balanced. INI entries: none. Test-only code:
  `ASYNC_TEST_IO_FAULT` is `false` without `TRUE_ASYNC_TEST_HOOKS`. CI: no change. Left as is: on Windows a bailout in
  `fs_record_name()` leaks the converted name (libc memory, one name per fatal error); the kernel's notification
  records are trusted within the op's result, which the core checks against the buffer's length (RFC-CHANGES 39).

## Open findings

- 2026-10-12 (S9.33, narrowed by S9.38) A C function that releases something after it has taken a value can lose
  that value to a cancel of the caller when the release suspends: a destructor that waits, or the core's GC wait,
  which is cancellable (`zend_gc.c`, "false means this coroutine was cancelled") and since S9.38 happens only to a
  coroutine that has added 100 000 roots past the queued run without yielding, as a loop that makes garbage does.
  S9.33 fixed the sites that change shared state. Letting that wait defer the cancellation would close its share;
  it needs a delivery point that `protect()` alone has today (the Sage's ruling). Owner: `PLAN.md`, Open questions.

