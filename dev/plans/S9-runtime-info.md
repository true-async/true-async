# S9 notes, layer 8: runtime information

Design note of the eighth S9 layer, 2026-10-13. The layer adds five read-only probes of the process and the
scheduler: `Async\available_parallelism()`, `Async\loadavg()`, `Async\CpuSnapshot`, `Async\cpu_usage()` and
`Async\runtime_stats()`. Names, signatures and array keys come from TrueAsync (`ext/async` at
`tests/lists/REFERENCE`, `1fdacf8`, file names alone below; libuv below `uv:` with its version) and its documentation
(`true-async.github.io` `en/docs/reference/available-parallelism.md`, `loadavg.md`, `cpu-snapshot.md`,
`cpu-usage.md`, below `site:`; `runtime_stats()` has no page). `true-async-doc` has no page for any of them. Behaviour
marked "probed" was run on 2026-10-13 on a debug NTS build of the fork `true-async/php-src` `863f6dd90cf` with that
`ext/async` and the system libuv 1.48.0 (scripts `r1_*.php`-`r7_*.php` in `/mnt/project-files/s9/probes/s9.runtime/`);
the same build passes the 6 tests of the layer. This extension has none of the five yet, so nothing here was run on
ours.

**Why this layer is the eighth.** `dev/PLAN.md` orders the S9 layers scope, context, channels, task groups, pools,
iterators, FileSystemWatcher, runtime information; the layer came from S3's exclusions (DECISIONS 2026-10-12 S14.4).

**What is new here.** Nothing waits: every function returns at once, runs no user code and suspends nothing. TrueAsync
asks libuv for the CPU count through the core's reactor slot (`async.c:703-713`,
`zend_async_available_parallelism_fn`); our core has no such slot and this extension has no libuv
(`dev/PRINCIPLES.md` P1.2), so the layer reads the operating system itself. No core change, no reactor change.

## 1. What the layer implements

| Kind | In this layer |
|---|---|
| Functions | `available_parallelism(): int`, `loadavg(): ?array`, `cpu_usage(): array`, `runtime_stats(): array` in `true_async.stub.php`, as `async.stub.php:72-180` |
| Class | `final class CpuSnapshot` with six `public readonly int` properties (`wallNs`, `processUserNs`, `processSystemNs`, `systemIdleNs`, `systemBusyNs`, `cpuCount`), a private constructor and `static now(): CpuSnapshot`, `@strict-properties`, `@not-serializable` |
| Source | `src/runtime_info.c`: the platform readers (Linux and Windows, `sysconf()` elsewhere) and the five entry points |
| State | the previous snapshot of `cpu_usage()` in the request's globals (section 6, item 1) |

What a user gets, from `site:cpu-usage.md`:

```php
spawn(function () {
    cpu_usage();                                  // the first call stores a snapshot and returns zeros
    while (true) {
        delay(1000);
        $u = cpu_usage();
        printf("proc %.2f cores (%.1f%%), system %.1f%%\n",
            $u['process_cores'], $u['process_percent'], $u['system_percent']);
    }
});
```

## 2. The behaviour, as TrueAsync's

From `cpu_info.c`, `async.c:703-775` and the probes; every row holds for this layer unless section 6 says otherwise.

