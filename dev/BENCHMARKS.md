# Benchmarks

The results journal: every measurement with its date, builds and outcome. The method and the
benchmarks are `dev/plans/S3.md`, section 12; the scripts are `bench/`, the runner `tools/bench.py`.

## 2026-10-08, S9.20: Channel rendezvous and buffered transfer, counted

**Builds.** Release, ZTS, `-O2`, gcc 13.3, run with `-n`, no opcache; one count per side, the child
pinned to one CPU (cachegrind counts). Both cores configured with `tools/ci/build-core.sh`'s flags
without `--enable-debug`.

- `ours`: the pinned core `77dbfc061f3` (`async-core-io-2026-10-08-4`), ior `d46649f6425`, and our
  extension as a `phpize` module of `ba6412c` (S9.19), the test hooks off.
- `ref`: the fork core `863f6dd90cf` with `ext/async` `1fdacf8` built in, libuv 1.48.

`B15` sends N messages back and forth between two coroutines over two `Channel(0)` (`bench/b15.php`); one
operation is one message. `B16` sends N values from 4 producers to 1 consumer through a `Channel(64)`
(`bench/b16.php`); one operation is one value.

| Bench | ours | ref | ours / ref |
|---|---|---|---|
| B1 | 2,965.0 / 1.020 | 3,128.9 / 2.030 | 0.948 |
| B15 | 1,391.0 / 0 | 1,755.0 / 1.000 | 0.793 |
| B16 | 694.5 / 0 | 731.6 / 0.055 | 0.949 |

Instructions / allocations per operation; page faults and system calls 0 on every row. Known answer: the
`known` variants send a `new stdClass` and count +369.0 / +1 (ours) and +380.0 / +1 (ref) on `B15`, +308.1 / +1
and +313.0 / +1 on `B16`; `B1-known` +370.0 / +1 and +372.2 / +1. Two runs gave the same numbers. Our
channel allocates nothing per message; the reference's rendezvous allocates one block per message.

## 2026-10-08, S9.14: find() up the scopes, and a coroutine's context

**Builds.** Release, ZTS, `-O2`, gcc 13.3, run with `-n`, no opcache; one count per side, the child
pinned to one CPU (cachegrind counts).

- `ours`: the pinned core `662dfe91919` (`async-core-io-2026-10-08`) and our extension as a `phpize`
  module of `7e82c15` with S9.14's changes (a comment in `src/context.c`), the test hooks off.
- `ref`: the fork core `863f6dd90cf` with `ext/async` `1fdacf8` built in.

`B13-1`, `B13-10` and `B13-1000` call `find()` of a missing key from a scope 1, 10 and 1 000 levels
below a scope of the script, each level's context holding one key (`bench/b13.php`); one operation is
one `find()`. `B14-1000` is `B1-1000` with `coroutine_context()->set()` and `get()` in every coroutine
(`bench/b14.php`); one operation is one coroutine.

| Bench | ours | ref | ours / ref |
|---|---|---|---|
| B1 | 2,965.0 / 1.020 | 3,129.0 / 2.030 | 0.948 |
| B1-1000 | 2,945.2 / 1.002 | 4,233.4 / 2.006 | 0.696 |
| B13-1 | 272.0 / 0 | 298.0 / 0 | 0.913 |
| B13-10 | 641.0 / 0 | 748.0 / 0 | 0.857 |
| B13-1000 | 41,230.9 / 0 | 50,247.9 / 0 | 0.821 |
| B14-1000 | 4,232.3 / 3.002 | 5,465.6 / 4.006 | 0.774 |

Instructions / allocations per operation. Known answer: `B1-known` (3,335.0 / 2.020 and 3,501.1 /
3.030) adds one `new stdClass` per spawn and counts +370.0 / +1.000 over `B1` on `ours`, +372.1 /
+1.000 on `ref`. `find()` costs about 41 instructions per level on `ours` and 50 on `ref`, and
allocates nothing on either. A coroutine's context with one `set()` and one `get()` costs `ours`
1,287 instructions and 2 allocations over `B1-1000`, `ref` 1,232 and 2. Page faults and system calls
per operation are 0.

## 2026-10-07, S9.7: spawn to finish with the scope, and awaitCompletion()

**Builds.** Release, ZTS, `-O2`, gcc 13.3, run with `-n`, no opcache; one count per side, the child
pinned to one CPU of 4 (cachegrind counts, so the load of the other lanes does not change them).

