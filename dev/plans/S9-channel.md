# S9 notes, layer 3: Channel

Design note of the third S9 layer, 2026-10-08. The layer adds `Async\Channel`, `Async\ChannelException`
and the enum `Async\ChannelCloseReason`: message passing between the coroutines of one thread, unbuffered
(a rendezvous) or with a bounded buffer, with blocking and non-blocking ends, `foreach`, and the three
deadlock protections of TrueAsync. Names, signatures, messages and behaviour come from TrueAsync (`ext/async`
at `tests/lists/REFERENCE`, `1fdacf8`, file names alone below; its core, the fork `true-async/php-src`
`863f6dd90cf`, below `F:`); a departure is listed in section 8 with its reason. Behaviour marked "probed"
was run on a debug build of that fork with that `ext/async` on 2026-10-08 (scripts `h1.php`-`h14.php` in
`/mnt/project-files/s9/probes/s9.channel/`); the same build passes the 87 tests of `channel/`,
`edge_cases/015-deadlock-caught-still-terminates` and `stream/038`-`044`.

Layers 1 and 2 are built (`dev/plans/S9-scope.md`, `dev/plans/S9-context.md`). The channel needs no core
change: the pinned core (`async-core-io-2026-10-08` `662dfe91919`) has no channel type, and the fork's
`zend_async_channel_t` (`F:` `Zend/zend_async_API.h:1996-2002`) serves its thread pool and `ThreadChannel`,
which are S10's. The channel stands on S4's wait-record layer (`src/true_async_API.h`), which S3 and S4
shaped for it: the CHANNEL kind and its `abort` (`dev/plans/S3.md` 4.4 and the waits table, D26, D29), and
the `collector_target` slot S7 left for it (`dev/plans/S7.md` 4, 10).

## 1. What the layer implements

| Kind | In this layer | Elsewhere |
|---|---|---|
| Classes | `Channel` (final, `Awaitable`, `IteratorAggregate`, `Countable`), `ChannelException extends AsyncException` with `public ChannelCloseReason $reason`, `enum ChannelCloseReason: string` (`channel.stub.php`) | `ThreadChannel`: S10 |
| `Channel` | `__construct(capacity = 0, noProducerTimeout = 0, noConsumerTimeout = 0, hardTimeouts = false)`, `send(value, ?Completable)`, `sendAsync(value): bool`, `recv(?Completable): mixed`, `recvAsync(): Future`, `close()`, `isClosed()`, `capacity()`, `count()`, `isEmpty()`, `isFull()`, `getIterator()` | |
| Waits | the CHANNEL wait kind (section 3) | |
| Deadlock protection | the per-channel timers, the close of soft-timer channels at the global deadlock, the close when the owner scope goes (section 5) | |
| Collector | CHANNEL's `collector_target`, the walk of a channel's queued Futures; S7's channel case (`dev/plans/S7.md`, end of section 12) | |

## 2. The channel

**The structure**, TrueAsync's (`channel.h:52-111`) on our event:

```c
typedef struct {
	async_event_t event;              /* CLOSED once closed; `callbacks`: the Awaitable subscribers only */
	int32_t capacity;                 /* 0: a rendezvous */
	zval_circular_buffer_t buffer;    /* capacity > 0: the values, oldest first, grown on demand */
	zval rendezvous_value;            /* capacity 0: the one value in the slot */
	bool rendezvous_has_value;
	bool rendezvous_committed;        /* send() matched a receiver and returned: close() keeps the value */
	async_channel_queue_t receivers;  /* parked receivers and recvAsync() Futures, in arrival order */
	async_channel_queue_t senders;    /* parked senders, in arrival order */
	uint32_t reserved_receivers;      /* values promised to woken receivers that have not run yet */
	uint32_t reserved_senders;        /* free slots promised to woken senders */
	zend_object *close_exception;     /* the close's ChannelException, held: the Awaitable outcome */
	zval dropped_value;               /* a value a scope's close rolled back: get_gc reports it, free_obj releases it */
	/* section 5: the timeouts, the timer, its reason, the close reason, the owner-scope binding */
	zend_object std;
} async_channel_t;
```

**Reservations** are TrueAsync's rules (`channel.c:182-193`, `431-519`), kept whole because the 15 tests
`channel/073`-`087` check them: a wake only queues a coroutine, so the value or the slot it was woken for
is held back from everyone else, the coroutine that gave it included, until the woken coroutine runs and
takes it. `recv()`, `recvAsync()`, `foreach` and `sendAsync()` take only an unpromised value or slot
(`channel_has_free_value()`, `channel_has_free_slot()`). A rendezvous sender puts its value into the slot
and parks until a receiver takes it (the "delivering" sender, `channel.c:1142-1162`); the receiver's take
wakes it and grants the freed slot to the oldest sender waiting for one in the same call
(`channel.c:497-519`). A woken waiter that leaves without spending its reservation (a cancellation)
hands it to the next waiter of its side (`channel.c:766-779`). `close()` rolls back a rendezvous value
nobody was told was delivered and keeps a committed one for its receiver (`channel.c:577-585`).

