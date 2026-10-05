# Benchmarks

The results journal: every measurement with its date, builds and outcome. The method and the
benchmarks are `dev/plans/S3.md`, section 12; the scripts are `bench/`, the runner `tools/bench.py`.

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
