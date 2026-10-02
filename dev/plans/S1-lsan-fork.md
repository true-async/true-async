# S1: LeakSanitizer after fork with the ior thread backend

Open item of S1.2, handed to session PHP3 on 2026-10-01. Worktree `~/php-src3`, branch
`iohooks-lsan-fork`.

## Cause

LeakSanitizer in a forked child warns about every thread of the parent that was running at the
fork. ASAN's thread registry is copied into the child with those threads still marked running;
the child has only the forking thread, so the leak check at exit cannot suspend the others and
prints `Running thread N was not suspended. False leaks are possible.` to stderr, which
run-tests compares with `--EXPECT--`. Neither ior nor PHP adds anything: the warning comes from
any program that forks while a second thread runs.

The earlier C repro (`lsan-repro/forkthr.c`) did not show it because it forked before the new
thread reached ASAN's thread start, while the thread was still `Created` in the registry. The
same program with a barrier that waits for the thread to start (`lsan-repro/forkthr-barrier.c`)
prints the line 3 of 3 runs with gcc 13.3 and 1 of 1 with clang 18.1.

Upstream compiler-rt (`main`, read 2026-10-01) keeps this behaviour: `ReportUnsuspendedThreads`
in `lsan_common.cpp` reports unconditionally, and the child's `AfterFork` in `asan_posix.cpp`
only releases locks. `thread_suspend_fail=0` (absent from gcc 13's libasan) skips the leak
check but still prints the line. No matching issue found in llvm/llvm-project.

With the io_uring backend `probe.php` shows one thread in the parent, so the child has nothing to
report; that backend stays green.

## Measurements

| Revision | `ring-fork` | `ring-orphan-outputs` | `ring-destroy-signal` |
|---|---|---|---|
| `056d9f803a3` | 1 of 1 FAIL | 1 of 1 FAIL | 1 of 1 FAIL |
| `b05a2fd63e5` (bukka's head, ff-merged) | 5 of 5 FAIL | 5 of 5 FAIL | 3 FAIL, 1 PASS, 1 pass on retry, of 5 |

ZTS ASAN build of `~/php-src3`, `IOR_BACKEND=threads`, `run-tests.php -n --asan`. The diff of
every failure is the warning line alone.

## Where a fix can go

Open, for Edmond. ior cannot stop its workers around a fork: a worker may sit in a blocking call
such as `waitpid`, which `ring-destroy-signal` relies on. ASAN exports no call that removes a
thread from its registry.