| Case | What happens |
|---|---|
| `available_parallelism()` | the CPUs this process may run on, at least 1, read at every call (`uv:1.48 core.c:1868-1905`: the affinity mask's count on Linux, `sysconf(_SC_NPROCESSORS_ONLN)` elsewhere; probed `r4`: 4, and 1 under `taskset -c 0`) |
| `loadavg()` | `[1min, 5min, 15min]` floats from `getloadavg()`; `null` on Windows or when the call fails (`cpu_info.c:270-286`) |
| `CpuSnapshot::now()` | monotonic wall time; the process's user and kernel CPU time over all its threads (`getrusage(RUSAGE_SELF)`, `GetProcessTimes()`); the host's idle (`idle + iowait`) and busy (`user + nice + system + irq + softirq + steal`) time over all CPUs from the first line of `/proc/stat` (`GetSystemTimes()` on Windows); the online CPUs of the host (`cpu_info.c:149-217`) |
| Unreadable host counters | `/proc/stat` missing or unparsable: `systemIdleNs` and `systemBusyNs` are 0, silently (`cpu_info.c:170-185`) |
| `CpuSnapshot` | `new` is an `Error` (private constructor); the properties are readonly; no dynamic property; `clone` gives an equal object; `serialize()` and `unserialize()` throw `Exception` "Serialization of 'Async\CpuSnapshot' is not allowed"; `json_encode()` and `var_dump()` show the six fields; `newInstanceWithoutConstructor()` gives an object whose properties are uninitialised (probed `r2`) |
| `cpu_usage()` | the first call stores a snapshot and returns zeros; each later call returns the delta against the stored one and replaces it: `process_cores` = process CPU time / wall time, `process_percent` = `process_cores / cpu_count * 100`, `system_percent` = busy / (busy + idle) of the host, `cpu_count`, `interval_sec`, `loadavg` (`cpu_info.c:288-355`; probed `r3`: 0.2 s of a busy loop gives `process_cores` 0.999, `process_percent` 25.0 on 4 CPUs) |
| `runtime_stats()` | ten integer keys read from the scheduler, no allocation beyond the array (`async.c:716-775`); the values below |
| Arity | each function and `now()` rejects an argument with `ArgumentCountError` (the 6 tests) |

`runtime_stats()`, probed `r1` and `r7`:

| Key | TrueAsync's stub says | What it counts |
|---|---|---|
| `coroutines_total` | coroutines in the coroutine map | every unfinished coroutine in the registry, main and zombies included (0 at the top level before the first spawn, 4 after three spawns) |
| `coroutines_active` | running and queued coroutines | every unfinished coroutine that is not a zombie, suspended ones included (`r1`: 5 inside a coroutine while three sleep; `r7`: 2 before a safe disposal, 1 after) |
| `microtasks_queue` | pending microtasks | the microtask queue's length |
| `coroutine_queue` | coroutines waiting for a free fiber | the run queue's length (3 after three spawns) |
| `resumed_queue` | coroutines resumed by I/O, pending pickup | the list of woken coroutines whose wait events are cleaned at the next tick (`scheduler.c:426-436`) |
| `fiber_pool_count` | fibers parked in the pool | the same |
| `fiber_pool_capacity` | the pool's capacity | the ring buffer's usable slots (3, then 7), not a limit |
| `fiber_pool_min` | `ASYNC_FIBER_POOL_SIZE` | the same, 4 there |
| `fiber_stack_size` | `EG(fiber_stack_size)` | the same, 2 MiB |
| `fiber_pool_virtual_bytes` | count times stack size | the same |

Behaviour that looks like a defect, each a departure in section 6:

- **`cpu_usage()` keeps one previous snapshot for the whole process** (`cpu_info.c:49-58`, under a mutex in ZTS) and
  every request's RSHUTDOWN clears it (`async.c:1836-1841`). Under NTS this is one snapshot per request, so
  `site:cpu-usage.md`'s "global to the process" holds for no FPM worker; under ZTS two threads' requests take each
  other's snapshot (each gets the interval since the other's call) and the end of any request clears the other's.
- **`available_parallelism()` honours cgroup quotas only with libuv 1.49 or later.** The reference build's libuv 1.48
  reads the affinity mask alone (`uv:1.48 core.c:1879-1882`), so `site:available-parallelism.md`'s "in Docker with
  `--cpus=2` → 2" holds only on a newer libuv. libuv 1.51 reads the quota (`uv:1.51 core.c:2047-2055`) but floors it,
  so a quota of 1.5 CPUs gives 1 and one of 0.5 is ignored (all CPUs); and on cgroup v2 it keeps the quota of the
  last level it reads, the root's side, not the smallest along the path: `min_quota` is computed and never stored
  (`uv:1.51 linux.c:2372-2374`).
- **`process_percent` divides by the host's online CPUs**, not by the CPUs the process may use. The site contradicts
  itself: its table (`site:cpu-usage.md:38`) and the stub (`async.stub.php:157`) say "share of the total machine
  capacity", as the code does, while its note below (`site:cpu-usage.md:44-46`) say the key accounts for affinity and
  cgroup throttling, and its backpressure example compares it with 90. Probed `r6`: a process confined to 1 CPU of 4
  and keeping it busy reads 24.8 %. Question 1.
- **`runtime_stats()`'s own descriptions** of `coroutines_active`, `coroutine_queue` and `fiber_pool_capacity` do not
  match what it counts (the table above).

## 3. The design

**`src/runtime_info.c`** holds the five entry points and three readers per platform, as `cpu_info.c`:

