# Prior art for the wait model (2026-10-02)

This review compares the wait model of `wait-model-sage.md` section 2 (WM), approved as
`EDMOND-DECISIONS.md` items 26-28 (D26-D28), with the waiter mechanisms of Go, the Linux kernel, Rust
(tokio, futures), C++ (C++20 awaiters, Asio, folly), Kotlin, Java Loom, .NET, Python (asyncio, Trio),
Erlang and Zig. Section 1 gives the verdict, sections 2-11 one runtime each, section 12 the choices
that prior art confirms, section 13 what the model may have missed, most important first.

Method. Runtime sources were fetched from the default branch on 2026-10-02 and read; function and
field names below come from those files, line numbers are omitted because they drift. Issue and CVE
facts come from the linked trackers and advisories. Project references (`channel.c`, `scheduler.c`,
`libuv_reactor.c`) are php-async `1fdacf8`; the fork's `zend_async_API.c` is read in
`/home/user/php-src-true-async`; the RFC IO header (`main/php_io_hooks.h`) is read with
`git show 834811f2d88:` in `/home/user/php-src`. Nothing was built, run or measured.

## 1. Verdict

The model's core matches the most mature designs. Wait records on the waiter's stack, linked into the
target's list, are what Linux does (`DEFINE_WAIT`, `poll_wqueues` with on-stack inline entries) and
what C++20 awaiters and tokio's pinned waiters do. One pointer list from the waiter to its records is
Go's `g.waiting`. Unlink of every record at wake is Linux's `autoremove_wake_function` and pass 3 of
Go's `selectgo`. Wake by enqueue instead of inline resume is the rule .NET and asyncio arrived at.
Counting a timer as a liveness source only while someone waits on it is stricter than Go's
`checkdead`, which treats any pending timer as liveness.

Every mature runtime hit the same five problems, and the model leaves three of them open and two
partly open:

1. A waiter woken with a consumed value (channel item, permit, lock ownership) and then cancelled
   loses the value. WM step 6 discards `result` on the error exit; the reference channel hands the
   reservation on after resume (`channel.c:755-778`), but the CHANNEL kind's `abort` (D29) only
   removes the queue entry. Go, Linux, tokio, asyncio and Kotlin each have an explicit rule here.
2. A Timeout armed lazily keeps the reference's restart semantics: its clock restarts at every
   await. Go 1.23 arms channel timers lazily and still keeps an absolute deadline; the choice
   between eager and restart in `passive-edges-optimizer.md` 4.4 has a third option.
3. A target torn down with records still linked: WM clears `rec->event` in release builds and wakes
   nobody, so the waiter parks forever. Linux (`POLLFREE`) and Trio (`break_lot`) wake the waiters
   with an error.
4. Unlink from the callbacks vector is a linear search (fork `zend_async_callbacks_remove`), so N
   waiters leaving one shared target one by one (a shared cancellation, a fan-in on one coroutine)
   cost O(N^2). Go, Linux and tokio use O(1) removal.
5. Completion-based IO (io_uring, the libuv threadpool) keeps writing into memory after a
   cancellation request. The RFC queue has `orphan()` and `in_flight`; the model's IO kind must route
   `unlink` and `abort` through them, and no completion may be keyed by a frame address.

## 2. Go

**Records.** A `sudog` (`runtime/runtime2.go`) holds `g`, `next`/`prev` (the channel's `waitq` is a
doubly linked list), `elem` (pointer to the value slot, often on the waiter's stack), `c`, `isSelect`,
`success` and `waitlink`. The type comment explains the indirection: the g-to-object relation is many
to many. Sudogs come from a per-P cache (`acquireSudog`, `releaseSudog` in `proc.go`), never from the
goroutine stack, because Go stacks move. `acquireSudog` documents an allocation hazard of the same
kind as WM invariant L: `new(sudog)` can trigger GC, the GC uses semaphores, the semaphore code calls
`acquireSudog`; the cycle is broken with `acquirem`. `releaseSudog` throws when any field (`elem`,
`isSelect`, `next`, `prev`, `waitlink`, `c`, `gp.param`) is still set: a cheap invariant check at the
point of reuse.

**Waiter-side list.** `g.waiting` lists the sudogs of the current wait in lock order. `selectgo`
throws `"gp.waiting != nil"` on entry (WM's debug `ZEND_ASSERT(c->waker.wait == NULL)` is the same
check). The traceback, the stack copier and the leak detector walk `g.waiting`.

**Select.** `selectgo` (`select.go`) builds `pollorder` by a random shuffle (`cheaprandn`) and
`lockorder` by channel address; pass 1 polls in random order, pass 2 enqueues a sudog on every
channel, pass 3 after wake dequeues from every channel that did not fire. Across threads, two
channels can fire one select at once; `(*waitq).dequeue` (`chan.go`) resolves it with
`g.selectDone.CompareAndSwap(0, 1)` and skips the loser. Random poll order exists for fairness:
a select in a loop does not starve later cases.

**Races found in production.** Issue [#40641](https://github.com/golang/go/issues/40641) (Go 1.14,
1.15, release blocker): the stack shrinker adjusted `sudog.elem` pointers into a stack while the
goroutine was between "decided to park on a channel" and "parked"; the fix is
`g.parkingOnChan`, an atomic flag that makes that window an unsafe point for shrinking. The
window between link and switch in WM (U2, U5) is the same window; WM closes it by switch-blocking
the tick.

**Timers.** Since Go 1.23 a channel timer (`time.go`: `timer.isChan`, `timer.blocked`) is in the
timer heap only while a goroutine is blocked on its channel: `blockTimerChan` adds it at the first
blocked receiver, `unblockTimerChan` leaves it as a `timerZombie` when the count reaches 0, and the
next block un-zombies it without a heap operation. The deadline `when` is absolute, set at creation
or `Reset`. A receive that does not block calls `(*timer).maybeRunChan`, which fires the timer
on the spot if `when <= now`. `timer.seq` discards a send that belongs to an earlier `Reset`.

**Diagnostics.** `g.waitreason` is an enum with fixed strings ("chan receive", "select",
"sync.Mutex.Lock", and the never-wakeable "chan receive (nil chan)" and "select (no cases)").
`g.waitsince` is set lazily: the GC mark phase (`markroot` in `mgcmark.go`) stores the cycle start
time into a blocked goroutine whose `waitsince` is 0, and the traceback prints
"[chan receive, 12 minutes]". The hot path pays nothing.

**Deadlock detection.** `checkdead` (`proc.go`) runs when the last running M goes idle: with no
running M, every user goroutine waiting and no timer in any P's heap, it prints "all goroutines are
asleep - deadlock!" and exits. It detects only global deadlock, and any pending timer (even one
nobody waits on) suppresses it.