**The buffer** holds zvals in a ring that starts small and doubles up to `capacity`, from `emalloc`.
TrueAsync allocates `capacity + 1` slots rounded up to a power of two at construction, from the persistent
allocator (`channel.c:1101-1108`, `internal/circular_buffer.c:97-127`): probed (`h6.php`),
`new Channel(1 << 24)` under `memory_limit=64M` grows `memory_get_usage()` by 0 KB and the RSS past 200 MB, and `new
Channel(2147483647)` ends the request with "Out of memory". Ours costs nothing until values come and
counts against `memory_limit` (section 8, item 3). The ring is our own type, a sibling of the pointer ring
`src/internal/circular_buffer.c` for zvals (`src/internal/zval_circular_buffer.c`), as TrueAsync's
`zval_circular_buffer` is of its `circular_buffer`.

**Methods**, as TrueAsync (`channel.c:1067-1343`): the constructor refuses a capacity or a timeout outside
`0..INT32_MAX` with `ValueError` "must be between 0 and 2147483647"; `send()` and `recv()` on a closed
channel throw `ChannelException` whose message and `reason` name why it closed (`channel.c:83-104`), and
`recv()` drains the values left before it throws; `sendAsync()` returns false on a closed or full channel;
`count()` includes the values promised to woken receivers, `isEmpty()` is `count() === 0`, `isFull()` reads
the buffer; `close()` twice does nothing the second time and the first reason stays (`channel/062`). A second
`__construct()` throws `Error`: on the reference it drops the held values and leaks them (probed `h5.php`:
`count()` 0 after it, two leaked strings in the debug build's report), section 8, item 4. `clone` and
`serialize()` are refused, as the reference (probed `h10.php`; the stub's `@not-serializable`).

**Where it may be called.** `send()`, `recv()` and `foreach` wait, so they refuse as every wait of ours
does: in scheduler context and once async is off (`THROW_IF_UNAVAILABLE`, `php_true_async.h:97-101`).
TrueAsync's `ENSURE_COROUTINE_CONTEXT` launches its scheduler first (`channel.c:49-55`, `1039-1048`); ours
runs main as a coroutine from the first opcode (layer 1 note, section 3), so `channel/071` and
`edge_cases/015` need nothing more. `sendAsync()`, `recvAsync()`, `close()` and the readers wait for nothing
and need no running coroutine. In a `Coroutine::finally()` handler both kinds work on the reference (probed
`h14.php`); ours runs those handlers in a coroutine too (layer 1 note, section 7).

## 3. Waiting: the CHANNEL kind

**The record is the queue entry.** A parked `send()` or `recv()` links `waker.records[0]` of the waiting
coroutine with the CHANNEL kind, and a cancellation token `waker.records[1]` with S5's token kind
(`async_await_token_link()`, `src/await.h`). The queue of its side holds a pointer to that record, so a
parked send or receive allocates nothing: TrueAsync allocates its waiter (`channel.c:692`), D29 asked for
one allocation fewer, and the waker records live in the coroutine, not on the frame, since S3
(`src/true_async_API.h:376-389`), so a bailout that unwinds the frame leaves the entry valid. The record
is in the channel's queue, not in a callbacks vector, and the channel's own vector holds only the
Awaitable subscribers of section 4. `close()` walks the two queues, as TrueAsync's
`channel_wake_all()` does before its notify (`channel.c:529-561`).

The record's flags word carries two bits of the kind, TrueAsync's waiter fields (`channel.c:110-116`):
RESERVED, set by the wake that promised the coroutine a value or a slot, and DELIVERING, a rendezvous
sender parked on its own value; as built, a third, SENDER, tells `abort` which queue and which counter
the record belongs to (TrueAsync's frame knows its queue, `abort` has only the record). The frame reads
RESERVED after its suspend and spends or hands on the reservation, as `channel_wait_for()` after
`ZEND_ASYNC_SUSPEND()` (`channel.c:719-786`).

**The kind's operations:**

- wake: the channel's code, not a notify. A value or a slot for the oldest receiver or sender with no
  reservation sets RESERVED and enqueues it, and the record stays in its queue (`channel.c:437-470`,
  `497-519`), even when its coroutine is already queued by a cancel, whose frame then hands the
  reservation on (`channel.c:766-779`). A rendezvous receiver's take acknowledges the delivering sender:
  its record leaves the senders' queue and its coroutine is enqueued (`channel.c:472-492`), so a close
  before it runs does not fail a message the receiver holds (`channel/082`), and the same call grants the
  freed slot to the oldest sender still queued (`channel/085`). A close takes every record out of the
  queues but a reserved receiver's, whose value is still there and which takes it, and enqueues each with
  the close's `ChannelException` (`channel.c:521-558`); a reserved sender is failed too, and its frame
  finds the reservation unspent and, the channel being closed, hands nothing on. A close that reaches a
  coroutine a cancel already queued puts its exception on top of the cancellation, which
  `async_scheduler_enqueue()` does for a queued coroutine (`src/scheduler.c:1435-1440`; the stacking,
  `189-219`), as TrueAsync's (`coroutine.c:811-815`): that is how `channel/048`, `050`-`053` and `063`
  see `SCOPE_DISPOSED` after their scope's cancel, which queues its coroutines before it notifies
  (section 5);
- `unlink`, the enqueue's unlink of a wake by something else (a cancel, a token), leaves the record linked
  and in its queue. Every CHANNEL record stays linked (`event` set) from its wake until its frame takes it
  out, right after its suspend returns and before it runs anything else; whether it is still in its queue
  then is TrueAsync's `was_queued` (`channel.c:719-743`): a rendezvous sender still queued was not
  delivered, and withdraws its value on an error. This is TrueAsync's waiter, which stays registered in
  its waker until the frame cleans it, and the waits table of `dev/plans/S3.md` (690) planned it for the
  value wake: "the channel's wake removes the vector entry and leaves the queue entry … the frame removes
  it after resume". So the enqueue's unlink of the whole wait removes the token record and leaves the
  CHANNEL one. This is an exception to D26 and to S4's `unlink` contract, which clear every record at the
  enqueue (Edmond 2026-10-08, DECISIONS, question 2 of section 10). As built (S9.17): the record is linked
  by `async_wait_link_outside()`, which sets `ASYNC_CALLBACK_F_FRAME_UNLINKS` instead of pushing it into a
  vector, and `async_wait_record_unlink()` leaves a record with that flag. The record also stays linked
  because `abort` runs only for linked records (`async_wait_abort()`, `src/true_async_API.c:253-264`), and
  a coroutine woken and never run again is the case `abort` exists for (below). The callers of
  `async_wait_is_empty()` hold: `src/io_provider.c:297` asserts on a running coroutine, which has removed
  its record by then; `src/collector.c:1017` looks only at suspended coroutines; `src/coroutine.c:411`
  aborts the wait of a frame a bailout unwound, which is wanted. A woken coroutine that has not run yet
  shows the channel line in `getAwaitingInfo()`; the timer's refresh counts it as waiting while it is
  queued without a reservation, as TrueAsync's (`channel.c:401-422`), and arming refuses a closed channel
  (`channel.c:365-366`);
- `abort`, for a frame that never runs again (a bailout's transfer U4, `src/scheduler.c:1842`, `1863`,
  `1873`; RSHUTDOWN's U6, `2335`; the finalize after a bailout, `src/coroutine.c:405-413`): it runs inside
  a bailout's unwinding or in RSHUTDOWN, where it may neither enqueue nor start PHP code
  (`src/scheduler.c:2312-2317`, `2361-2362`), so it only gives back what the frame held, for a record in
  any state, woken or not, queued or not. A RESERVED record returns its reservation and nobody is woken;
  the record leaves its queue if it is there; `abort` clears `event` itself, so the generic unlink after
  it (`src/true_async_API.c:265`) and the finalize's second look (`src/coroutine.c:411`) find nothing; the
  timer is disarmed when no unreserved waiter is left, and never armed, since every abort site is
  terminal. A DELIVERING sender's value stays in the slot for whoever receives next, and `free_obj`
  releases it if nobody does, as TrueAsync after a bailout. As built, `async_wait_end()` aborts such a
  record too, at the next wait of a coroutine whose frame a caught bailout unwound: the reservation
  returns there without a hand-on, so a waiter parked behind it waits for the channel's next send, receive
  or close. Shutdown functions run after U4 and the finalize, so this passes the test `dev/plans/S3.md`
  4.4 names: a receiver woken with a value and cancelled before it runs, the value reaching the next
  receiver (the frame hands the reservation on); the same with a bailout in place of the cancel, the value
  reaching a `recv()` in a shutdown function (the reservation was returned). A `recvAsync()` Future queued
  behind the aborted receiver stays pending, and such a `recv()` takes the value ahead of it: serving the
  Future there would enqueue inside the bailout;
- `info`: TrueAsync's line (`channel.c:825-836`) with the reservations named, `Channel(capacity=0,
  receivers=1, senders=0, reserved receivers=0, reserved senders=0)`, for `getAwaitingInfo()` and the
  deadlock report;
- `collector_target`: section 6.

**The wait.** Every `send()` and `recv()` is TrueAsync's retry loop (`channel.c:1113-1245`): take a free
value or slot, or the reserved one; else throw if closed; else park and try again. A wake by something
else than the channel (a cancellation, a token) comes back with the exception, which the frame throws
after its cleanup. A token that already completed throws `OperationCanceledException` at the entry, as
`CANCELLATION_TOKEN_PREPARE` (`channel.c:57-61`), through `async_await_token_check()`; a `Timeout` token
arms for the wait (`async_await_token_arm()`). The values the cleanup drops, a withdrawn rendezvous value,
are released after the queues are consistent again: their destructors run PHP code, and one that sends on
the same channel must find it whole (section 8, item 7). A close the owner scope makes runs inside the
scope's own walks (its cancel's loops, `src/scope.c:581-587`, the error route's, `773-779`, and its free,
`366`), where no PHP code may run (`src/scope.c:579-580`, `337-339`): the uncommitted rendezvous value
that close rolls back (`channel.c:581-585`) is to move to a `dropped_value` field, out of the slot, so no
later `recv()` receives it, and `get_gc` is to report it and `free_obj` to release it (S9.19). A close
runs once, so the field holds at most one value. As built in S9.17, `channel_close()` hands that value
to its caller, and `close()` and the destructor release it once the queues are consistent. TrueAsync
releases such values in place (`channel.c:738-742`, `581-585`).

Kept as TrueAsync: a `send()` whose value was taken and whose coroutine is cancelled before it runs reports
the cancellation (`channel.c:745-752`; clearing it leaves the coroutine marked cancelled with nothing to
raise it). The queues keep the arrival order with an O(n) removal, as TrueAsync's `memmove`: a cancellation
of each of N waiters costs O(N^2) moves in all, which S9.21 measures.

## 4. `recvAsync()`, the iterator, the channel as an Awaitable

**`recvAsync()`** returns a Future: completed at once with a free value, failed at once on a closed channel,
else pending in the receivers' queue (`channel.c:1247-1292`). The queue entry is a small heap waiter, as
TrueAsync's Future payload (`channel.c:118-139`), holding the future event borrowed, and a subscriber in that
event's vector whose `dispose` takes the waiter out of the queue when the Future goes before the channel
serves it: the pattern of S6's `signal()` Future (`src/os_signal.c:78-85`, `680-740`). Every completion of
the Future disposes that subscriber too (`async_callbacks_free()` at `src/future.c:663`), so the wake and
the close take the waiter out of the queue before they complete or reject the Future, the `dispose` alone
frees the waiter, and its removal accepts a waiter no longer queued. So a dropped Future
takes no value (`channel/066`-`068`), and a loop of `await_any_or_fail([$channel->recvAsync(), timeout(1)])`
leaves nothing behind in the queue. A Future waiter reserves nothing: the wake completes it in place
(`channel.c:448-460`).

**`foreach`** parks as `recv()` (`channel.c:974-1012`): an explicit `close()` ends the loop silently, any
other close or a cancellation propagates out of it, a by-reference loop throws "Cannot iterate channel by
reference" (probed `h10.php`), the key is null. `getIterator()` returns an `\Iterator` over the same handler
(`zend_create_internal_iterator_zval()`). TrueAsync's returns the raw iterator wrapper and fails its own
return type: probed (`h11.php`), "Return value must be of type Iterator, __iterator_wrapper returned", a
fatal error; section 8, item 5.

**An Awaitable.** The channel's close notifies its event with the close's `ChannelException`, and a channel
is accepted where TrueAsync accepts it: probed (`h4.php`), `await_any_or_fail([$channel])` fails with
"Channel is closed" once the channel closes, and `$scope->awaitCompletion($channel)` ends with
`OperationCanceledException` at the close; `await($channel)` is a `TypeError`, since `await()` takes a
`Completable`. Ours accepts only coroutines and Futures as `await_*` items (`src/await.c:717-747`) and as
tokens (`src/await.c:85-110`, `144-182`), and treats every event that is neither a coroutine nor a
`Timeout` as a future event: its reference is the event's own counter and its release frees the event
(`src/await.c:50`, `61`), which for a channel is embedded in the object. So the channel gets a type bit
in the event's flags, as `Timeout`'s (`ASYNC_TIMEOUT_F_TIMEOUT`, `src/timeout.h:26-30`), and a branch at
each place that reads the type:
`async_awaitable_addref()` and `async_awaitable_release()` (the object's counter, `src/await.c:45-63`),
which covers every item and token hold (`src/await.c:387`, `469`, `1446-1448`, `src/scope.c:1537`, `1567`,
`1655`, `1723`); `await_outcome()` (`src/await.c:105-110`), pending until the close and the held
`close_exception` after it; the class gate `await_trigger_of()` (`src/await.c:732-733`); the info lines
`token_record_info()` and `await_record_info()` (`src/await.c:258-270`, `591-598`), which say "channel";
and `await_record_report_held_target()` (`src/await.c:275-287`), which reports a channel item or token
with the same function as CHANNEL's `collector_target` (section 6), not as a Future. The tokens of
`delay()`, `Future::await()` and `signal()` are `Completable` (`src/true_async.c:314`, `src/future.c:1305`,
`src/os_signal.c:767`), which a channel is not.

## 5. Closing and the deadlock protections

**`close()` and the destructor.** `close()` closes with `EXPLICIT`. The object's destructor closes with
`DISPOSED` (`channel.c:909-916`): a parked waiter holds the channel through its frame, so the destructor
meets only Futures and Awaitable subscribers, or runs at the request's end. `free_obj` releases the values
and, when no destructor ran (after a fatal error, `main/main.c` skips them), detaches the queued Future
waiters without completing them: TrueAsync relies on the destructor's close there (`channel.c:127-130`), and
a Future freed after the channel would take itself out of a freed queue (section 8, item 8).

The three protections, TrueAsync's (`docs/channel-deadlock-protection.md`; its constructor defaults are
stale there: the stub and the code default to 0, `channel.stub.php:52-57`, `channel.c:1069-1072`):

**1. The per-channel timer** (`channel.c:347-422`). While a receiver without a reservation waits, a
`noProducerTimeout` timer is armed; while only senders wait, `noConsumerTimeout`; it is withdrawn when its
side drains and armed afresh from zero, and its fire closes with `NO_PRODUCERS` or `NO_CONSUMERS`. A hard
timer (`hardTimeouts: true`) is a Timer op on the reactor's waits (`async_io_event_submit()`), so a script
that ends by itself waits for it, as `Scope::disposeAfterTimeout()`'s (`src/scope.c:1294-1347`). A soft timer,
the default, is one of the reactor's own ops (`async_reactor_submit_own()`, `src/reactor.h:126-128`): it
fires on time while anything else keeps the loop running (probed `h9.php`: `NO_PRODUCERS` at about 100 ms
while another coroutine sleeps 300 ms), and keeps nothing from the global deadlock, as libuv's hidden
timer. The refresh counts the queued `recvAsync()` Futures as receivers, as TrueAsync's
(`channel.c:401-422`), and runs where TrueAsync's does: on the exit of a parked `send()` or `recv()`
and at a wake or a close, not when a Future is queued or disposed (`channel.c:1278-1288`, `297-320`), so a
channel with only a pending Future arms no timer. It arms nothing once async is off
(`ZEND_ASYNC_IS_ACTIVE`, `php_true_async.h:81-87`): a destructor run from `released_values` may still
call `recvAsync()`.

The timers go before the reactor does. After a fatal error no destructor runs and `free_obj` comes in
`zend_deactivate()`, after RSHUTDOWN, while our reactor's RSHUTDOWN asserts that its owners withdrew
their own ops and frees the timer heap (`src/reactor.c:637-643`). So RSHUTDOWN walks
`ASYNC_G(deadlock_channels)`, the registry of soft-timer channels below, after
`async_scheduler_request_shutdown()`, whose aborts may disarm, and before
`async_reactor_request_shutdown()` (`src/true_async.c:227`, `235`), withdraws each timer and destroys the
registry; a channel freed later finds no timer and no registry entry. A hard timer is on the reactor's
waits, which the reactor orphans itself (`src/reactor.c:613-615`). A forked child's rebuild drops the
parent's waits unrun (`src/reactor.c:516-520`; own ops are resubmitted, `534-564`), so for a hard timer
the channel calls `async_reactor_check_fork()` and tests whether the timer is still armed, as
`Scope::disposeAfterTimeout()` does (`src/scope.c:1319`, `303-306`), before it keeps one; TrueAsync's
refresh keeps a timer it holds (`channel.c:414-417`), which never fires in the child.

**2. The global deadlock.** A soft timer's channel is registered in `ASYNC_G(deadlock_channels)`, as
TrueAsync's (`php_async.h:133-136`, `channel.c:375-380`). Where our loop finds nothing runnable and no wait
of the reactor (`src/scheduler.c:905-914`), before the DeadlockError it closes every registered channel
with `DEADLOCK` and runs the queue again; the DeadlockError comes only when none was closed, as
`resolve_deadlocks()` (`scheduler.c:750-757`; `channel/047`, `061`). TrueAsync also skips its loop's wait when
only hidden events are alive (`scheduler.c:1560-1568`); ours reaches that branch with no wait at all, since
an own op is not a wait (`async_reactor_has_waits()`). S7's collector, which finds partial deadlocks at the
idle point, works beside it (section 6); `dev/plans/S7.md` 9 left this choice to S9, and the layer keeps
TrueAsync's.

**3. The owner scope.** At construction the channel subscribes to the event of the current scope
(`async_scope_current()`, `src/scope.c:133`): the current coroutine's, or the global scope at the top level,
as `channel_bind_to_owner_scope()` (`channel.c:611-627`), which binds nothing when there is no scope or it
is closed (`channel.c:617`; ours tests `ASYNC_SCOPE_F_CLOSED`, `src/scope.h:45`). Ours has no scope after
the global scope goes (`src/scope.c:1919`), where a destructor run from `released_values`
(`src/true_async.c:244`) can still construct a channel. The subscriber is a callback inside the channel,
with a `dispose` that clears its pointer when the scope goes first (`channel.c:592-638`); the channel's
free leaves the scope's vector when the channel goes first (`channel/056`). `close()` never leaves the
vector: `async_callbacks_free()` walks it with swap removals (`src/true_async_API.c:163-196`), and a
subscriber that removed another would be skipped there with a dangling pointer.

The channel closes with `SCOPE_DISPOSED` when its scope is cancelled or destroyed, not when it completes
(Edmond, 2026-10-08, DECISIONS): at the transition, not by reading the scope's flags at each notify.

- a notify that carries an error: the cancel (`src/scope.c:592`) and the error route (`783`), both after
  `ASYNC_SCOPE_F_CANCELLED` is set (`577`, `770`);
- the cancel or dispose of a scope that completed or was cancelled before (`src/scope.c:547-570`; it is
  taken whenever `scope_is_completed()` holds, which a cancelled scope does, `160`; also reached through a
  parent's cancel and the error route, `582`, `774`): that branch sets `ASYNC_SCOPE_F_CLOSED` and notifies
  nothing, and stays so. It closes the scope's bound channels itself, by walking its event's vector for
  the channel's subscribers, recognised by their callback function as the collector's hand-out needs them
  (section 6), and not by a notify. The walk keeps the notify's protocol without calling the other
  subscribers: the vector marked `ASYNC_CALLBACKS_F_NOTIFYING` with its cursor, so a record a close's wake
  removes (an `awaitAfterCancellation()` token on that channel, through the enqueue's unlink) does not make
  it skip a channel (`src/true_async_API.c:84-105`), and the scheduler-context flag set around it, as
  `async_callbacks_notify()` sets it (`src/true_async_API.c:127-130`). The other subscribers, an `awaitAfterCancellation()` waiter of an already
  cancelled scope among them (`src/scope.c:1587-1591`), must not be woken there. TrueAsync has the
  same silent branch (`scope.c:964-971`) and closed the channel at the completion before;
- the scope's free (`src/scope.c:366`), through the subscriber's `dispose`: the free of a scope without an
  object (the `await_*` iterator's scope, `src/await.c:1249`; a finally run's, `src/coroutine.c:624`) comes at
  its last member's end, so for those the completion is the destruction, as in TrueAsync. The `dispose`
  clears its pointer first and closes only while async is on (`ZEND_ASYNC_IS_ACTIVE`): RSHUTDOWN's free of
  the request's scopes (`src/scope.c:1917`) and `scope_object_free()` in `zend_deactivate()` leave the
  channel to `free_obj` (item 8 of section 8).

A completion notify (`src/scope.c:275`) without an error never closes, whether or not the scope was
cancelled before, so a channel made in a cancelled scope (the global scope after an unhandled error)
closes at that scope's next cancel, dispose or error route, or at its free, where TrueAsync's closes at the
next member's end. Probed on the reference with
its subscriber and its scope changed to this rule: the 87 tests of `channel/`, `edge_cases/015` and the 57
of `scope/` pass; `h1.php` prints "closed after the scope's last coroutine ended: false"; a `dispose()`, a
`cancel()` and a parent's cancel of a completed scope close the channel; `disposeSafely()` closes the
zombies' channels, as `channel/057` pins for `allowZombies()` (Sage's probes `f1a`-`f7`,
`/tmp/claude-0/sage-chan/p/`, copied to `/mnt/project-files/s9/probes/s9.channel/`).

The cost, section 8, item 11: a channel handed out of a scope whose coroutines ended, whose producer
returned without `close()`, and which live code still holds (an object's field) keeps its receiver parked;
the collector does not see it, since the channel is reachable, and only the channel's timers or the global
deadlock end the wait. TrueAsync's receiver gets `SCOPE_DISPOSED` when the scope runs empty (`h13.php`).

The global scope lives until RSHUTDOWN, so a channel made at the top level is still open in shutdown
functions and in destructors at shutdown; probed (`h12.php`), the reference's is open there too.

## 6. Garbage collection and the async object collector

**PHP's collector.** `get_gc` reports the buffered values and the rendezvous value (`channel.c:857-877`): a
channel whose buffer holds the channel is collected (probed `h3.php`: 1 000 such channels, `collected:
1000`, 0 KB kept). The queues own nothing: a waiter's record belongs to its coroutine, and a Future waiter's
event to its Future. A parked waiter holds the channel through its frame, which the coroutine's `get_gc`
does not report, so a channel with parked waiters is never garbage.

**The async object collector** (S7). A coroutine parked in `send()` or `recv()` waits for the channel:
whoever holds the channel can send, receive or close. CHANNEL's `collector_target` reports the channel
object as the target (`dev/plans/S7.md` 4: "a channel with no holder but its receivers is the plan's channel
without senders"), and as outside sources what ends the wait without a holder: an armed timer, soft or hard,
fires by itself; the owner scope can close the channel, so for a channel bound to a scope other than the
request's two the walk reports what `async_scope_collector_reach()` reports (S7.7), starting from the bound
scope: the holders of its object and of its ancestors' objects, armed dispose timers and iterator
coroutines; and, for a scope without an object or a cancelled one, every live coroutine of its subtree,
since the last member's end frees it and the free closes the channel. The error route stays left out at
every level, as agreed for S7 (`dev/DECISIONS.md`, 2026-10-07): the route's hand-out
(`scope_hand_out_found()`, `src/scope.c:702-728`) marks the found waiters of the scope's bound channels
handed out, which needs the channel's subscriber to be recognisable in the scope's vector; in production a
waiter on a channel bound to a scope whose live member later throws can get a "can never wake" warning, the
case S7 accepted. A channel item or token of `await_*` reports through the same function. A Channel object is read through its
`get_gc` and the future events of its queue, as `collector_object_references()` reads a Future's
(`src/collector.c:597-615`): a coroutine awaiting a `recvAsync()` Future is live while the channel is. The
global deadlock's close of soft channels is one of the routes S7 leaves out (`dev/plans/S7.md` 2), so it
marks the waiters it wakes handed out before it closes, as the route's hand-out of a scope's subtree
(`src/collector.h:110-115`). This is mostly moot, since every registered channel has an armed soft timer, which the walk counts as
outside. Every other wake of a channel waiter tells the fuzz oracle: a send, a receive or a close by
running code calls `async_collector_check_event_wake()` for each queued waiter it wakes, as a Future's
wake does (`src/future.c:714`), and a close by running code calls
`async_collector_check_records_wake(&channel->event.callbacks, NULL)` before it notifies the Awaitable
subscribers, as a Future and a scope do before theirs (`src/future.c:658`, `src/scope.c:590`). A timer's
close runs in scheduler context, as an outside source. TrueAsync detects none of this: a receiver on a channel nobody else
holds waits until the global deadlock (probed `h8.php`).

## 7. Steps

- S9.16 This note.
- S9.17 The list block for layer 3 in `tests/lists/S9.txt` (section 9), with `--XFAIL--` naming S9.17,
  S9.18 or S9.19; the channel, its buffer, `send()`, `sendAsync()`, `recv()`, `close()` and the readers, the
  reservations, the CHANNEL kind with its `unlink`, `abort` and `info`, cancellation tokens, the destructor
  and `free_obj`, `ChannelException` and `ChannelCloseReason` (sections 2, 3, 5). As built: until S9.18,
  `recvAsync()` and `getIterator()` throw "not implemented yet", and `async_await_awaitable_of()` refuses a
  channel, which `Scope::awaitCompletion()` and `awaitAfterCancellation()` would otherwise read as a future
  event; the channel's type bit comes with this refusal.
- S9.18 `recvAsync()`, `foreach` and `getIterator()`, the channel as an `await_*` item and a token
  (section 4).
- S9.19 The per-channel timers, the close at the global deadlock, the owner-scope binding and the close of
  a completed or cancelled scope's channels (section 5), CHANNEL's `collector_target` and the walk of the queued Futures, S7's channel case (sections 5,
  6).
- S9.20 Layer review: the Critic over S9.17-S9.19, coverage of `src/channel.c`, Mull on the layer's diff,
  the fuzz oracle over 100 seeds of the layer's tests and `collector/`, the measurements of section 9.
- S9.21 Security pass by `dev/SECURITY.md`: the capacity against `memory_limit`, the O(N^2) cancellations
  of section 3, destructors of dropped values during a close, a Future freed after its channel, a
  `recvAsync()` Future with `map()` children rejected by the global scope's cancel walk (its drain
  coroutine joins the scope being cancelled, `src/future.c:550`), PHP's GC started inside a scope's walk.

## 8. Departures from TrueAsync

1. **A parked send or receive allocates nothing** (section 3): the waiter is the coroutine's waker record;
   TrueAsync allocates one (`channel.c:692`). D29.
2. **The close walks the queues; the channel's event vector holds only Awaitable subscribers** (section 3).
   TrueAsync adds each waiter to both (`channel.c:700-707`). Same behaviour, one place per waiter.
3. **The buffer grows on demand from `emalloc`** (section 2): a large capacity costs nothing until filled and
   counts against `memory_limit` (`h6.php`).
4. **A second `__construct()` throws `Error`** (section 2): the reference loses and leaks the values
   (`h5.php`).
5. **`getIterator()` returns an `\Iterator`** (section 4): the reference's is a fatal error (`h11.php`).
6. **`getAwaitingInfo()` names the channel wait** with the kind's line, as for every wait of ours; the
   reference returns an empty array for it (probed `h7.php`).
7. **Values a close or a withdrawn send drops are released after the queues are consistent** (section 3);
   TrueAsync releases them in place, inside its walks.
8. **`free_obj` detaches the queued Future waiters a destructor did not close** (section 5).
9. **A coroutine parked on a channel nobody else can reach is found by S7's collector** (section 6): the
   partial deadlock S7 adds; the reference waits for the global deadlock (`h8.php`).
10. **A forked child does not keep a timer the rebuild dropped** (section 5); the reference's never fires
    there.
11. **The owner scope closes its channels when it is cancelled or destroyed, not when it completes**
    (section 5, Edmond 2026-10-08): TrueAsync's code closes on every notify of the scope; its documentation
    promises the close "when that scope is disposed or cancelled". The cost is the parked receiver of
    section 5. A cancel or a dispose of a completed or cancelled scope closes its channels, where
    TrueAsync's closed them at the completion.
12. **A channel made in a cancelled scope closes at the scope's next cancel, dispose, error route or free**
    (section 5); the reference closes it at the scope's next member's end.
13. **A rendezvous send whose wait fails before it parks withdraws its value** (S9.17, the Critic): a
    `Timeout` token whose deadline passed after the entry's check refuses the delivering wait, and the
    value would stay uncommitted in the slot for a receiver while `send()` throws; the reference leaves it
    there (`channel.c:688-690`, `1156-1160`).
14. **`send()` and `recv()` refuse in scheduler context and once async is off even when they would not
    wait** (section 2), as every wait of ours; the reference fails only an actual park.

Kept as TrueAsync and noted: a CHANNEL record linked from its wake until its frame takes it out, its queue
membership TrueAsync's (an exception to our D26); the reservation rules and the arrival order with its
O(n) removal; constructor defaults of 0 (no timers); `count()` counting promised values; a cancellation
after delivery reported by `send()`; the first close reason staying; the global deadlock closing soft
channels before it raises DeadlockError; a top-level channel open until the request's end (`h12.php`).

## 9. Tests and measurements

**List** `tests/lists/S9.txt`, a block for layer 3, frozen in S9.17; 93 reference tests, each passing on the
reference build above (2026-10-08):

- `channel/001`-`087` but `058` and `059`, which need `TaskGroup` and go to a new `tests/lists/S9.excluded`
  as `component:S9 (TaskGroup)`: 85 tests, a group no list has ported yet;
- `edge_cases/015-deadlock-caught-still-terminates` from `tests/lists/S3.excluded` and `stream/038`-`044` from
  `tests/lists/S6.excluded` (`component:S9 (Channel)`), which leave those files in the same commit.

By what each uses: `recvAsync()` (`019`, `020`, `029`, `030`, `066`-`068`, `077`) and `foreach` over a channel
(`008`, `017`, `039`, `040`, `070`, `079`) name S9.18, 14 tests; a timer (`041`, `042`, `044`, `046`, `047`,
`071`, `072`) or a scope's close (`048`-`057`, `061`-`063`) name S9.19, 20 tests; the other 59 need only
S9.17. As built, `044`, `049`, `056` and `062` pass with S9.17 alone and carry no `--XFAIL--`: each checks
that something does not happen (a timer firing early, a completion closing, a free leaving a dangling
subscriber, a later close replacing the reason), which holds while nothing exists to do it; they prove
S9.19's work only once it is built, so S9.19 does not count them as its tests: 63 pass in S9.17, 14 wait
for S9.18 and 16 for S9.19. TrueAsync's `fuzzy-tests/` are not ported, as no fuzzy test is.

**Own tests**, written in the step that needs them:

- S9.17: `h3.php` (a channel holding itself collects), `h5.php` (a second `__construct()` refused, nothing
  leaked), `h6.php` (`new Channel(1 << 24)` grows nothing; filling past `memory_limit` ends with the usual
  fatal error), `h10.php`, `h7.php` (`getAwaitingInfo()` names the wait); a receiver woken with a value and
  cancelled before it runs, the value reaching the next receiver, and the same with a bailout
  (`dev/plans/S3.md` 4.4); a rendezvous sender cancelled while delivering withdraws its value, and after a
  bailout the value it was delivering reaches a `recv()` in a shutdown function; a reserved sender failed by
  a `close()` from the coroutine that freed its slot; a dropped value whose destructor sends on the channel during a close; `send()` and `recv()`
  refused in scheduler context;
- S9.18: `h11.php` (`getIterator()` iterates), `h4.php` (an `await_*` item, an `awaitCompletion()` token,
  `await()` refused); 10 000 rounds of `await_any_or_fail([$channel->recvAsync(), timeout(1)])` with memory
  flat; a pending `recvAsync()` Future at a fatal error's end, freed after the channel, on ASAN;
- S9.19: `h1.php` (open after the scope's last coroutine ended), `h13.php` rewritten (the receiver outside
  the scope ends in the global deadlock), Sage's `f2a`-`f2d` (`dispose()`, `cancel()`, a `finally` handler,
  a parent's cancel of a completed scope close the channel), `f5` (a channel made in the cancelled global
  scope stays open past its members' ends); a second `dispose()` of a cancelled scope with an
  `awaitAfterCancellation()` waiter wakes nothing but the channel, `f7` (`disposeSafely()` closes the zombies' channels); the parked receiver of section
  8, item 11; a scope freed after a fatal error with a pending `recvAsync()` Future, on ASAN; `h9.php` (a soft timer fires while another coroutine sleeps);
  `h12.php`; a soft timer armed with a `recvAsync()` Future and a parked receiver at a fatal error's end,
  on debug and ASAN; a hard timer in a forked child; for the collector, a receiver on a channel only it holds found by
  `get_deadlocked_coroutines()`, and not found: a channel a running coroutine holds, an armed timer, a bound
  scope whose object a running coroutine holds, a `recvAsync()` Future awaited while the channel is held.

**Measurements** (S9.20, `dev/BENCHMARKS.md`), against the reference: B15, a rendezvous ping-pong of 100 000
messages between two coroutines, instructions and allocations per message (D29's allocation, `dev/plans/S3.md`
12); B16, 4 producers and 1 consumer through a channel of capacity 64, 100 000 values.

**Core dependencies**: none.

## 10. Questions for Edmond

1. **Which scope events close a channel.** Decided by Edmond, 2026-10-08: as TrueAsync's binding, but the
   cancel and the destruction of the scope close the channel, its completion does not, and the
   collector deals with the rest. Checked by the Critic and the Sage; the rule is section 5.

2. **May a CHANNEL record stay linked from its wake until its frame removes it.** Decided by Edmond,
   2026-10-08: yes, as TrueAsync and as the waits table of S3 planned; an exception to D26 for the CHANNEL
   kind (section 3). The final Critic found the first wording ("stays queued") wider than TrueAsync: its
   close and its delivery acknowledgement take the waiter out of the queue while it stays in its waker,
   and keeping it queued there fails `channel/082` and `085`; section 3 follows TrueAsync. The Sage's probe on the reference: with the waiter taken out of the queue at a
   scope's cancel, `channel/048`, `050`-`053` and `063` fail, the other 82 pass.