- **Process CPU**: `getrusage(RUSAGE_SELF)`; Windows `GetProcessTimes()`.
- **Host CPU**: the first line of `/proc/stat`, read with one `read()` into a stack buffer, ticks converted by
  `sysconf(_SC_CLK_TCK)`; Windows `GetSystemTimes()` (its kernel time includes idle, subtracted as TrueAsync does).
  Elsewhere both are 0, as TrueAsync's POSIX path without `/proc`.
- **Wall time**: `zend_hrtime()`, the clock of `hrtime()`, instead of TrueAsync's own `clock_gettime()` and
  `QueryPerformanceCounter()` code.
- **Online CPUs** (`cpuCount`, `cpu_count`): `sysconf(_SC_NPROCESSORS_ONLN)`; Windows
  `GetActiveProcessorCount(ALL_PROCESSOR_GROUPS)`. The host's count, since the host counters it divides are the
  host's.

**`available_parallelism()`**, at every call, as libuv 1.51 with its two defects fixed (section 6, item 2):

1. The affinity count: on Linux `sched_getaffinity()`'s count, `sysconf(_SC_NPROCESSORS_ONLN)` when it fails;
   elsewhere `sysconf(_SC_NPROCESSORS_ONLN)`. On Windows `GetProcessAffinityMask()`'s count while the host has one
   processor group (`GetActiveProcessorGroupCount()` is 1); with more groups, or a mask of 0 (a process whose threads
   span groups), the sum of `GetActiveProcessorCount(group)` over the groups `GetProcessGroupAffinity()` returns (it
   gives group numbers, not masks; Windows keeps no per-group process mask), and when that fails
   `GetActiveProcessorCount(ALL_PROCESSOR_GROUPS)`. An explicit restriction through CPU sets
   (`GetProcessDefaultCpuSetMasks()`, Windows 11 and Server 2022) is counted where the call exists. libuv's Windows code (`uv: win/util.c`) reads the one mask and so
   sees at most 64 CPUs, or 1 when the mask is 0.
2. The quota, as a fraction of CPUs:
   - Linux. The controller is v1's `cpu` when `/proc/self/cgroup` has a line whose comma-separated controller list
     holds `cpu` (a hybrid host keeps the CPU controller in v1), otherwise v2's `0::` line. The line's path is made
     relative to the controller's mount: `/proc/self/mountinfo` names the mount of `cgroup` (v1, with `cpu` in its
     super options) or `cgroup2` (v2), and the mount's root field is stripped from the path, as the JDK and Go do; a
     path outside the mount's root (`/..` inside a cgroup namespace) starts at the mount point. With several
     mounts of the controller, the one whose root field is a prefix of the path is used. From that directory up
     to the mount point, every level's `cpu.max` (v2, "quota period", "max" for none) or `cpu.cfs_quota_us` and
     `cpu.cfs_period_us` (v1, -1 for none) is read, and the smallest `quota / period` is the quota.
   - Windows. The job object the process runs in (`QueryInformationJobObject(NULL,
     JobObjectCpuRateControlInformation)`), when its control is enabled with a hard cap: `CpuRate / 10000` of the
     host's CPUs, as a container started with `--cpus` gets, and with the min-max mode `MaxRate / 10000` of them. A
     weight-based or absent control is no quota. To check against the API reference in S9.49.
   - Elsewhere no quota. A missing or unreadable file, a malformed line, or a path longer than the buffer is no
     quota at that level.
3. `available_parallelism()` is the smaller of the affinity count and the quota rounded up, at least 1: a quota of
   1.5 CPUs gives 2, one of 0.5 gives 1.

The unrounded smaller of the two (a float, at least a tiny positive value) is kept apart as the process's effective
CPUs for question 1's option B. The files are read with `open()` and `read()` into stack buffers. Only `/proc/self`
is a constant: the cgroup mount points come from its `mountinfo`. Under the test hook (below) `$root` is prefixed to
`/proc/self` and to every mount point read from the fixture's `mountinfo`.

**`CpuSnapshot`** is declared in the stub with `@strict-properties` and `@not-serializable`; `now()` fills the six
properties with `zend_update_property()` through the class scope, as `cpu_info.c:231-268`. Clone stays allowed: an
equal immutable object is harmless.

**`cpu_usage()`** takes a snapshot, swaps it with the request's stored one and computes the delta as
`cpu_info.c:288-355`; deltas that go backwards are 0. The stored snapshot is a struct in `ASYNC_G`, zeroed at RINIT;
no mutex.