**Leak detection.** Go 1.26 adds an experimental `goroutineleak` profile
(`GOEXPERIMENT=goroutineleakprofile`, [release notes](https://go.dev/doc/go1.26)). The GC marks from
the usual roots but scans the stack of a blocked goroutine only once one of the objects it waits on
is marked: `(*g).isMaybeRunnable` (`mgc.go`) walks `g.waiting` and tests `sg.c` or `sg.elem`;
`setSyncObjectsUntraceable` hides those pointers during the cycle so the waiter's own sudogs do not
keep its targets alive; `findGoroutineLeaks` iterates to a fixed point and marks the rest
`_Gleaked`. Wait reasons outside channels and `sync` count as runnable (conservative).

**Fairness of locks.** `internal/sync/mutex.go` switches a mutex to starvation mode when a waiter
has waited more than 1 ms (`starvationThresholdNs`): ownership is then handed to the first waiter
instead of letting a running goroutine barge in.

## 3. Linux kernel wait queues and poll

The kernel is the closest structural match: wait entries live on the sleeping task's stack.

- `DEFINE_WAIT` creates a `wait_queue_entry` on the stack; `prepare_to_wait_event`
  (`kernel/sched/wait.c`) links it and sets the task state under the queue lock, so a wake between
  the condition check and the sleep is not lost. `autoremove_wake_function` unlinks the entry at
  wake; `finish_wait` unlinks it otherwise and uses `list_empty_careful` to skip the lock when the
  waker already removed it.
- Exclusive waiters (`WQ_FLAG_EXCLUSIVE`, added at the tail) get one wake per `wake_up`; everyone
  else gets all. This is the kernel's answer to the thundering herd.
- `prepare_to_wait_event` documents the woken-then-cancelled rule: "Exclusive waiter must not fail
  if it was selected by wakeup, it should 'consume' the condition we were waiting for ... it should
  wake up another exclusive waiter if we fail."
- `select`/`poll` (`fs/select.c`) put a `struct poll_wqueues` on the stack with
  `inline_entries[N_INLINE_POLL_ENTRIES]` (`include/linux/poll.h`: 832 B of stack minus 256) and
  allocate pages only beyond that; `poll_freewait` unlinks all of them. This is WM's `await_*`
  frame with K = 8 inline records and a heap overflow.
- Lifetime bugs. [CVE-2019-2215](https://googleprojectzero.github.io/0days-in-the-wild/0day-RCAs/2019/CVE-2019-2215.html)
  (Android binder, exploited in the wild): `binder_thread` was freed by `BINDER_THREAD_EXIT` while
  epoll still had an entry linked into its `wait` queue; epoll cleanup then wrote into freed memory.
  [CVE-2021-47505](https://lkml.iu.edu/2112.1/05727.html): aio poll did not handle `POLLFREE`, the
  notification that signalfd and binder send to every waiter before freeing a queue whose lifetime
  is the task's, not the file's. Rule taught: a queue that can die while entries are linked must
  wake and detach them first.

## 4. Rust: tokio and futures

**Records.** `tokio::sync` waiters (`Notify`'s `Waiter`, the batch semaphore's `Waiter`) are
intrusive list nodes inside the future, which is pinned and `!Unpin`; `util/linked_list.rs` removes
a node in O(1). Drop is cancellation: `Notified::drop` (`sync/notify.rs`, `drop_notified`) and
`Acquire::drop` (`sync/batch_semaphore.rs`) unlink the node under the lock.

**Woken-then-dropped.** `drop_notified`: "See if the node was notified but not received. In this
case, if the notification was triggered via `notify_one`, it must be sent to the next waiter."
`Acquire::drop` returns permits already assigned to the node (`add_permits_locked`). Both are the
Linux exclusive-waiter rule.

**Cancel safety is documented per API.** The `select!` docs (`macros/select.rs`) list methods that
are cancel safe (`mpsc::Receiver::recv`, `TcpListener::accept`), methods that lose data
(`read_exact`, `write_all`), and methods that lose their place in a fair queue (`Mutex::lock`,
`Semaphore::acquire`, `Notify::notified`).

**Fairness.** `select!` polls branches from a random start unless `biased;` is given. Cooperative
budgeting (`task/coop/mod.rs`, `Budget::initial` = 128) makes tokio resources return `Pending` after
128 operations in one poll even when ready, so a task looping on always-ready operations still
yields.

**Wake model.** A `Waker` may be called spuriously or more than once; the task re-polls and
re-checks. This pull model never loses a value at the waker; cancel safety moves into each future's
own state. WM is a push model (the waker carries `result` and `error`), like Go and Kotlin, so it
needs exactly-once delivery and an explicit rule for undelivered values.

**Diagnostics.** tokio-console's lints (`tokio-console/src/warnings.rs`): `LostWaker` warns when a
task is idle, not completed, not woken, and holds no waker ("will never be woken again");
`NeverYielded` warns about a task that has run more than 1 s without yielding.

**Completion IO.** withoutboats, ["Notes on io-uring"](https://without.boats/blog/io-uring/): "the
kernel will write to or read from that buffer even if you cancel the future"; the conclusion is
that the kernel must own the buffer.

## 5. C++: C++20 awaiters, Asio, folly

**Records.** An awaiter object lives in the coroutine frame for the duration of `co_await`; Lewis
Baker ([Understanding operator co_await](https://lewissbaker.github.io/2017/11/17/understanding-operator-co-await))
describes it as borrowing frame memory for per-operation state, with no heap allocation. The same
article gives the hazard of WM's window: once `await_suspend` publishes the handle, another thread
may resume and destroy the coroutine before `await_suspend` returns, so `await_suspend` must not
touch `this` afterwards.

**Asio.** Per-operation cancellation through a cancellation slot that holds exactly one handler.
`cancellation_type` (`boost/asio/cancellation_type.hpp`) classifies what a successful cancellation
guarantees: `terminal` (only close or destroy the object afterwards), `partial` (partial side
effects, object usable), `total` (no observable side effects). The [overview](https://www.boost.org/doc/libs/latest/doc/html/boost_asio/overview/core/cancellation.html)
states that an operation that does not support the requested type does not cancel at all.

**folly.** `CancellationCallback` (`folly/CancellationToken.h`) runs the callback inline in its
constructor if cancellation was already requested; its destructor blocks until a callback running
on another thread returns.

## 6. Kotlin coroutines

`CancellableContinuation` (`CancellableContinuation.kt`) gives the "prompt cancellation guarantee":
if the job is cancelled while the continuation is suspended it does not resume successfully, "even
if `CancellableContinuation.resume` was already invoked but not yet executed". This is WM step 6.
Kotlin pairs it with a disposal path for the value: `resume(value, onCancellation)` calls
`onCancellation` with the undelivered value, and `Channel(onUndeliveredElement = ...)`
(`channels/Channel.kt`, "Undelivered elements") is called for an element a receive retrieved but
could not return because of cancellation. Exceptional resume against cancellation: the first one
wins.

`select` is biased to the first clause (`selects/Select.kt`); `selectUnbiased` shuffles
(`SelectUnbiased.kt`). `invokeOnCancellation` handlers must be fast, non-blocking and thread safe.
The `Channel.send` docs state that send does not check for cancellation when it does not suspend.

## 7. Java Loom

`LockSupport.park` uses a single permit: an `unpark` before `park` makes `park` return at once, so
a wake that arrives before the park is not lost (WM's U2 short path does the same). `park` may also
return spuriously, so every caller loops and re-checks. Pinning: a virtual thread blocked where it
cannot unmount holds its carrier; [JEP 491](https://mail.openjdk.org/pipermail/jdk-dev/2024-November/009632.html)
(JDK 24) removed pinning for `synchronized`, native frames and class initializers still pin, and
JFR's `jdk.VirtualThreadPinned` event reports each case. Loom degrades (blocks the carrier); WM
refuses with `Error` where switching is blocked (D14).

## 8. .NET

`TaskCompletionSource.SetResult` runs continuations synchronously by default; the standing advice
([Premier Developer blog](https://devblogs.microsoft.com/premier-developer/the-danger-of-taskcompletionsourcet-class/))
is `TaskCreationOptions.RunContinuationsAsynchronously`, because inline continuations couple the
completer's thread to the waiter's code and deadlock. `CancellationToken.Register` on an
already-cancelled token runs the delegate "immediately and synchronously" and propagates its
exception ([docs](https://learn.microsoft.com/en-us/dotnet/api/system.threading.cancellationtoken.register));
`CancellationTokenRegistration.Dispose` returns only after a running callback finishes, "except in
the degenerate case where the callback is unregistering itself".

## 9. Python asyncio

`Future` callbacks are scheduled with `loop.call_soon` (`futures.py`, `__schedule_callbacks`), never
run inline. `Task.cancel` cancels the future the task waits on (`_fut_waiter.cancel`), or sets
`_must_cancel` when the task is not waiting; 3.11 added `cancelling()`/`uncancel()` so a timeout can
tell its own cancellation from an outer one.

Bugs in exactly the races WM must handle:

- `Queue.get` (`queues.py`): after `await getter` raises, "We were woken up by put_nowait(), but
  can't take the call. Wake up the next in line." The waiter list is a deque; removal of a cancelled
  getter is a linear `remove`.
- [bpo-27585](https://bugs.python.org/issue27585): `asyncio.Lock` deadlocked after a woken waiter was
  cancelled. [gh-90155](https://github.com/python/cpython/issues/90155): `Semaphore` FIFO did not
  hold, and a waiter cancelled after its future was marked done lost the wake (fixed in 3.11.0;
  Guido van Rossum's [analysis](http://neopythonic.blogspot.com/2022/10/reasoning-about-asynciosemaphore.html)).
- [gh-86296](https://github.com/python/cpython/issues/86296): `wait_for` swallowed an outer
  cancellation when the inner task completed in the same iteration; [gh-81839](https://github.com/python/cpython/issues/81839):
  `wait_for` leaked a connection when the result arrived together with the timeout. `wait_for` was
  rewritten on top of `asyncio.timeout` for 3.12 ([PR 98518](https://github.com/python/cpython/pull/98518)).

## 10. Trio

The low-level wait is `wait_task_rescheduled(abort_func)` (`_core/_traps.py`). On cancellation
Trio calls `abort_func` at most once; it returns `Abort.SUCCEEDED` (the operation is detached, the
task gets `Cancelled`) or `Abort.FAILED` (it cannot detach now; someone will still call
`reschedule`, possibly with a late cancellation). The docs cite Windows overlapped IO, whose cancel
is itself asynchronous. WM's `kind->unlink` has no FAILED answer.

`ParkingLot` (`_core/_parking_lot.py`) keeps waiters in an `OrderedDict` (O(1) removal, FIFO);
`repark` moves waiters to another lot without waking them (condition variable to lock);
`break_lot` wakes every waiter with `BrokenResourceError`, and a task registered with
`add_parking_lot_breaker` breaks its lots when it exits (`_run.py`), so a lock whose owner died
does not hang its waiters.

Every Trio async function is a checkpoint even when it does not block: a cancel point and a
schedule point. Cancellation is level-triggered: once a scope is cancelled, every checkpoint in it
raises again ([reference](https://trio.readthedocs.io/en/stable/reference-core.html)).

## 11. Erlang, Zig, Lua

**Erlang.** A process waits in `receive`; a reply that arrived after the caller's timeout used to
stay in its mailbox. OTP 24 process aliases (EEP-53, [OTP 24.0](https://www.erlang.org/patches/otp-24.0))
let `gen_server:call` drop late replies; `gen_server:call/2` has a 5000 ms default timeout
([docs](https://www.erlang.org/doc/apps/stdlib/gen_server.html)). The runtime does not detect
deadlocks; the default timeout turns one into an error.

**Zig.** The new `std.Io` ([Andrew Kelley](https://andrewkelley.me/post/zig-new-async-io-text-version.html)):
`Future.cancel` has the semantics of `await` plus a cancellation request, both idempotent, and it
returns the result, so `defer if (a.cancel(io)) |s| gpa.free(s)` frees a resource the task produced
before it noticed the request.

**Lua.** Coroutines have no waiter records; scheduling is in user libraries. Nothing applies.

## 12. Choices prior art confirms

| WM choice | Prior art |
|---|---|
| Records on the waiting frame's stack, no allocation per wait | Linux `DEFINE_WAIT`, `poll_wqueues` inline entries; C++20 awaiters; tokio pinned waiters |
| `await_*` with K inline records, heap beyond | Linux `N_INLINE_POLL_ENTRIES` then pages |
| `waker.wait` + `wait_count` from waiter to records | Go `g.waiting`, used by traceback, stack copy, leak detector |
| Unlink all records at wake (D26) | Linux `autoremove_wake_function`; Go `selectgo` pass 3; Erlang aliases for late replies |
| `wait == NULL` asserted at wait entry | Go `selectgo` throws `gp.waiting != nil`; `releaseSudog` field checks |
| Already-fired cancellation and Timeout checked in phase 1 | folly and .NET run a registration on a cancelled token inline |
| Wake enqueues, never resumes inline | .NET `RunContinuationsAsynchronously`; asyncio `call_soon` |
| No allocation after the first link (L) | Go `acquireSudog`'s GC recursion note |
| No CAS on the claim: one thread per scheduler | Go needs `selectDone` only because of multiple Ms |
| Timer counted as liveness only while awaited (`F_COUNTED`) | stricter than Go `checkdead` (any pending timer) |
| Prompt cancellation (result discarded, error thrown) | Kotlin's prompt cancellation guarantee |

## 13. What we may have missed

Ordered by expected cost of the miss. Each item names the runtime that taught it, why it matters
for this model, and a check or change.

### 13.1 Consumed values after a wake that is then cancelled

Taught by: Linux `prepare_to_wait_event`, tokio `Notified::drop` and `Acquire::drop`, asyncio
`Queue.get` and bpo-27585 / gh-90155, Kotlin `onUndeliveredElement`, Zig `Future.cancel`.

Why. WM step 6 drops `result` when `waker.error` is set, and SF 2.8 (7) applies a cancellation to a
QUEUED coroutine that was already woken with a result. That is harmless when the result is a copy
of state the target keeps (a coroutine's or Future's result). It loses data when the wake transfers
ownership: a channel item, a pool resource, a semaphore permit or mutex ownership (S9). The
reference channel already handles the resume path with reservations (`channel.c:755-778`: an
unspent reservation is handed to the next waiter). Two paths remain open: the CHANNEL kind's
`abort` (D29) is specified as queue removal only, so a coroutine that holds a reservation and is
then aborted (bailout, U6) strands it; and any future kind that writes a consumed value into
`waker.result` would lose it on cancel.

Check or change. Write one rule into S3.md: a record callback never moves ownership into the
waker; a consuming kind keeps the value in the target under a reservation until the frame claims it
after resume, and both the frame's error exit and `kind->abort` release the reservation and wake
the next waiter. Test: a receiver woken with an item and cancelled before it runs; the item reaches
the next receiver. The same with a bailout in place of the cancel.

### 13.2 Timeout: lazy arming with an absolute deadline

Taught by: Go 1.23 `blockTimerChan`, `unblockTimerChan`, `timerZombie`, `maybeRunChan`.

Why. `ARM_ON_WAIT` (WM 2.3, `passive-edges-optimizer.md` 4.4, FO "including restart") restarts the
full timeout at every 0 → 1 subscription. `$t = Async\timeout(1000); while (...) await($ch->recv(), $t);`
never times out if each iteration waits less than a second, and the stub says the Timeout "trips
after $ms" (`async.stub.php:183`). Section 4.4 framed the choice as eager (timer work on every
`timeout()`) against restart; Go shows that lazy arming and a fixed deadline combine.

Check or change. Store an absolute deadline at `timeout()` (`uv_now`, already cached by the loop:
no syscall); arm at 0 → 1 with the remaining time; in phase 1 treat `deadline <= now` as fired
without arming. Optionally keep the handle armed at 1 → 0 and ignore a fire with no subscribers
(Go's zombie) to save a heap removal and re-insert per loop iteration; measure both. When the arm
moves into the direct call, keep the reference's clock refresh (`ZEND_ASYNC_TIMER_SET_REFRESH_CLOCK`,
`async.c:1662`; `uv_update_time` in `libuv_timer_start`, `libuv_reactor.c:1084-1086`): libuv caches
the time at the start of the tick ([uv_now](https://docs.libuv.org/en/v1.x/loop.html)), so an arm
after long CPU work fires early without it. This changes semantics: Edmond's decision.

### 13.3 Target torn down with linked records

Taught by: Linux `POLLFREE` (CVE-2021-47505) and CVE-2019-2215; Trio `break_lot` and
`add_parking_lot_breaker`.

Why. WM 2.2 "Teardown": an `F_RECORD` element in a vector being destroyed is a debug assertion; a
release build sets `rec->event = NULL` and calls nothing. Invariant F says it cannot happen, but the
waiter then parks with no edge and no wake: a hang with no report (the deadlock detector may later
catch it only if nothing else runs). Typed owners (an IO handle closed by another coroutine, a
reactor shutdown, a thread object) are where F fails in practice; the reference already meets the
case for fds closed elsewhere (`scheduler.c:819-830`, "EBADF notifications for fds closed
externally").

Check or change. Teardown of a vector with records wakes each waiter with an error (for example
"awaited object was destroyed") through the normal enqueue, then asserts in debug builds. The
release path costs nothing on the hot path. Test: close a socket from coroutine B while A waits on
it.

### 13.4 Linear unlink and order of the callbacks vector

Taught by: Go `waitq` (doubly linked), Linux `list_head`, tokio `linked_list`, Trio `OrderedDict`;
asyncio pays O(n) in `Queue.get`.

Why. A record does not know its position; `async_wait_unlink` removes it by search (fork
`zend_async_callbacks_remove` loops over the vector) unless it is at the notify cursor. One target
with N waiters that leave one at a time costs O(N^2): a request-wide cancellation shared by N
waits that each finish normally, N coroutines awaiting one coroutine with individual timeouts.
Swap-remove (SF 2.4) also reorders the vector, so it is not FIFO after the first removal.

Check or change. Measure first: N ∈ {1 000, 10 000} waiters on one Cancellation, each woken by its
own Future. If it shows, give records an index at no size cost: the record's `ref_count` slot is
unused ("a record never reads it", WM 2.1), so store the index there, update it whenever swap-remove
or the cursor rule moves a record, and keep the search for heap subscribers. An intrusive
`prev`/`next` pair (+16 B per record) would also keep FIFO and remove phase 2's capacity
reservation. Independently, state that no wake-one primitive may take "the first waiter" from a
callbacks vector; wake-one targets keep a typed FIFO queue, as the channel does.

### 13.5 Completion-based IO memory after cancel or abort

Taught by: withoutboats on io_uring; libuv `uv_cancel` (fails once a request runs, and the
callback still runs later with `UV_ECANCELED`, [docs](https://docs.libuv.org/en/v1.x/request.html));
Asio (completion handler always runs); Trio `Abort.FAILED`.

Why. WM puts the IO op and the Timer op in the waiting frame (WM 2.3: `delay()`, IO through the
provider). With a readiness queue a cancel is synchronous (`php_io_poll_queue_orphan` is "a plain
cancel"). With a completion queue the kernel or a thread keeps the op, its `buf`, and the output
pointers (`accept.addr`, `getaddrinfo.res`, `waitpid.status`, `sigwait.info` in
`php_io_hooks.h`) until completion; U4-U6 abort the frame and the context returns to the pool. The
RFC already has `orphan()`, `in_flight` and `php_io_stream_orphan`, so the gap is the wait model's
binding to them, not the IO layer.

Check or change. The IO kind's `unlink` calls `cancel` when the op is not `in_flight` and `orphan`
otherwise; `abort` always orphans; an orphaning queue must re-home every pointer into the frame
(the op itself if completions are keyed by its address) before the frame can die. A fiber stack
must not return to the pool while an op on it is in flight (debug assert on `in_flight` ops per
context). Record this as an S4.1/S6 constraint next to the existing "queue must not dispatch into
dead frames" line.

### 13.6 S7 leak detection: parked frames are not roots

Taught by: Go 1.26 `isMaybeRunnable`, `setSyncObjectsUntraceable`, `findGoroutineLeaks`.

Why. Invariant F keeps every target alive from the waiter's own frame, so a reachability pass that
treats parked frames as roots never finds a leak. Go solves exactly this: a blocked goroutine's
stack is scanned only after one of its wait targets is marked, iterated to a fixed point, with
unknown wait reasons treated as runnable.

Check or change. S7.1 roots: running and queued coroutines, globals, and external sources
(`F_COUNTED`, `F_EXTERNAL`). A parked coroutine becomes a root when a target of one of its records
is reached; records from kinds without a known completer count as roots (conservative). Report the
rest as "leaked: waiting for X", which is more than the global deadlock check can say.

### 13.7 Liveness counted at the waiter instead of the source

Taught by: libuv (`uv_loop_alive`: active referenced handles), the reference's `active_event_count`
incremented at event start (`ZEND_ASYNC_INCREASE_EVENT_COUNT`, `libuv_reactor.c:1105` and 24 other
sites) and its last-chance drain (`scheduler.c:819-840`); Go `checkdead` (source-side: any timer).

Why. WM counts external waits per record (`F_COUNTED`, set when the target is `F_EXTERNAL` or
armed by `ARM_ON_WAIT`). Anything external that completes a target without being directly awaited
must set `F_EXTERNAL` on that target, or the deadlock check reports a false deadlock: a Future
resolved from a reactor callback, a signal, a thread, a child process, a foreign extension (FC 10
already lists the last). Source-side counting fails safe (a forgotten unref prevents detection);
waiter-side counting fails loud (a forgotten flag kills a live program with DeadlockError).

Check or change. Keep the reference's last-chance non-blocking reactor drain before declaring a
deadlock. In debug builds, at each deadlock decision compare `external_waits == 0` with the
reactor's own count of active referenced handles and report a mismatch.

### 13.8 Cross-thread completion (S10)

Taught by: Go `selectDone` CAS; Kotlin `tryResume`; folly and .NET registration races; Erlang
aliases.

Why. A record has no atomic state, and immediate unlink assumes the waker and the target vector
are on the waiter's thread. A record linked into a target that another thread completes would race
on the vector and on the claim (two targets of one `await_any` firing on two threads).

Check or change. State the invariant now: records link only into targets owned by the waiter's
thread; a cross-thread completion goes through a proxy event on the waiter's thread (the reference's
thread channel and remote Future resolve to a trigger event, `passive-edges-optimizer.md` section 3;
that the trigger is owned by the waiter's thread is an assumption, not checked), and a completion that arrives
after the waiter left is dropped at the proxy, as an Erlang alias drops a late reply. Debug: assert
the target's owner thread at link.

### 13.9 Stale record pointers and stack reuse

Taught by: Go `releaseSudog` field checks; Linux debugobjects for on-stack timers; CVE-2019-2215.

Why. A record pointer left in a vector by a missed unlink points into a stack that is popped, then
reused by the next wait in the same frame (same address, new record) or by another coroutine after
the context returns to the pool (D23). Both are silent: the callback fires into a live but wrong
record.

Check or change. Debug builds only: at link, scan the target vector for the record address
(catches a stale duplicate of the previous wait); a per-coroutine wait sequence number in the
record, checked by the callback against the coroutine's current one; fill the pooled stack with a
pattern on return. php-src's fibers carry ASan annotations (`__sanitizer_start_switch_fiber` in
`Zend/zend_fibers.c`); run the suite once with
`ASAN_OPTIONS=detect_stack_use_after_return=1`, which turns a write through a stale record into a
report.

### 13.10 The completion fast path never yields

Taught by: tokio cooperative budget (128 operations); Trio checkpoints; Kotlin `Channel.send`
docs.

Why. Phase 1 returns without switching when the target is complete. `while (true) { $x = $ch->recv(); ... }`
on a channel that a fast producer keeps non-empty, or a loop of `await` on completed Futures, never
yields; PHP has no preemption, so other coroutines and the reactor starve.

Check or change. Decide and record whether a completed await is a schedule point. A per-tick
budget (count phase-1 returns; after N, enqueue self and switch) costs one counter increment on the
fast path. N comes from a measurement, not from tokio's 128.

### 13.11 Bias of `await_any`

Taught by: Go `selectgo` random `pollorder`; tokio `select!` random unless `biased;`; Kotlin
`select` biased, `selectUnbiased` random.

Why. Phase 1 scans items in order and returns the first complete one; at wake, the first target to
notify wins. In a loop over the same set, item 0 wins every tie.

Check or change. Choose one and document it: biased (Kotlin's default, free, deterministic for
tests) or a rotating start index in phase 1 (one counter, no RNG). Test the chosen order.

### 13.12 Diagnostics the record can carry cheaply

Taught by: Go `waitreason`, lazy `waitsince`, "chan receive (nil chan)"; tokio-console
`LostWaker`.

Why and change.
- Wait duration: set a `wait_since` lazily, as Go does from the GC, from the deadlock check or the S7
  pass (first observation of a SUSPENDED coroutine with the field at 0), cleared at wake. The hot
  path pays nothing; the dump prints "waiting for X, 3 min". It needs 8 B in the coroutine
  (304 B in S3, 312 B with S9's `finally_handlers`; both stay in bin 320 by WM 2.1's sizing rule,
  not probed), or a side table owned by the observer.
- Never-wakeable waits: `await_any([])` and similar get a distinct `info` text (Go's "select (no
  cases)") and are reported at once.
- Lost waker: a coroutine that parks SUSPENDED with `wait == NULL`, no foreign registration and not
  one of the known zero-record parks (fiber, GC, `Async\suspend`) can never be woken. Debug builds
  assert this at park time.
- Debug: a successful return from `suspend()` of a single-result wait asserts `result` is not
  UNDEF; it catches a wake that set nothing.

### 13.13 Edge- or level-triggered cancellation

Taught by: Trio (level), asyncio (edge, with `uncancel` for nested timeouts).

Why. SF 2.8 delivers one `AsyncCancellation` and leaves `F_CANCELLED` set without re-raising at the
next await (edge-triggered, asyncio-like). Cleanup code that catches the cancellation and awaits
again can then wait forever; D16's 5 s deadline bounds this for shutdown only.

Check or change. Record the choice in S3.md with this consequence; it is a semantic decision for
Edmond, not a defect.

### 13.14 PHP code running inside a notify

Taught by: .NET `RunContinuationsAsynchronously`; asyncio `call_soon`.

Why. Record callbacks only enqueue, but heap subscribers share the vector (RFC finish handlers,
signal callbacks; WM 2.1 `dispose`) and a foreign extension's handler may run PHP code inline in
the middle of a notify. That code can cancel waiters in the same vector or release the target; the
cursor rule and the notify's reference cover the first two cases, nothing covers a nested wait or
a long-running handler.

Check or change. Debug: raise a counter around each record callback in `async_callbacks_notify`
and assert it is zero at every entry into PHP code and at every `suspend()`; document that heap subscribers that run PHP code must defer
it to a microtask.

### 13.15 Cancel-safety table per wait

Taught by: tokio `select!` docs; Asio `terminal`/`partial`/`total`.

Why. Users of `await_any`, timeouts and cancellation need to know what a cancelled `recv`,
`send`, `acquire` or `read` leaves behind.

Check or change. A table in the user docs: for each waiting API, whether a cancellation can lose
data, loses queue position, or leaves no trace.

## 14. Sources

Go (default branch, read 2026-10-02): [runtime2.go](https://github.com/golang/go/blob/master/src/runtime/runtime2.go),
[chan.go](https://github.com/golang/go/blob/master/src/runtime/chan.go),
[select.go](https://github.com/golang/go/blob/master/src/runtime/select.go),
[proc.go](https://github.com/golang/go/blob/master/src/runtime/proc.go),
[time.go](https://github.com/golang/go/blob/master/src/runtime/time.go),
[mgc.go](https://github.com/golang/go/blob/master/src/runtime/mgc.go),
[mgcmark.go](https://github.com/golang/go/blob/master/src/runtime/mgcmark.go),
[traceback.go](https://github.com/golang/go/blob/master/src/runtime/traceback.go),
[internal/sync/mutex.go](https://github.com/golang/go/blob/master/src/internal/sync/mutex.go),
[issue #40641](https://github.com/golang/go/issues/40641), [Go 1.26 release notes](https://go.dev/doc/go1.26).

Linux: [kernel/sched/wait.c](https://github.com/torvalds/linux/blob/master/kernel/sched/wait.c),
[fs/select.c](https://github.com/torvalds/linux/blob/master/fs/select.c),
[include/linux/poll.h](https://github.com/torvalds/linux/blob/master/include/linux/poll.h),
[CVE-2019-2215 RCA](https://googleprojectzero.github.io/0days-in-the-wild/0day-RCAs/2019/CVE-2019-2215.html),
[aio POLLFREE fix (CVE-2021-47505)](https://lkml.iu.edu/2112.1/05727.html).

Rust: [tokio sync/notify.rs](https://github.com/tokio-rs/tokio/blob/master/tokio/src/sync/notify.rs),
[sync/batch_semaphore.rs](https://github.com/tokio-rs/tokio/blob/master/tokio/src/sync/batch_semaphore.rs),
[macros/select.rs](https://github.com/tokio-rs/tokio/blob/master/tokio/src/macros/select.rs),
[task/coop/mod.rs](https://github.com/tokio-rs/tokio/blob/master/tokio/src/task/coop/mod.rs),
[util/linked_list.rs](https://github.com/tokio-rs/tokio/blob/master/tokio/src/util/linked_list.rs),
[tokio-console warnings.rs](https://github.com/tokio-rs/console/blob/main/tokio-console/src/warnings.rs),
[Notes on io-uring](https://without.boats/blog/io-uring/).

C++: [Understanding operator co_await](https://lewissbaker.github.io/2017/11/17/understanding-operator-co-await),
[Asio per-operation cancellation](https://www.boost.org/doc/libs/latest/doc/html/boost_asio/overview/core/cancellation.html),
[cancellation_type.hpp](https://github.com/boostorg/asio/blob/develop/include/boost/asio/cancellation_type.hpp),
[folly/CancellationToken.h](https://github.com/facebook/folly/blob/main/folly/CancellationToken.h).

Kotlin: [CancellableContinuation.kt](https://github.com/Kotlin/kotlinx.coroutines/blob/master/kotlinx-coroutines-core/common/src/CancellableContinuation.kt),
[channels/Channel.kt](https://github.com/Kotlin/kotlinx.coroutines/blob/master/kotlinx-coroutines-core/common/src/channels/Channel.kt),
[selects/Select.kt](https://github.com/Kotlin/kotlinx.coroutines/blob/master/kotlinx-coroutines-core/common/src/selects/Select.kt),
[selects/SelectUnbiased.kt](https://github.com/Kotlin/kotlinx.coroutines/blob/master/kotlinx-coroutines-core/common/src/selects/SelectUnbiased.kt).

Java: [LockSupport](https://docs.oracle.com/en/java/javase/21/docs/api/java.base/java/util/concurrent/locks/LockSupport.html),
[JEP 491 proposal](https://mail.openjdk.org/pipermail/jdk-dev/2024-November/009632.html).

.NET: [CancellationToken.Register](https://learn.microsoft.com/en-us/dotnet/api/system.threading.cancellationtoken.register),
[CancellationTokenRegistration.Dispose](https://learn.microsoft.com/en-us/dotnet/api/system.threading.cancellationtokenregistration.dispose),
[The danger of TaskCompletionSource](https://devblogs.microsoft.com/premier-developer/the-danger-of-taskcompletionsourcet-class/).

Python: [asyncio futures.py](https://github.com/python/cpython/blob/main/Lib/asyncio/futures.py),
[tasks.py](https://github.com/python/cpython/blob/main/Lib/asyncio/tasks.py),
[queues.py](https://github.com/python/cpython/blob/main/Lib/asyncio/queues.py),
[bpo-27585](https://bugs.python.org/issue27585), [gh-90155](https://github.com/python/cpython/issues/90155),
[gh-86296](https://github.com/python/cpython/issues/86296), [gh-81839](https://github.com/python/cpython/issues/81839),
[PR 98518](https://github.com/python/cpython/pull/98518),
[Trio _traps.py](https://github.com/python-trio/trio/blob/main/src/trio/_core/_traps.py),
[_parking_lot.py](https://github.com/python-trio/trio/blob/main/src/trio/_core/_parking_lot.py),
[_run.py](https://github.com/python-trio/trio/blob/main/src/trio/_core/_run.py),
[Trio reference-core](https://trio.readthedocs.io/en/stable/reference-core.html).

Others: [Erlang OTP 24.0](https://www.erlang.org/patches/otp-24.0),
[gen_server](https://www.erlang.org/doc/apps/stdlib/gen_server.html),
[Zig's new async I/O](https://andrewkelley.me/post/zig-new-async-io-text-version.html),
[libuv loop](https://docs.libuv.org/en/v1.x/loop.html), [libuv request](https://docs.libuv.org/en/v1.x/request.html).
