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

## Open findings

None.