**`runtime_stats()`** reads our scheduler's state (`src/scheduler.c`), all of it allocated at RINIT
(`async_scheduler_request_startup()`), so no "before the scheduler" branch is needed:

| Key | Ours |
|---|---|
| `coroutines_total` | `zend_hash_num_elements(&ASYNC_G(coroutines))`, the registry, which also holds the core's own coroutines (the collector's, the shutdown destructors') while they run |
| `coroutines_active` | what `scheduler_get_coroutine_count()` (`src/scheduler.c:2283`, `static`, so exported through `src/scheduler.h`) returns: the registry's count minus `ASYNC_G(zombie_coroutines_count)`, which `async_scope_mark_zombie()` raises (`src/scope.c:124`) and a zombie's end lowers (`src/coroutine.c:434`) |
| `microtasks_queue` | `ASYNC_G(microtasks)`'s length |
| `coroutine_queue` | `ASYNC_G(run_queue)`'s length |
| `resumed_queue` | 0: our scheduler cleans a coroutine's wait at its wake and keeps no such list (section 6, item 4) |
| `fiber_pool_count`, `fiber_pool_capacity` | `ASYNC_G(fiber_context_pool)`'s length and its usable slots, `capacity - 1` (`src/internal/circular_buffer.h:19`), as TrueAsync's 3 and 7 |
| `fiber_pool_min` | `ASYNC_FIBER_POOL_SIZE`, 1024 (D23) |
| `fiber_stack_size` | `EG(fiber_stack_size)` |
| `fiber_pool_virtual_bytes` | `fiber_pool_count * (EG(fiber_stack_size) + ZEND_FIBER_VM_STACK_SIZE)`, the size a context reserves here (`src/scheduler.c:134`) |

**Test hook.** `TrueAsync\Test\cpu_limits(?string $root, ?int $affinity = null)` in `src/test_hooks.c`, beside
`fail_io()`: `$root` replaces both production roots, so the fixture holds `$root/proc/self/cgroup`,
`$root/proc/self/mountinfo` and the cgroup tree under `$root` followed by the mount points it names; `$affinity` replaces the affinity
count, so a fixture's result does not depend on the CPUs of the CI machine; `null` restores both. Both live in
`ASYNC_G` and are released in `async_test_hooks_request_shutdown()`, as the other hooks. No CI machine runs in a
cgroup with a CPU quota, and the parsing is the layer's most error-prone code. The Windows job object is not faked:
its test runs a child under a job with a hard cap on `pocs-win`, if the runner lets a process create one.

**Threads.** Every reader is thread-safe (`getrusage()`, `read()` on a fresh descriptor, `sched_getaffinity()`), and
the only state is per request, so ZTS needs no lock.

## 4. Garbage collection and the collector

`CpuSnapshot` holds six integers and no reference, so it reports nothing to PHP's GC. No function holds an object
past its return. The layer adds no wait kind.

## 5. Steps

- S9.49 The layer's block of `tests/lists/S9.txt` (section 8), `src/runtime_info.c`, the stub, the test hook; Linux
  and Windows; own tests for each row of sections 2 and 6 the reference tests leave out.
- S9.50 Layer review: the Critic over S9.49, coverage of the layer's lines, Mull on its diff, 100 fuzz seeds over the
  layer's tests; `true-async-doc` pages for the five with section 6's differences and a page for `runtime_stats()`'s
  keys.
- S9.51 Security pass by `dev/SECURITY.md`: the cgroup and `/proc` reads (path lengths, short and long reads, a file
  without a newline, numbers that overflow, among them the host's nanosecond counters past `ZEND_LONG_MAX` on a host
  with hundreds of CPUs and months of uptime, as TrueAsync), the test hook's absence from release builds, what
  `runtime_stats()` and the host counters expose to a script.

## 6. Departures from TrueAsync

Decided here by the project's rules and recorded in `DECISIONS.md` with S9.48, except question 1.

