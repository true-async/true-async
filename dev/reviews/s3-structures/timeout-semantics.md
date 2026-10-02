# Timeout semantics: per-wait restart or absolute deadline

Recommendation: option B. `Async\timeout(int $ms)` fixes an absolute deadline when it returns
(`now + $ms` on the reactor's monotonic clock), arms the reactor timer only while at least one
operation waits on it, and arms it for the remaining time, so every await that shares one Timeout
shares one budget. No new function is needed: a per-wait budget is written by calling `timeout()`
inside the loop, a total budget by calling it outside. The cost against the reference is one clock
read moved from the arm to `timeout()`; allocations are unchanged. Option C (a second constructor
with the other meaning) is not recommended, and a third variant D (clock starts at the first wait)
is the fallback if the measurement in section 7 shows the creation-time clock read above 3 %.

Status of the evidence: the reference behaviour is read from source at php-async `1fdacf8`, not run
(no PHP build with the extension exists on this machine). The ecosystem facts come from the official
documentation and upstream source fetched on 2026-10-02 (links in section 9). The only measured
number is the cost of one clock read (section 6).

## 1. Reference behaviour at 1fdacf8

`timeout()` (`async.c:777-796`) rejects `$ms <= 0`, then `async_timeout_create(ms, false)`
(`async.c:1638-1679`) allocates the object and a timer event with `async_timeout_ext_t` as extra
space (`async.c:1652-1653`), sets `ZEND_ASYNC_TIMER_F_REFRESH_CLOCK` (`async.c:1662`) and returns.
No timer is started. `libuv_new_timer_event` starts the reactor and runs `uv_timer_init`
(`libuv_reactor.c:1232-1268`).

An await subscribes the Timeout as the cancellation event (`async.c:358-359`). The subscription calls
`libuv_timer_start` (`libuv_reactor.c:1078-1105`), which refreshes the loop clock
(`uv_update_time`, `:1084-1086`) and calls `uv_timer_start` with the full `timeout`
(`:1088-1091`). libuv computes the due time as `loop->time + timeout` (libuv `src/timer.c:78`).
At wake, `zend_async_waker_stop_events` calls `stop` on every event of the waker
(php-src-true-async `Zend/zend_async_API.c:819-832`), so the next await starts the full duration
again. On fire, a one-shot timer closes its event (`libuv_reactor.c:1063-1065`); every later operation
sees `ZEND_ASYNC_EVENT_IS_CLOSED` and throws before waiting (`async_resolve_cancel_token`,
`async_API.c:1253-1281`).

Three properties of this design are not visible from the question:

1. **The restart depends on overlap.** `EVENT_START_PROLOGUE` (`libuv_reactor.c:351-358`) only
   increments `loop_ref_count` when the timer is already running, and `EVENT_STOP_PROLOGUE`
   (`:360-374`) only decrements it while other waiters remain. Coroutine 1 awaits at t=0 and
   coroutine 2 at t=500 ms with the same `timeout(1000)`: both are cancelled at t=1000. If coroutine 1
   had returned at t=400, coroutine 2 would get a fresh 1000 ms, until t=1500. The budget an await
   sees depends on whether another coroutine happened to be waiting at that moment.
2. **A later await on a fired Timeout loses the `TimeoutException`.** The exception is built in the
   notify handler for the waiters present at the fire (`async.c:1622-1636`) and is not stored; the
   timer event has no `replay` handler (no `replay =` assignment in `async.c` or for timers in
   `libuv_reactor.c`). `async_resolve_cancel_token` then throws `OperationCanceledException` with no
   previous (`async_API.c:1265-1278`), and `signal()` rejects with a generic `AsyncCancellation`
   "Signal wait cancelled" (`async.c:1392-1410`). The stub promises a `TimeoutException` as
   `getPrevious()` (`async.stub.php:190-191`). This is a defect today, independent of the option
   chosen; option B makes the case common, so B must fix it (section 5, rule 5).
3. **No periodic Timeout exists.** `async_timeout_create` takes `is_periodic`, but its only caller
   passes `false` (`async.c:790`). The parameter is dead.

### What the RFC text promises

The current `base.rfc` (php-true-async-rfc `6bd8fca`) contains no occurrence of "timeout" at all; its
`await()` takes `?Completable $cancellation` (`base.rfc:105`). `scope.rfc:1134-1158` and
`base-full.rfc:2457-2478` say: "The `timeout` function is similar to `delay`, but it returns an
`Awaitable` object", with one single-use example. `delay()` starts counting at the call. No RFC text
says when the clock of a Timeout starts or what a second await sees. The archived
`arh/bounded_scope.md:107-113` describes `Scope::defineTimeout()` as "a single internal timer, which
starts when `defineTimeout` is called", a deadline from the call. The stub says "Creates a
cancellation token that trips after $ms" (`async.stub.php:183`). None of these texts contradicts B;
the restart of A appears in none of them.

### What the tests rely on

`grep 'timeout('` over `php-async/tests` finds 45 files. Three tests keep a Timeout in a variable:
`await/002-await_timeout.phpt:18`, `common/timeout_class_methods.phpt:23`,
`signal/003-signal_already_cancelled.phpt:14`. Each uses its Timeout for at most one operation. **No
test awaits one Timeout twice**, so no test pins the restart of A.

`signal/003` is titled "already completed cancellation returns rejected Future" and runs
`$t = timeout(1); delay(50); signal(SIGINT, $t)`. Under A the timer is not running during `delay(50)`,
so `signal()` does not take its "already completed" branch (`async.c:1392-1396`); it starts the timer
and the Future is rejected 1 ms later through the notify handler, which yields the expected
`Async\TimeoutException`. The test passes for a reason other than its title. Under B the Timeout is
expired at `signal()`, the branch named in the title runs, and the expected output holds only if the
Timeout replays a `TimeoutException` (defect 2 above).

## 2. How mature ecosystems model a timeout

| Runtime | Object | Clock starts | Reuse across operations | After firing |
|---|---|---|---|---|
| Go `context.WithTimeout` | Context | at the call: `WithDeadline(parent, time.Now().Add(timeout))` | yes, one deadline for all work under the context | `Done` stays closed, `Err()` is `DeadlineExceeded` |
| Go `time.Timer` / `time.After` | channel timer | at the call | `Reset` re-arms; `time.After` per loop iteration is a new timer | one send; Go 1.23: no stale value after `Reset`/`Stop` |
| Go `net.Conn.SetDeadline` | absolute instant | caller-supplied instant | applies to all future and pending I/O | all I/O fails until a new deadline is set |
| .NET `CancellationTokenSource(TimeSpan)` | token source | during the constructor | yes, one token for many operations | stays cancelled; `TryReset` only for a sole owner, only if not cancelled |
| .NET `Task.WaitAsync(TimeSpan)` | none (argument) | at the call | no, per call | the returned task faults with `TimeoutException` |
| Python `asyncio.timeout(delay)` | `Timeout` | at the call: `loop.time() + delay` | no: a second `async with` raises `RuntimeError` | `expired()` is true; `reschedule(when)` moves the deadline |
| Trio `move_on_after` / `move_on_at` | `CancelScope` | at scope entry (since 0.27.0); `move_on_at` takes an absolute deadline | no: "each cancel scope can be used for at most one `with` block" | every checkpoint inside the scope raises `Cancelled` |
| Rust tokio `timeout` / `Sleep` | future | at the call: `Instant::now().checked_add(duration)` | one `Sleep`, pinned, can bound a whole `select!` loop; `reset(Instant)` moves it | `is_elapsed()`; `Elapsed` error |
| Kotlin `withTimeout(ms)` | none (block) | at the call | covers the whole block | `TimeoutCancellationException` |
| Kotlin `select { onTimeout(ms) }` | none (clause argument) | at each `select` registration | no object to reuse: a number per `select` | clause selected |
| Java `CompletableFuture.orTimeout` | the future itself | at the call | per future | completes exceptionally with `TimeoutException` |
| Java `StructuredTaskScope` (JEP 525, JDK 26) | scope configuration | when the scope opens | one timeout for all subtasks and `join()` | scope cancelled, `join()` throws `TimeoutException` |
| JavaScript `AbortSignal.timeout(ms)` | `AbortSignal` | at the call (active time, not elapsed time) | one signal can be passed to several operations | aborted with a `TimeoutError` `DOMException`, permanently |
| Swift `withDeadline` (SE-0526) | none (block) | caller-supplied instant; `withDeadline(in:)` uses `clock.now.advanced(by:)` at the call | nested deadlines compose by minimum | the body is cancelled |

Every runtime that hands out a timeout **object** (Go context, .NET token, asyncio `Timeout`, tokio
`Sleep`, `AbortSignal`) starts its clock at creation, never at each use, and none restarts on reuse.
A per-wait budget appears only where the timeout is a **number** passed to one call (Kotlin
`onTimeout(ms)`, .NET `WaitAsync(TimeSpan)`, Go `time.After` inside a loop). A fired timeout object is
terminal in every runtime.

### Per-runtime notes and known problems

**Go.** `WithTimeout` is defined as `WithDeadline(parent, time.Now().Add(timeout))`; a child deadline is
clamped to the parent's earlier one. `WithDeadlineCause` cancels at once when `time.Until(d) <= 0` and
otherwise arms `time.AfterFunc` at construction, so a context costs a runtime timer even if nobody
waits; the docs require calling `cancel` to release it. `net.Conn` deadlines are absolute and "apply to
all future and pending I/O"; an idle timeout is built "by repeatedly extending the deadline after
successful Read or Write calls". Known problem: `time.After` inside a `for { select }` loop gives an
idle timeout, not a total one, and before Go 1.23 each iteration left a timer that was not collected
until it fired. Go 1.23 made unreferenced timers collectable and timer channels unbuffered, so that
"for any call to a `Reset` or `Stop` method, no stale values prepared before that call will be sent
or received after the call". In the runtime, `needsAdd` puts a channel timer into the heap only when
`t.blocked > 0`, and `blockTimerChan`/`unblockTimerChan` add and remove it as receivers come and go;
the deadline `when` stays absolute. This is the lazy arming that option B copies.

**.NET.** "The countdown for the delay starts during the call to the constructor", and "Subsequent calls
to `CancelAfter` will reset the delay ... if it has not been canceled already". `TryReset` exists for
pooling a source between unrelated operations and is documented as safe only for the sole owner, when
no one else will cancel it; used concurrently with a cancel it can report success after the
cancellation. `Task.WaitAsync(TimeSpan)` is the per-call budget. Lesson: a reset on a shared token
needs an ownership rule.

**Python asyncio.** `timeout(delay)` returns `Timeout(loop.time() + delay)`: the deadline is fixed at the
call, before `async with`. `__aenter__` raises `RuntimeError("Timeout has already been entered")` on
reuse. `reschedule(when)` with `when <= loop.time()` schedules the cancel with `call_soon`, not
synchronously. `wait_for` is now implemented as `async with timeouts.timeout(timeout): return await
fut`, so the per-call form is a thin layer over the deadline object.

**Trio.** A cancel scope carries an absolute `deadline`; `move_on_at` takes one, `move_on_after` a
relative duration. Trio 0.27.0 (2024-10-17) changed `move_on_after`/`fail_after` to compute the
deadline on entry, because `ctx = trio.fail_after(5); await trio.sleep(5); with ctx: ...` timed out
immediately (issue #2512). Trio 0.34.0 removed the ability to set absolute deadlines on a scope built
with a relative one (issue #3403). The motivation for deadlines over per-operation timeouts is
N. J. Smith's essay: a library whose timeout restarts on every socket operation can be kept alive
forever by a peer that sends one byte per period ("if a malicious or misbehaving server sends at least
1 byte every 10 seconds ... our requests call ... will keep resetting its timeout over and over and
never return"). That is the failure mode of option A with a shared Timeout.

**Rust tokio.** `timeout()` computes the deadline at the call. `Sleep` is created with `timer: None`
and creates and registers its timer entry on the first `poll_elapsed` (tokio `src/time/sleep.rs`,
`new_timeout` and `poll_elapsed`, master as of 2026-10-02): the deadline is fixed early and the timer
work is deferred until someone waits, the same split as option B. `Sleep::reset(Instant)` moves the
deadline; the documented `select!` loop pins one `Sleep` so it bounds the whole loop. Known caveat:
"the future is polled before the timeout is checked", so a future that never yields can overrun.

**Kotlin.** `withTimeout` schedules `invokeOnTimeout` when called and covers the whole block; a
non-positive value cancels at once without running the block. The documented hazard is that the
timeout "runs concurrently the code running in the block and may happen at any time, even after the
block finishes executing but before the caller gets resumed with the result", so a resource returned
by the block can leak. `select`'s `onTimeout(ms)` registers a new `invokeOnTimeout` in every `select`
(`OnTimeout.kt`, `register`): per-wait semantics, expressed as a number.

**Java.** `orTimeout` attaches a timeout to one future. JEP 525 (delivered in JDK 26) configures a
timeout on a `StructuredTaskScope`; "if the timeout expires before or while waiting in `join()` then
the scope is cancelled": a deadline for a group of operations.

**JavaScript.** `AbortSignal.timeout(ms)` starts at the call and aborts with `TimeoutError`; the timer
counts active time, so it pauses in a suspended worker or a page in the back-forward cache. The signal
cannot be reset or cancelled; MDN points to `AbortController` with `setTimeout` for that.

**Swift.** SE-0526 `withDeadline` (status "Accepted with modifications") rejected a duration-based
`withTimeout` as the primary API: "Duration-based timeouts accumulate drift when passed through multiple
call layers ... With an absolute deadline, every layer in the stack sees the same instant". The
convenience `withDeadline(in:)` is `withDeadline(clock.now.advanced(by: timeout), ...)`, a deadline
computed at the call.

## 3. Options against the reference

| | A: restart per await | B: deadline at `timeout()`, lazy arm | C: A plus a new `deadline()` | D: deadline fixed at the first arm |
|---|---|---|---|---|
| `$t` shared by a loop of awaits | never times out if each wait is shorter | total budget | total only if the user picked `deadline()` | total budget, from the first wait |
| `$t = timeout(1000); work(2 s); await($x, $t)` | waits up to 1 s | throws at once | A or B by name | waits up to 1 s |
| Two coroutines share `$t` | depends on overlap (section 1, item 1) | one deadline | A or B by name | depends on which coroutine waits first |
| Matches the surveyed object APIs | none | Go, .NET, asyncio, tokio, `AbortSignal` | both | Trio entry semantics, without Trio's single entry |
| Matches stub and RFC wording | no text says restart | "trips after $ms", "similar to `delay`" | yes, with two names | partly |
| `isCompleted()` before any await | false forever | true once the deadline passed | by name | false forever |
| Cost at `timeout()` | 0 clock reads | 1 clock read | 0 or 1 | 0 |
| Cost per suspending await | 1 clock read + timer insert | timer insert + subtraction | 1 or 0 | first: as A; later: as B |
| BC against 1fdacf8 | none | created-early and reused Timeouts change | none | reused Timeouts change |

**Why not C.** Two constructors with the same signature and different clocks make the common spelling,
`timeout()`, the one that does not compose: a Timeout passed down to a library still restarts at each
of its awaits, which is the `requests` failure above. Under B the per-wait budget needs no second name,
because the idiomatic one-liner `await($x, timeout(300))` already creates a fresh Timeout per wait. The
only thing A offers that B does not is an idle timeout with zero allocations per iteration
(`$t = timeout(300); while (...) await($ch->recv(), $t)`), and no test, RFC example or documentation
uses that pattern.

**Why not D.** D is the cheapest (no clock read at `timeout()`) and keeps every single-use program
bit-identical, including the created-early case. Its deadline, however, is set by whichever coroutine
waits first, so the same program gives different budgets under different schedules, and a Timeout that
is never awaited never expires, so `isCompleted()` cannot answer "has the time passed". Trio's
start-at-entry works because a scope is entered exactly once, by one task.

**If a new name is wanted.** None is required for B. If Edmond wants one, the one that improves code is
a read-only accessor, `Timeout::remaining(): int` (milliseconds left, `0` once expired or cancelled),
which lets a caller hand the budget to an API that takes a plain number (`stream_set_timeout()`, curl
options), as Go's `ctx.Deadline()`, asyncio's `when()` and tokio's `deadline()` do. A mutating
`Timeout::reset(int $ms)` is not recommended now: .NET restricts its `TryReset` to a sole owner, Trio
removed deadline mutation on relative scopes in 0.34, and Go needed the 1.23 change to make `Reset`
free of stale fires. Add it only for a measured idle-timeout hot loop, with a sequence number that drops
a fire queued before the reset.

## 4. Behaviour changes against the reference

| Pattern | A (1fdacf8) | B |
|---|---|---|
| `await($x, timeout(ms))` inline, and every test in `php-async/tests` | budget `ms` from the arm | budget `ms` from `timeout()`; the gap is argument evaluation |
| `$t = timeout(ms); ...; await($x, $t)` | `ms` from the await | `ms` from `timeout()`; the work in between counts |
| one `$t` for several sequential awaits | `ms` per await | `ms` in total |
| one `$t`, overlapping awaits | `ms` from the first waiter while waiters overlap | `ms` from `timeout()` |
| await on a fired Timeout | throws at once, no `TimeoutException` as previous | throws at once with `TimeoutException` as previous |
| `signal/003` | passes via the notify path | passes via the "already completed" path, given rule 5 |

Users who relied on the restart, whether they knew it or not, get earlier cancellations, never later
ones. The fix for such code is to call `timeout()` per wait. CHANGELOG wording: "A Timeout shared by
several operations now bounds them together: its deadline is fixed when `timeout()` returns."

## 5. Semantics to write down

These rules replace the sentence "similar to `delay`" in the RFC and the stub docblock.

1. **Creation.** `timeout(int $ms)` requires `$ms > 0` (`ValueError` otherwise, as now). It returns a
   Timeout whose deadline is `now + $ms`, where `now` is the reactor's monotonic clock read at the
   call. Wall-clock changes do not move it.
2. **No effect alone.** A Timeout arms no timer, throws nothing and keeps the event loop alive for no
   one while no operation waits on it.
3. **Every use shares the deadline.** Each operation that takes the Timeout as its cancellation
   (`await()`, the `await_*` functions, `Channel` and `ThreadChannel` `send()`/`recv()`,
   `Scope::awaitCompletion()`, `signal()`, and every other `?Completable $cancellation` parameter)
   waits at most until the deadline, not for `$ms` from its own start. A second or concurrent operation
   sees the remaining time.
4. **Order at entry.** An operation first checks its awaitable: an awaitable that is already complete
   returns its result even if the deadline has passed (the order of `async.c:328-342` today). Then, if
   the Timeout has fired, was cancelled, or its deadline is not later than the reactor's current
   time, the operation throws without waiting. Otherwise it waits.
5. **Firing.** When the deadline passes while at least one operation waits, the Timeout becomes
   fired, which is terminal. Every operation it ends, then or later, throws
   `OperationCanceledException` whose `getPrevious()` is a `TimeoutException` with the message
   "Timeout occurred after $ms milliseconds", `$ms` being the value given to `timeout()`. A deadline
   that passes while no one waits makes the Timeout fired at the next observation (rule 4 or
   `isCompleted()`). A fired Timeout cannot be restarted.
6. **Precision.** The operation is cancelled no earlier than the deadline on the reactor clock and no
   later than the first timer phase of the event loop after it, at millisecond granularity. A deadline
   that passed during synchronous work in the current tick can be detected one tick late, never early.
7. **`cancel()`** makes the Timeout cancelled (terminal); later operations throw at once, as now.
   `isCompleted()` is true once the Timeout is fired, cancelled, or past its deadline; `isCancelled()`
   only after `cancel()`.
8. **No periodic Timeout.** Remove `is_periodic` from `async_timeout_create`.

## 6. Hot path cost

Measured on this machine (2026-10-02, Linux 6.18 guest, clocksource `tsc`): one `clock_gettime` costs
about 24 instructions under callgrind (24,226,231 instructions for 10^6 calls in a loop against 224,667
for none) and 29.5 ns with `CLOCK_MONOTONIC` or 5.3 ns with `CLOCK_MONOTONIC_COARSE` in a timed loop.
libuv's `uv_update_time` uses `CLOCK_MONOTONIC_COARSE` only when its resolution is 1 ms or better
(libuv `src/unix/linux.c`); here it is 4 ms, so libuv reads `CLOCK_MONOTONIC`. The reference's
`timeout()` and await were not measured.

| Path | A (1fdacf8) | B |
|---|---|---|
| `timeout()` | 2 allocations, `uv_timer_init`, no clock read | same, plus 1 clock read and 1 store |
| await, awaitable already complete | no timer work | same; the deadline is not consulted |
| await that suspends | `uv_update_time` (1 clock read) + `uv_timer_start` | load, compare, subtract + `uv_timer_start`, no clock read |
| N sequential awaits on one Timeout | N clock reads | 1 clock read in total |
| Timeout never awaited | nothing | 1 clock read at creation |
| wake | `uv_timer_stop` | same |
| allocations | 2 per `timeout()` | 2 per `timeout()`; the deadline is 8 bytes inside the existing timer-event allocation |

The arm needs no clock read under B because libuv computes the due time from the same cached clock:
arming with `deadline - loop->time` gives `loop->time + (deadline - loop->time) = deadline` exactly,
however stale `loop->time` is (libuv `src/timer.c:78`). The fresh read is needed once, at
`timeout()`, so that a Timeout created after synchronous CPU work does not start from a stale clock
(the #185 fix, `CHANGELOG.md:165`, moves from the arm to the creation).

Net effect: B adds about 24 instructions to a `timeout()` whose operation never suspends, and removes
about 24 from every suspending await. Whether 24 instructions stay under 3 % of
`timeout()` + an await that returns at once is not measured; it holds if that path costs 800
instructions or more, which is an estimate, not a number.

This corrects `prior-art.md` 13.2 (lines 328-333), which stores the deadline from the cached `uv_now`
at `timeout()` and keeps the clock refresh at the arm. That combination fires early: the deadline
inherits the staleness of the creating tick, and refreshing at the arm cannot recover it.

### Implementation sketch for the reference

- `zend_async_timer_event_t` (php-src-true-async `Zend/zend_async_API.h:1256-1262`) gains
  `uint64_t deadline` (0 means a relative timer). The struct holds the base event, an `unsigned int`
  and a `bool`, so the field adds 8 bytes to the one allocation.
- The reactor gets one entry point that refreshes the loop clock and returns it (today
  `zend_async_now_fn` returns the cached value, `Zend/zend_async_API.h:548-554`). `timeout()` calls it
  after `ZEND_ASYNC_NEW_TIMER_EVENT_EX`, which has already started the reactor
  (`libuv_reactor.c:1236`; `libuv_now` returns 0 before that, `:526-531`), and stores
  `deadline = now + ms`. `ZEND_ASYNC_TIMER_F_REFRESH_CLOCK` is no longer set on a Timeout; `delay()`
  keeps it.
- `libuv_timer_start`: when `deadline != 0`, arm with
  `deadline > uv_now(loop) ? deadline - uv_now(loop) : 0` and skip `uv_update_time`.
  `event->timeout` keeps `$ms` for the exception message.
- One helper, used by `async_resolve_cancel_token` (`async_API.c:1253`) and by the three call sites
  that test `ZEND_ASYNC_EVENT_IS_CLOSED` on a cancellation directly (`thread_channel.c:159`, `:278`,
  `async.c:1396`): a Timeout whose `deadline <= ZEND_ASYNC_NOW()` is closed on the spot (as
  `on_timer_event` does, `libuv_reactor.c:1063-1066`) and treated as fired. A call site that misses the
  helper stays correct: it arms with 0 ms and the Timeout fires on the next tick.
- A `replay` handler on the Timeout event yields a new `TimeoutException`, so rule 5 holds for
  operations that start after the fire and for `signal()`'s rejected Future.
- Optional, measure first: keep the timer armed at the last waiter's wake (Go's zombie timer). Under B
  a fire with no waiters is correct (the deadline did pass), which A could not allow; the timer must
  then be `uv_unref`'d so it neither keeps the loop alive nor counts as a waiter for deadlock detection.

### Mapping to the new extension (S3 plan)

`dev/plans/S3.md:538` leaves this question open for the `ARM_ON_WAIT` record. Under B the record
stores the deadline at `timeout()` (refreshed clock), arms at 0 → 1 subscriptions with the remainder,
disarms at 1 → 0 unless fired, and phase 1 treats `deadline <= now` as fired without arming. Nothing
else in the record layout changes.

## 7. Open measurements

1. Callgrind instruction counts, A against B, for three PHP loops of 10^6 iterations:
   `timeout(1000)` alone; `await($completedFuture, timeout(1000))`; `await($ch->recv(), timeout(1000))`
   with a value already buffered. Criterion: B within 3 % of A on each. If the first two exceed it,
   switch to D, which has no creation-time clock read.
2. The same harness for `$t = timeout(1000); for (10^6) await(spawn(fn() => null), $t)` to confirm
   the saved clock read per suspending await.
3. Run `signal/003` with rule 5 implemented and check that it takes the "already completed" branch.

## 8. Decisions made for Edmond in this report

- The fired-Timeout replay defect (section 1, item 2) was found while reading; it is reported here,
  not fixed, because the task allowed writing only this file.
- `Timeout::remaining()` is offered as optional; `reset()` is advised against until a measured need.
- Rule 4 keeps the reference's priority of a completed awaitable over an expired Timeout.
- The deadline is placed in the core timer struct rather than in `async_timeout_ext_t`, because the
  reactor's `libuv_timer_start` must read it.

## 9. Sources

- Go: [context package](https://pkg.go.dev/context);
  [`context.go`](https://raw.githubusercontent.com/golang/go/master/src/context/context.go)
  (`WithDeadlineCause`); [time package](https://pkg.go.dev/time);
  [Go 1.23 release notes, timer changes](https://go.dev/doc/go1.23);
  [`runtime/time.go`](https://raw.githubusercontent.com/golang/go/master/src/runtime/time.go)
  (`needsAdd`, `blockTimerChan`, `unblockTimerChan`, `maybeRunChan`);
  [`net.Conn`](https://pkg.go.dev/net#Conn).
- .NET: [`CancellationTokenSource` constructors](https://learn.microsoft.com/en-us/dotnet/api/system.threading.cancellationtokensource.-ctor?view=net-8.0);
  [`CancelAfter`](https://learn.microsoft.com/en-us/dotnet/api/system.threading.cancellationtokensource.cancelafter?view=net-8.0);
  [`TryReset`](https://learn.microsoft.com/en-us/dotnet/api/system.threading.cancellationtokensource.tryreset?view=net-8.0);
  [`Task.WaitAsync`](https://learn.microsoft.com/en-us/dotnet/api/system.threading.tasks.task.waitasync?view=net-8.0).
- Python: [`asyncio/timeouts.py`](https://raw.githubusercontent.com/python/cpython/main/Lib/asyncio/timeouts.py);
  [`asyncio/tasks.py`](https://raw.githubusercontent.com/python/cpython/main/Lib/asyncio/tasks.py) (`wait_for`).
- Trio: [core reference, cancel scopes](https://trio.readthedocs.io/en/stable/reference-core.html);
  [release history (0.27.0, 0.34.0)](https://trio.readthedocs.io/en/stable/history.html);
  [issue #2512](https://github.com/python-trio/trio/issues/2512);
  [N. J. Smith, "Timeouts and cancellation for humans"](https://vorpus.org/blog/timeouts-and-cancellation-for-humans/).
- tokio: [`time::timeout`](https://docs.rs/tokio/latest/tokio/time/fn.timeout.html);
  [`time::Sleep`](https://docs.rs/tokio/latest/tokio/time/struct.Sleep.html);
  [`time/timeout.rs`](https://raw.githubusercontent.com/tokio-rs/tokio/master/tokio/src/time/timeout.rs);
  [`time/sleep.rs`](https://raw.githubusercontent.com/tokio-rs/tokio/master/tokio/src/time/sleep.rs).
- Kotlin: [`withTimeout`](https://kotlinlang.org/api/kotlinx.coroutines/kotlinx-coroutines-core/kotlinx.coroutines/with-timeout.html);
  [`Timeout.kt`](https://raw.githubusercontent.com/Kotlin/kotlinx.coroutines/master/kotlinx-coroutines-core/common/src/Timeout.kt);
  [`onTimeout`](https://kotlinlang.org/api/kotlinx.coroutines/kotlinx-coroutines-core/kotlinx.coroutines.selects/on-timeout.html);
  [`OnTimeout.kt`](https://raw.githubusercontent.com/Kotlin/kotlinx.coroutines/master/kotlinx-coroutines-core/common/src/selects/OnTimeout.kt).
- Java: [`CompletableFuture`](https://docs.oracle.com/en/java/javase/25/docs/api/java.base/java/util/concurrent/CompletableFuture.html);
  [JEP 525](https://openjdk.org/jeps/525).
- JavaScript: [MDN `AbortSignal.timeout()`](https://developer.mozilla.org/en-US/docs/Web/API/AbortSignal/timeout_static).
- Swift: [SE-0526 `withDeadline`](https://github.com/swiftlang/swift-evolution/blob/main/proposals/0526-deadline.md);
  [returned for revision, 2026-06-14](https://forums.swift.org/t/returned-for-revision-se-0526-withdeadline/87379).
- libuv: [`src/timer.c`](https://raw.githubusercontent.com/libuv/libuv/v1.x/src/timer.c) (`uv_timer_start`);
  [`src/unix/linux.c`](https://raw.githubusercontent.com/libuv/libuv/v1.x/src/unix/linux.c) (`uv__hrtime`, fast clock).
- Local: php-async `1fdacf8` (`async.c`, `async_API.c`, `libuv_reactor.c`, `async.stub.php`,
  `php_async.h`, `CHANGELOG.md`, `tests/`); php-src-true-async `863f6dd90cf`
  (`Zend/zend_async_API.h`, `Zend/zend_async_API.c`); php-true-async-rfc `6bd8fca`
  (`base.rfc`, `base-full.rfc`, `scope.rfc`, `arh/bounded_scope.md`).