- `ours`: the pinned core `0145ca90d78` (`async-core-io-2026-10-07-6`) and our extension as a
  `phpize` module of `05037c7` with S9.7's changes, the test hooks off.
- `before`: the same core and the extension of `d196cbd`, the last commit before S9.2, which has no
  scope (it builds and runs on this core; the note's "before S9.2" side, on the same core rather than
  that commit's `8f89755d2b1`, so only the extension differs).
- `ref`: the fork core `863f6dd90cf` with `ext/async` `1fdacf8` built in.

`B1-1`, `B1-1000` and `B1-unbatched` are B1 at batches of 1, 1 000 and 100 000 spawns before their
awaits; `B12-1000` and `B12-100000` spawn N members into a new Scope and wait with one
`awaitCompletion()` (`bench/b12.php`). B12 has no `before` row: there is no Scope.

| Bench | before | ours | ref | ours / before | ours / ref |
|---|---|---|---|---|---|
| B1 | 2,702.0 / 1.020 | 2,889.0 / 1.020 | 3,128.9 / 2.030 | 1.069 | 0.923 |
| B1-1 | 4,100.0 / 3.000 | 4,292.0 / 3.000 | 4,793.6 / 5.000 | 1.047 | 0.895 |
| B1-1000 | 2,682.2 / 1.002 | 2,869.2 / 1.002 | 4,233.4 / 2.006 | 1.070 | 0.678 |
| B1-unbatched | 2,743.8 / 1.000 | 2,937.0 / 1.000 | 378,032.5 / 2.000 | 1.070 | 0.008 |
| B12-1000 | | 2,433.4 / 1.003 | 3,887.0 / 2.007 | | 0.626 |
| B12-100000 | | 2,429.1 / 1.000 | 127,645.9 / 2.000 | | 0.019 |

Instructions / allocations per operation. Known answer: `B1-known` adds one `new stdClass` per spawn
and counts +370.0 instructions and +1.000 allocation on `ours`, +370.0 / +1.000 on `before`, +372.1 /
+1.000 on `ref`. The scope adds about 190 instructions per spawn and no allocation, flat from 1 to
100 000 members. The reference's cost per spawn grows with the number of live members (its code
removes a member by a linear search of the scope's vector, `scope.c:58-73`), and it allocates one
more block per spawn. Page faults and system calls per operation are 0 but for the unbatched runs,
whose 100 000 live coroutines fault 0.14-0.21 pages per spawn.

## 2026-10-07, S5.6: a callback keeps its index in the vector

**Builds.** As the S5.5 entry: release, ZTS, `-O2`, gcc 13.3, the core `1ee473ff67b` of that entry
(the extension's code changed only in the files measured), our extension as a `phpize` module of
`main` at `bbe516b` with S5.6's changes; `before` with `src/true_async_API.{c,h}` and `src/await.c`
of `4268e50`, `after` with S5.6's: a callback keeps its index in its vector, so its removal
searches nothing (S3.md section 12, "Linear unlink under fan-in"); an `await_*` wait unlinks
its records in reverse link order, and its table of reservations folds the address into the key (B9
only uses that table). One count per side.

| Bench | before | after | after / before |
|---|---|---|---|
| B1 | 2,701.8 / 1.020 | 2,702.0 / 1.020 | 1.000 |
| B9-100 | 299,470.0 / 316 | 293,037.5 / 316 | 0.979 |
| B11-1000 | 7,974.5 / 5.004 | 6,494.0 / 5.004 | 0.814 |
| B11-10000 | 22,032.4 / 6.000 | 7,051.9 / 6.000 | 0.320 |

Instructions / allocations per operation. B11's cost per waiter grows 1.09 times from 1 000 to
10 000 waiters, 2.76 times before. Two searches measured on the way, each worse: from the end alone
B11-10000 24,534.4 (B11's waiters leave in link order, so their records sit near the front); from
both ends 17,036.4, still O(N^2) for two waits sharing a vector. The security cases on the debug
build: one pending Future repeated 200 000 times in `await_any_or_fail()` 8.07 s before, 0.040 s
after; two waits over the same 200 000 copies 30.8 s before, 0.016 s after, 10^6 copies 0.081 s.

## 2026-10-07, S7.4: B6, one walk of the collector

**Builds.** The debug ZTS core of the lanes (`pocs-dbg`, the pinned core `1ee473ff67b`), our
extension with the test hooks at `96449e3` plus S7.4's change; 4 CPUs, three runs each, ranges. Not a
release build: these are bounds. `bench/b6.php` times one `get_deadlocked_coroutines()` with
`hrtime()` and takes the memory it adds as `memory_get_peak_usage()` after `memory_reset_peak_usage()`
minus the usage before the call (checked on a 1 MiB string: 1,028 KiB). That includes the returned
array and the C list of found coroutines.

| Case (S7.md section 11) | wall | peak added | found |
|---|---|---|---|
| 10 000 coroutines in pairs awaiting each other | 24.0-25.6 ms | 4.9 MiB | 10 000 |
| the same, each stack also reaching one array of 1 000 000 objects | 238.4-261.8 ms | 76.2 MiB | 10 000 |
| 10 000 coroutines on `delay(60000)` (no candidate) | 0.5 ms | 0.1 KiB | 0 |

The shared graph is walked once per pass, not once per stack: a global keeps it live, so the count
and the spread each walk it, about 240 ns and 80 B per node in all. Without a
candidate the call stops after the registry scan. The automatic run pays the same walk at the idle
point, at most once per `true_async.partial_deadlock_interval` (backing off to 64 times it while it
finds nothing new; an interval of 0 walks at every idle point).
## 2026-10-07, S5.5: await_*, map chains and waiters of one token against the reference

**Builds.** Release, ZTS, `-O2`, gcc 13.3, configured as in S3.11, run with `-n`, no opcache; one
count per side, the child pinned to CPU 3 of 4.

- `ref`: the fork core `863f6dd90cf` with `ext/async` `1fdacf8` (`tests/lists/REFERENCE`) built in.
- `ours`: the pinned core `1ee473ff67b` (`async-core-io-2026-10-06`) and our extension as a
  `phpize` module at S5.4 (`1be2667`) with the S5.5 tests, the test hooks off.

**Known answers** (S3.11's check of the tools): B1 counts 2,701.8 instructions and 1.020 allocations
per spawn on `ours`, `B1-known` (B1 with one known allocation per spawn) 3,071.8 and 2.020: the
counter sees that allocation exactly.

**Benchmarks** (`bench/b9.php`-`b11.php`, the runner's `B9`-`B11`): B9-N awaits `await_all` over N
pending Futures that a spawned coroutine completes; B10 is a `map` chain of depth 1 000 or of fan-out
1 000 on one source; B11-N parks N coroutines each on `await(new Future($state), $token)` with one
shared token Future and wakes each by its own state (S5.md section 9).

| Bench | per | ref: instr / allocs | ours: instr / allocs | ours / ref |
|---|---|---|---|---|
| B9-1 | wait | 8,772.1 / 20 | 8,652.0 / 19 | 0.99 |
| B9-2 | wait | 12,001.4 / 25 | 11,501.0 / 22 | 0.96 |
| B9-8 | wait | 32,049.9 / 61 | 28,612.0 / 40 | 0.89 |
| B9-100 | wait | 342,250.6 / 613 | 299,466.0 / 316 | 0.87 |
| B9-10000 | wait | 34,609,909.6 / 60,013 | 30,791,554.0 / 30,016 | 0.89 |
| B10-depth | link | 3,080.3 / 6.02 | 1,954.4 / 3.018 | 0.63 |
| B10-fanout | link | 3,644.7 / 5.02 | 2,912.3 / 2.019 | 0.80 |
| B11-1000 | waiter | 13,888.9 / 9.008 | 7,955.5 / 5.004 | 0.57 |
| B11-10000 | waiter | 65,673.0 / 9.001 | 21,974.9 / 6.0 | 0.33 |

Page faults and system calls per operation are equal on both sides (B11: 2.5 faults and 0.5 system
calls at N = 10 000, the run's heap growth). Wall times were not taken.

**K and the heap threshold** (S3.md section 12): `await_*` keeps its context and record chunk on the
heap at every N, with no inline records past the waker's two, and costs fewer instructions and
allocations than the reference from N = 1; an inline K would save at most the two allocations of a
wait for one or two items. Not done.

**Fan-in to one token** (B11): from N = 1 000 to 10 000 the cost per waiter grows 2.76 times on
`ours` and 4.73 times on `ref`, under S3.md's threefold limit for "O(N^2) to fix", but with a linear
part of about 1.56 instructions per waiter already parked. Inferred from the code, not profiled:
removing a waiter's record from the token's vector searches it (`async_callbacks_remove`), the
case S3.md section 12 keeps a record index for. Left as is: under the
limit and a third of the reference's cost.

## 2026-10-06, S4.6: the Ring's lateness and the timer heap

**Builds.** As in the S4.4 entry below (`pocs-dbg`, debug ZTS, 4 CPUs); wall clock. The machine was
noisy this day: the same build read 46-64 ms and 85-290 ms medians on the S4.4 code in two series,
so each comparison below alternates its sides in one series.

**The cause S4.4 inferred is wrong.** The core instrumented with temporary counters (reverted), N =
10 000 `delay(200)` on the Ring, three runs: the backlog and waiting walks of `php_io_ring_expire`
took 0 steps in every run; `ready` held up to 330-5 500 completions, and `php_io_ring_deliver`'s
memmove moved 1-16 M pointers per run. The reactor's wait batched (64 completions per call, a
temporary patch) gave 53-81 ms against 46-64 ms unbatched. A C probe outside PHP, 10 000 absolute
`IORING_OP_TIMEOUT`s 20 us apart with W us of busy work after each completion, two runs each, against
the same deadlines in a sorted array with one `epoll_pwait2` until the nearest:

| W | kernel timeouts: median / max | one wait until the nearest: median / max |
|---|---|---|
| 0 | 0.012-0.052 / 0.8-2.0 ms | 0.039-0.040 / 0.7-1.3 ms |
| 10 us | 5.6-76.7 / 11.5-87.2 ms | 0.032-0.037 / 0.6-2.6 ms |
| 15 us | 5.5-15.8 / 12.2-25.3 ms | 0.035-0.036 / 0.7-1.0 ms |

Inferred, not isolated (the probe changes the wait, the timer source and the batch at once): each
fired kernel timeout costs a busy process a few microseconds, and a burst falls behind.

**After: the reactor's timer heap** (`dev/plans/S4.md` 3.5, as built in S4.6), N = 10 000, eight runs
each, the sides alternated:

| Queue | median | max |
|---|---|---|
| the Ring | 0.018-0.047 ms | 0.6-44.8 ms |
| the Poll queue | 0.040-0.148 ms, one run 25.9 ms | 0.7-50.9 ms |

The Ring went from 46-64 ms (the S4.4 code, the same day, four quiet runs) to the Poll queue's level.
Not measured on a release build.

## 2026-10-06, S4.4: delay() and the S4 lanes

**Builds.** The debug ZTS core of the lanes (`pocs-dbg`, the pinned core `9531d5b0b1f`, now with
`--with-zlib`), our extension with the test hooks; 4 CPUs, wall clock and `getrusage()` of the child,
three runs each unless said. Not a release build: these are bounds, not instruction counts.

**`delay(1000)`** (the stage's Done when: under 50 ms of user CPU). A script of one `delay(1000)`
against an empty script, both with the extension loaded:

| Script | wall | user CPU | system CPU |
|---|---|---|---|
| `delay(1000)`, the Ring | 1.023-1.037 s | 0.0-11.8 ms | 7.8-20.3 ms |
| `delay(1000)`, the Poll queue | 1.025-1.050 s | 16.0-25.0 ms | 0.0-10.6 ms |
| empty script | 0.022-0.026 s | 13.0-18.9 ms | 3.7-4.4 ms |

The wait costs no user CPU beyond the process's start: the scheduler blocks in the queue.

**A lane with `--jobs` above the core count** (PLAN, "Timer tests wait on the clock"): `pocs-dbg`,
511 tests, one run each: `--jobs 4` 10.3 s, `--jobs 8` 9.6 s, `--jobs 16` 9.6 s. More jobs than CPUs
gain 7 %: the timer tests are a small share of the lane. At `--jobs 8` `reactor/019` failed once and
passed on the retry: its main coroutine woke from `delay(1)` in its own tick (U2) before the two
coroutines it had spawned ran, so they were cancelled unrun (4 of 300 runs under 6 busy loops).
The test now yields with `suspend()` instead; the reactor tests passed 15 times in a row under 4 busy
loops at `--jobs 16`.

**N waiters of one deadline** (S3.md section 12, D26 row): N coroutines each `delay(200)`, spawned in
one loop; lateness is the wake time minus the coroutine's own `delay()` call time minus 200 ms, one
run each, at N = 10 000 four on the Ring and two on the Poll queue (ranges):

| N | the Ring: median / max | the Poll queue: median / max |
|---|---|---|
| 250 | 1.7 / 2.5 ms | 0.04 / 0.5 ms |
| 1 000 | 2.6 / 7.1 ms | 0.06 / 2.5 ms |
| 3 000 | 19.1 / 26.9 ms | 0.04 / 0.9 ms |
| 10 000 | 38.7-65.8 / 62.8-90.0 ms | 0.09-0.13 / 2.7-12.3 ms |

The Ring grows faster than N; the Poll queue, with its timer heap, stays flat. Inferred from the
code, not profiled, and wrong (the S4.6 entry above): Timer ops past the Ring's submission entries
wait in its backlog, and every `wait()` walks the whole backlog and the waiting list for expired
deadlines (`main/io/php_io_ring.c:1811-1843`), while the reactor calls `wait()` once per completion
(S4.md 3.2). The immediate unlink (D26) has no variant to compare. Left for S4.6: a timer heap of
the reactor's own with one Timer op for its nearest deadline, as libuv keeps (TrueAsync), or a core
change to the Ring's backlog.

## 2026-10-06, S4.3: B1 with the reactor's check in the tick

**Builds.** As in the S4.2 entry: release, ZTS, `-O2`, gcc 13.3, the pinned core `9531d5b0b1f`, our
extension as a `phpize` module, the test hooks off; `before` at `81dfeeb` (S4.2) and `after` with
S4.3's change. One count per side.

| Bench | N | before | after | after / before |
|---|---|---|---|---|
| B1 | 100 000 | 2,624.7 / 1.020 / 0 / 0 | 2,632.8 / 1.020 / 0 / 0 | 1.003 |

Per operation: instructions / allocations / page faults / system calls. B1 creates no queue, so what
it pays is the tick's test that no queue exists. The first version counted 2,662.0 (1.014): reading
`ASYNC_G(reactor)` after the microtasks' loop costs a shared module under ZTS a `__tls_get_addr()`
call, and the inlined coarse clock put a stack canary into the tick. The pointer is now read with
the microtasks' at the tick's start and the throttle lives out of line in `reactor.c`
(`async_reactor_poll_due()`): 8.1 instructions per spawn remain.

## 2026-10-05, S4.2: B1 with the waker of two records and a block

**Builds.** Release, ZTS, `-O2`, gcc 13.3, the pinned core `async-core-io-2026-10-05-4` `9531d5b0b1f`
configured as in S3.11; our extension as a `phpize` module (`shared`), `before` at `7a9c99a` (S4.1)
and `after` with S4.2's change, the test hooks off on both. Machine and metric as in S3.11 (its
known answers not run again); one count per side.

| Bench | N | before | after | after / before |
|---|---|---|---|---|
| B1 | 100 000 | 2,617.2 / 1.020 / 0 / 0 | 2,624.7 / 1.020 / 0 / 0 | 1.003 |

Per operation: instructions / allocations / page faults / system calls. The first version of the
change counted 2,646.1 (1.011): the finish called the abort and the end of the wait out of line for
every coroutine. With the inline test that nothing is linked (`async_wait_is_empty`, `coroutine.h`)
before both and before every unlink, 7.5 instructions per spawn remain, no allocation more. The
coroutine's allocation grows from 408 to 456 B, from the 448 B to the 512 B bin. B1 stays below the
reference's 3,128.9 of S3.11 (that count was taken on the core of that day).

## 2026-10-03, S3.11: B0-B5 against the reference

**Builds.** Release, ZTS, `-O2`, gcc 13.3, `./configure --enable-zts --disable-all --disable-cgi
--disable-phpdbg` on both sides, run with `-n`, no opcache.

- `ref`: the reference, fork core `863f6dd90cf` with `ext/async` `1fdacf8575b` built into the core
  tree. `ext/async` cannot be built with `phpize`: it never defines `_tsrm_ls_cache`.
- `static`: our extension built into the RFC core tree (`async-core-io-2026-10-02-2` `82df2fc6ccc`
  plus `--enable-test-scheduler`), the configure header check removed in that copy only.
- `shared`: our extension as it ships, a `phpize` module loaded by the same RFC core.

**Machine.** Cloud container, 4 vCPU Intel Xeon @ 2.10 GHz under a hypervisor, Linux 6.18. Every run
pinned to one CPU with `taskset`.

**Metric.** The container exposes no hardware counters (`perf_event_open` gives ENOENT for
`instructions`), so `perf stat -e instructions:u` is replaced by cachegrind's instruction count
(`valgrind --tool=cachegrind --cache-sim=no`, valgrind 3.22), which counts the same user-space
instructions exactly. Per operation: (I(2N) - I(N)) / N. Allocations per operation the same way
with `bench/alloc_count.so` (`LD_PRELOAD`, `USE_ZEND_ALLOC=0`); page faults from `getrusage`;
system calls from `strace -c -f`. Wall time: the sides alternated, 30 runs each at N, the median
with its bootstrap 95 % interval.

**Known answers**, run first:

| Check | Expected | Measured |
|---|---|---|
| cachegrind on a loop of 2 instructions, 10^6 more iterations | 2 per iteration | 2.000 |
| allocation counter on a C loop of `malloc`, 1000 more iterations | 1 per iteration | 1.000 |
| B1 with one `new stdClass` per spawn (`B1-known`, shared, at 89e4fe7) | +1 allocation | +1.000 allocation, +368 instructions |
| B1 with `efree(emalloc(64))` in spawn (`bench/variants/known-answer.patch`, shared, at 89e4fe7) | +1 allocation | +1.000 allocation, +70.0 instructions |

Two counts of B1 on the same build: 2,645.9 and 2,646.0 instructions per operation.

### D2: the final code against the reference

Per operation: instructions / allocations / page faults / system calls. B0 is the whole run.

| Bench | N | ref | static | shared | shared / ref |
|---|---|---|---|---|---|
| B0 | 1 run | 11,838,486 / 15,180 / 1,106 / 9 | 11,181,314 / 14,672 / 1,065 / 5 | 11,256,715 / 14,689 / 1,072 / 5 | 0.951 |
| B1 | 100 000 | 3,128.9 / 2.030 / 0 / 0 | 2,197.1 / 1.020 / 0 / 0 | 2,517.0 / 1.020 / 0 / 0 | 0.804 |
| B1 unbatched | 100 000 | 378,027.4 / 2.000 / 0.210 / 0 | 2,237.1 / 1.000 / 0.130 / 0 | 2,554.1 / 1.000 / 0.130 / 0 | 0.007 |
| B2 | 400 000 | 639.0 / 0 / 0 / 0 | 444.0 / 0 / 0 / 0 | 622.0 / 0 / 0 / 0 | 0.973 |
| B3 | 400 000 | 505.0 / 0 / 0 / 0 | 317.0 / 0 / 0 / 0 | 477.0 / 0 / 0 / 0 | 0.945 |
| B4, depth 100 | 100 000 | 5,815.3 / 6.980 / 4.850 / 0.970 | 3,003.1 / 2.020 / 0 / 0 | 3,620.2 / 2.020 / 0 / 0 | 0.623 |
| B4, depth 10 000 | 100 000 | 30,547.3 / 7.000 / 4.999 / 1.000 | 3,795.6 / 3.796 / 4.489 / 0.898 | 4,457.8 / 3.796 / 4.489 / 0.898 | 0.146 |
| B5 | 100 000 | 8,246.6 / 5.012 / 2.505 / 0.501 | 3,180.5 / 2.005 / 0 / 0 | 3,789.3 / 2.005 / 0 / 0 | 0.459 |

D2 holds: on B1-B5 both builds run fewer instructions per operation than the reference, and
allocate no more. The verdict compares the two systems, cores included: the control below could not
separate the cores. The reference's unbatched B1 is its quadratic scope bookkeeping
(`scope.c:58-74`); its B4 at depth 10 000 grows the same way.

Wall time, median and 95 % interval, ms:

| Bench | ref | static | shared |
|---|---|---|---|
| B0 | 7.1 (6.8-7.7) | 6.3 (6.2-6.6) | 6.6 (6.5-6.8) |
| B1 | 39.5 (38.3-42.1) | 24.4 (23.4-25.4) | 28.4 (27.5-29.9) |
| B1 unbatched | 951.7 (930.5-974.3) | 72.0 (70.7-74.7) | 76.2 (74.6-79.0) |
| B2 | 47.2 (46.2-47.9) | 30.4 (30.0-30.7) | 38.2 (37.7-38.7) |
| B3 | 25.5 (24.4-26.9) | 15.3 (15.2-15.8) | 20.3 (20.0-20.7) |
| B4, depth 100 | 1596.2 (1568.0-1630.5) | 39.5 (39.0-40.3) | 45.6 (44.9-47.3) |
| B4, depth 10 000 | 2552.7 (2532.6-2599.0) | 2082.5 (2049.9-2134.3) | 2096.1 (2051.1-2124.6) |
| B5 | 1133.1 (1110.6-1159.6) | 100.1 (94.2-102.3) | 109.8 (106.4-111.1) |

B4 at depth 10 000 goes past the pool on both sides; its wall time is mostly the kernel creating
and unmapping stacks (about 4.5 page faults and one system call per link), which instructions do
not count. The rows B0-B4 at depth 100 were taken with the pool floor at 256 (B0 also with its 2 KiB
pool buffer), B4 at depth 10 000 and B5 again at 1024; the code measured after the review below
counted the same within 0.05 %. Wall times are compared within one table only: the same build read
69.3 ms and 76.2 ms on the unbatched B1 in two tables.

**D2's control.** `ext/test_scheduler` has no counterpart on the fork core, so the control gives
the RFC core's own floor, not a difference between the cores. Its `suspend()` parks until a
`resume()`, so `bench/control/b2.php` resumes the other player before each park. RFC core
without our extension: B2 792.0 instructions per operation (with the extra `resume()`), B4 at
depth 100 4,367.6 instructions, 8.060 allocations, 1.010 page faults and 1.010 system calls per
link (no context pool).

### Items of the section 12 table

Each A/B pair differs in one change and runs on the shared build.

| Item | Base | Variant | B1 | B1 unbatched | B4, depth 100 | B4, depth 10 000 | B5 | Outcome |
|---|---|---|---|---|---|---|---|---|
| O6, `zend_fcall_t` inline | 89e4fe7 | `spawn_fcall` | 2,646.0 / 2.020 → 2,518.0 / 1.020 (-4.8 %) | 2,685.8 / 2.000 → 2,555.1 / 1.000 (-4.9 %), faults 0.124 → 0.130 | | | | taken (D17) |
| Every context's first VM page on its C stack | 89e4fe7 | as TrueAsync's `fiber_entry` | 2,646.0 (=) | | 4,950.5 / 5.940 → 4,693.4 / 4.970 (-5.2 %) | 5,007.1 / 5.999 → 4,685.1 / 5.000 (-6.4 %) | 4,552.9 / 4.509 → 4,414.0 / 4.008 (-3.1 %) | taken (a gap, below) |
| Plain heap vector, no inline element | 89e4fe7 | heap array from the first waiter | | | 4,950.5 / 5.940 → 5,044.3 / 6.950 (+1.9 %) | 5,007.1 / 5.999 → 5,090.3 / 7.000 (+1.7 %) | 4,552.9 / 4.509 → 4,527.0 / 4.510 (-0.6 %, allocations above) | inline kept |
| Pool cap 128, no run-queue rule | 89e4fe7 | `pooled >= 128` | | | 4,950.5 → 3,728.3 (-24.7 %) | 5,007.1 → 4,969.2 (-0.8 %) | 4,552.9 / 4.509 → 4,993.2 / 5.625 (+9.7 %) | rejected |
| Pool cap 1024, no run-queue rule | 89e4fe7 | `pooled >= 1024` | | | 3,728.3 (-24.7 %) | 4,853.0 (-3.1 %) | 3,896.4 / 3.006 (-14.4 %) | not taken: the floor below matches it |
| Pool floor 64, TrueAsync's rule | O6 + VM page | floor 64 | | | 4,564.1 / 3.960 → 3,980.9 / 2.760 (-12.8 %) | 4,557.1 → 4,551.2 (-0.1 %) | 4,285.2 → 4,284.3 (0 %) | |
| Pool floor 256, TrueAsync's rule | O6 + VM page | floor 256 | | | 3,621.3 / 2.020 (-20.7 %) | 4,532.6 / 3.949 (-0.5 %) | 4,281.2 (-0.1 %) | |
| Pool floor 1024, TrueAsync's rule | O6 + VM page | floor 1024 | | | 3,621.3 / 2.020 (-20.7 %) | 4,457.9 / 3.796 (-2.2 %) | 3,791.0 / 2.005 (-11.5 %) | taken (D23 changed) |

Wall time of the same pairs, ms, median and 95 % interval:

| Pair | Bench | Base | Variant |
|---|---|---|---|
| O6 | B1 | 27.9 (27.2-28.2) | 26.8 (26.3-27.0) |
| O6 | B1 unbatched | 65.5 (64.1-66.5) | 69.3 (68.7-70.8) |
| floor 4 / 256 / 1024 | B4, depth 100 | 1519.7 (1508.1-1567.1) | 45.5 (44.1-50.1) / 44.7 (43.0-47.8) |
| floor 4 / 256 / 1024 | B4, depth 10 000 | 2291.8 (2203.1-2340.6) | 2256.1 (2200.6-2296.0) / 2144.2 (2091.1-2199.2) |
| floor 4 / 256 / 1024 | B5 | 1109.1 (1068.7-1131.6) | 1106.6 (1048.5-1138.3) / 104.0 (99.1-107.5) |

O6's wall time points the other way on the unbatched run: 5.8 % slower, where 100 000 coroutines
live at once and each touches 16 B more fresh memory (page faults +5 %). D17's condition is the
instruction count, which falls by 4.9 %; the wall-time result is recorded here and in the
decision.

**The VM stack gap.** `fiber_entry` started every context with the core's
`zend_fiber_vm_stack_start`, which allocates the first 16 KiB VM page from the request's memory;
TrueAsync's `fiber_entry` (`scheduler.c:1796-1829`) puts it on the context's C stack, as our
scheduler coroutine already did. With 10 000 coroutines parked in a chain, `memory_get_usage(true)`
read 168 MiB (the reference 14 MiB), and B4 at depth 10 000 ended with "Allowed memory size
exhausted" at the default limit. After the change: 8 MiB. Test `scheduler/055`.

**Memory kept by the pool** after one chain of the given depth has finished (`/proc/self/status`
VmRSS growth; `memory_get_usage()`), shared build:

| Floor | depth 100 | depth 1 000 | depth 10 000 |
|---|---|---|---|
| 4 | +428 KiB, 494 KiB | +992 KiB, 526 KiB | +6,764 KiB, 1,246 KiB |
| 256 | +2,368 KiB, 508 KiB | +6,036 KiB, 561 KiB | +11,808 KiB, 1,281 KiB |
| 1024 | +2,368 KiB, 514 KiB | +20,936 KiB, 658 KiB | +27,172 KiB, 1,389 KiB |

A pooled context keeps about 20 KiB of its stack resident and about 0.14 KiB of the request's
memory, until the scheduler ends: in a long-running script that had one burst of 1000 coroutines,
about 20 MiB of resident stacks and 1000 more mappings stay. A pooled context keeps the
`fiber.stack_size` it was created with, as in TrueAsync.

**ZTS TLS cost** (decides moving hot state into the RFC core, D31). The shared build runs more
instructions than the static one on every benchmark: +320 per B1 operation, +178 per suspend in B2,
+160 in B3, +617 per B4 link, +609 per B5 waiter. callgrind on B2: 9 calls of `__tls_get_addr` per
suspend, 108 instructions inside it. The extension reaches its own globals and the core's (`EG()`,
`ZEND_ASYNC_*`) through the same per-module `_tsrm_ls_cache`, which the core declares without a TLS
model for modules (`TSRM.h`, `TSRMLS_CACHE_DEFINE`); moving state into the core removes none of these
calls. D31 unchanged.

**Not measured in S3.11.**
- U5 `zend_try` per branch and the pool-miss branch: U5 has no `zend_try` since S3.7 (the record
  lives in the waker); the wait path has none to count.
- Stale stack records: the S3 list on the ASAN build with
  `ASAN_OPTIONS=detect_stack_use_after_return=1`: 236 PASS, 14 SKIP, no report. Since S3.7 no
  record lives on a stack.
- The lazy suspend location, the folded waker status and the immediate unlink (D26) have no
  variant to compare; the suspend as a whole is in B2 and B3 above.
- The S4, S5 and S9 rows of the table wait for their stages.

### After the review (Critic, Sage)

- A context's stack is `fiber.stack_size` plus the 16 KiB VM page `fiber_entry` keeps on it: with
  `fiber.stack_size=16K` a coroutine crashed in its first frame (TrueAsync too), now it throws the
  core's stack-limit Error as a Fiber does. B4 at depth 10 000, the one benchmark that creates a
  context per link: 4,457.9 -> 4,457.8 instructions per link, the same allocations, faults and
  system calls.
- The pool buffer starts empty and never shrinks, as the run queue: at 1024 slots from request
  startup it took 8 KiB in every request and held only 1023. B0 11,265,169 -> 11,256,715
  instructions (shared); B5 3,791.0 -> 3,789.3.
- O6 makes every coroutine object 448 B: a Fiber's coroutine, main and the scheduler carry the
  unused 104 B block (+128 B each); no Fiber benchmark is in the plan.