1. **`cpu_usage()` keeps its previous snapshot per request**, so two requests never share one: under ZTS each
   thread's request measures its own interval. TrueAsync shares one per process and every request's end clears it.
   The doc page says "per request", and also what stays shared: the process CPU time is the whole process's
   (`getrusage(RUSAGE_SELF)` sums every thread, so a ZTS request sees the server's CPU), and coroutines of one
   request share its snapshot, each call taking the interval since the previous call of any of them.
2. **`available_parallelism()` honours CPU quotas on every build**: cgroup v1 and v2 on Linux with their mount roots,
   the smallest quota along the path, and a job object's hard cap on Windows; rounded up; on Windows with every
   processor group. TrueAsync's honours cgroup quotas only with libuv 1.49 or later, floors them, on 1.51 keeps the
   root side's quota of a v2 path, reads no mount root and no job object, and on Windows sees one processor group.
   Rounded up because a process granted 1.5 CPUs gets more done with 2 workers than with 1, and a 0.5 quota then
   gives 1 instead of every CPU of the host.
3. **`coroutines_active` counts what TrueAsync counts** (unfinished and not a zombie); the doc page says so rather than
   "running and queued".
4. **`resumed_queue` is always 0** and stays in the array, so code written for TrueAsync reads every key it expects.
5. **`fiber_pool_min` is 1024** (D23) and `fiber_pool_virtual_bytes` counts the VM stack a context reserves with its C
   stack.

Items 1 and 2 follow Edmond's rule of 2026-10-13 (iterate): TrueAsync behaviour that looks like a bug is not copied.
Items 3-5 keep TrueAsync's keys over our own names (Edmond, "check TrueAsync's name first").

TrueAsync's documentation, to correct in the doc pages of S9.50: `site:cpu-usage.md` "global to the process"
(item 1); `site:available-parallelism.md` "Backend: `uv_available_parallelism()`" and its Docker example, true only on
libuv 1.49 or later (item 2).

## 7. Questions for Edmond

Asked one at a time, each with the PHP code and what the programmer sees. The plan itself needs his agreement before
S9.49 (`dev/PLAN.md` S9).

1. **What `process_percent` is a share of.** A process confined to 1 CPU of a 4-CPU host keeps it busy (`r6`, run
   under `taskset -c 0`):

   ```php
   cpu_usage();
   $t = microtime(true); while (microtime(true) - $t < 0.5) {}
   $u = cpu_usage();
   printf("process_cores=%.2f process_percent=%.1f\n", $u['process_cores'], $u['process_percent']);
   ```

   (A) as TrueAsync's code, stub and site table, a share of the host's CPUs: `process_cores=0.99
   process_percent=24.8`; a backpressure check `process_percent < 90` never trips in a container. (B) as the site's
   note promises, a share of the CPUs the process may use, the unrounded smaller of the affinity count and the quota
   (section 3): `process_cores=0.99 process_percent=99.0`, and under a 0.5-CPU quota a saturated process also reads
   about 100, not 50. `cpu_count` stays the host's, as `system_percent` needs. Under a quota the kernel lets several
   threads run past it for part of a 100 ms period, and a process on 1 CPU reads jitter such as 1.01 cores, so B
   caps `process_percent` at 100, keeping the site's `0..100` and TrueAsync's `cpu_usage_basic`, which fails above
   100.5; `process_cores` stays uncapped. Recommended: B, since the site sells the key for backpressure.

## 8. Tests and measurements

**List** `tests/lists/S9.txt`, a block for layer 8, frozen in S9.49: the 6 reference tests of `tests/common`
(`available_parallelism`, `cpu_loadavg`, `cpu_snapshot_basic`, `cpu_snapshot_invariants`, `cpu_usage_basic`,
`runtime_stats`), each passing on the reference build above (2026-10-13); they leave `tests/lists/S3.excluded`.
`cpu_usage_basic` burns CPU in a loop of 5 000 000 additions and checks only that the result is in range, so it
depends on no clock.

**Own tests** (S9.49), one case each: the affinity mask (`taskset -c 0` through `proc_open()`, skipped without
`taskset`); through the hook, cgroup v2 with a quota at a middle level and none at the leaf, a fractional quota,
"max", a mount root stripped from the path, a path outside the mount's root, v1, a hybrid host (v1 `cpu` beside a
`0::` line), a missing file; on `pocs-win` a child under a job object with a hard cap; `runtime_stats()` after three spawns (`coroutine_queue` 3), with a zombie
(`coroutines_active` one less than `coroutines_total`), after the coroutines end (`fiber_pool_count`); `cpu_usage()`'s
first call returns zeros in a second request of the same process (two requests to the CLI server);
`CpuSnapshot`'s clone, serialization and uninitialised reflection object as `r2`.

**Measurement**: none. Nothing here is on a hot path; `runtime_stats()` reads counters and allocates one array.

