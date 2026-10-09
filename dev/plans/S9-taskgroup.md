# S9 notes, layer 4: TaskGroup and TaskSet

Design note of the fourth S9 layer, 2026-10-09. The layer adds `Async\TaskGroup` and `Async\TaskSet`: a set
of tasks run as coroutines of one scope, with an optional concurrency limit and a queue for the tasks over
it, results kept by key, and Futures that settle on all, the first or the first successful task. `TaskSet`
is the same object with consuming reads: an entry leaves the set when its result is delivered. Names,
signatures, messages and behaviour come from TrueAsync (`ext/async` at `tests/lists/REFERENCE`, `1fdacf8`,
file names alone below; its core, the fork `true-async/php-src` `863f6dd90cf`, below `F:`) and its
documentation (`true-async.github.io` `c5d30eb`, `en/docs/components/task-group.md`, `task-set.md`, below
`site:`). Edmond decided the departures one question at a time on 2026-10-09 (DECISIONS 2026-10-09 S9.26),
except the internal ones, which he accepted together at 07:26; section 8 lists them with their reasons. `true-async-doc` has only an early sketch
(`ru/concepts/scope.md:218-238`, `new TaskGroup(captureResults: true)` and `spawn with $group`), which the
code and the site replaced; it is not followed. Behaviour marked "probed" was run on a debug build of that
fork with that `ext/async` on 2026-10-09 (scripts `g1.php`-`g23.php`, `g10h.php`, `d1.php`-`d5.php`, `f1.php`, `r28_plain.php`, `r28_lost.php` and
`r28_control.php` in
`/mnt/project-files/s9/probes/s9.taskgroup/`); the same build passes the 71 tests listed in section 9 and `channel/058`,
and skips `task_group/040` (ZTS only).

The layer also narrows `await_*` and every cancellation token to `Completable` (section 7, S9.27): Edmond
judged a non-completable item or token a design error, the channel's included (DECISIONS 2026-10-09,
reversing the channel item and token of S9.18 and S9.20).

**Why this layer is the fourth.** `dev/PLAN.md` orders the layers scope, context, channels, task groups,
pools, iterators. `channel/058` and `059` wait for TaskGroup in `tests/lists/S9.excluded`. `TaskSet` is not
a second implementation: `task_group.c` serves both classes, and `task_set_arginfo.h` maps every `TaskSet`
method to a `TaskGroup` one (`joinNext` = `race`, `joinAny` = `any`, `joinAll` = `all`). `Async\Pool`
(`pool.c`, 70 tests) has no user in the first version: its only consumer, the PDO pool, is out of it
(`DECISIONS.md` 2026-10-08, S10 item 5). `iterate()` (20 tests) stands on the iterator core S9.6 built and
comes as a small layer after this one. `Async\select` for channels is not added: `await_*` over
`recvAsync()` Futures covers it (Edmond, 2026-10-09).

**No core change.** The pinned core (`async-core-io-2026-10-08-4` `77dbfc061f3`) has no group type. The
fork's `zend_async_group_t` (`F:Zend/zend_async_API.h:2117-2119`) is an event with nothing else, and only
its default stub (`F:Zend/zend_async_API.c:76`) and the extension call `ZEND_ASYNC_NEW_GROUP`; the RFC text
says nothing about groups. The layer is the extension's own, on layers 1 and 3 and S4's wait-record layer.

## 1. What the layer implements

| Kind | In this layer | Elsewhere |
|---|---|---|
| Classes | `TaskGroup` and `TaskSet`, both final, `Awaitable`, `Countable`, `IteratorAggregate`, `@strict-properties`, `@not-serializable` (`task_group.stub.php`, `task_set.stub.php`) | `Pool`, `iterate()`: later layers |
| Both | `__construct(?int $concurrency = null, ?int $queueLimit = null, ?Scope $scope = null)`, `spawn(callable, ...$args)`, `spawnWithKey(string\|int, callable, ...$args)`, `trySpawn(callable, ...$args): bool`, `trySpawnWithKey(string\|int, callable, ...$args): bool`, `cancel(?AsyncCancellation)`, `close()`, `dispose()`, `isFinished()`, `isClosed()`, `count()`, `awaitCompletion()`, `finally(Closure)`, `getIterator()` | |
| `TaskGroup` | `all(bool $ignoreErrors = false): Future`, `race(): Future`, `any(): Future`, `getResults()`, `getErrors()`, `suppressErrors()` | |
| `TaskSet` | `joinNext(): Future`, `joinAny(): Future`, `joinAll(bool $ignoreErrors = false): Future` | |
| Waits | the TASK_GROUP wait kind: a step of `foreach`, `awaitCompletion()`, `spawn()` on a full queue (section 4) | |
| Scope | the owner pin of the group's own scope, the "zombies allowed" bit of `Scope::allowZombies()` (section 5) | |
| Collector | TASK_GROUP's `collector_target`, the group as a source of its Futures and the holder of its own scope (section 6) | |
| `await_*` | items and tokens `Completable` only; `Scope::awaitCompletion()`, `awaitAfterCancellation()` and `timeout()` typed with it (section 7, S9.27) | |

What a user gets, from `site:task-group.md`:

```php
$group = new Async\TaskGroup(concurrency: 50);
foreach ($urls as $url) {
    $group->spawn(fn() => fetch($url));     // a coroutine now, or a queued task while 50 run
}
$results = $group->all()->await();          // [key => result], or CompositeException

$group = new Async\TaskGroup();
$group->spawnWithKey('user', fn() => loadUser($id));
$group->spawnWithKey('orders', fn() => loadOrders($id));
$group->close();
foreach ($group as $key => [$result, $error]) { ... }   // as tasks settle, ends when all are delivered

$set = new Async\TaskSet(concurrency: 5);
foreach ($jobs as $job) { $set->spawn(fn() => run($job)); }
$set->close();
while (count($set) > 0) {
    $result = $set->joinNext()->await();    // the next task to end; its entry leaves the set
}
```

## 2. The group

**The structure** follows TrueAsync's (`task_group.h:45-104`) on our event:

```c
typedef struct {
	async_event_t base;              /* type bit and the group's flags; no subscribers (section 4) */
	async_scope_t *scope;            /* the group's own child scope, pinned; NULL with an external scope or after the closing */
	zend_object *scope_object;       /* an external Scope object, held; its scope is read through it at each use */
	uint32_t concurrency;            /* 0: no limit */
	uint32_t queue_limit;            /* 0: unbounded */
	uint32_t active_count;           /* running task coroutines */
	zend_long next_key;              /* spawn()'s next integer key */
	HashTable tasks;                 /* key => a task entry: queued, running, succeeded, failed; spawn order */
	HashTable pending;               /* the queued tasks' keys, oldest first (section 8, item 21) */
	HashTable settled;               /* the settled tasks' keys in completion order (section 3) */
	async_task_group_queue_t slot_waiters; /* spawn() parked on a full queue, oldest first */
	async_task_group_queue_t waiters;      /* foreach steps and awaitCompletion() */
	async_task_group_futures_t futures;    /* pending Futures of the reads, oldest first, each with its kind */
	HashTable *finally_handlers;     /* lazy */
	zend_object std;
} async_task_group_t;
```

The flags: SEALED (`close()`, `cancel()`, `dispose()`, the destructor), COMPLETED, TASK_SET, CANCELLED (a
cancel ran, section 5) and CLOSING (the destructor ran, section 5); each is a named bit among the event's own
type bits (13-28), which `await.c` does not read. TrueAsync's bits 11-13 belong to our core's event flags and
move. Nothing reads the event's CLOSED bit of a group, so the group does not set it (TrueAsync sets it at
every settle, `task_group.c:725`, the defect of S3.md section 13, item 17). TrueAsync's group-wide "errors
handled" bit becomes a bit per entry (section 3).

**A task entry** is TrueAsync's (`task_group.c:25-42`) with one bit added: queued (it owns the callable, its
arguments and named arguments), running (a counted reference to the coroutine), succeeded (the result) or
failed (a counted reference to the exception); the HANDLED bit says its error needs no report; set on a
queued or running entry, it covers the failure to come (section 3). Keys are the caller's `string|int` or `spawn()`'s counter; a key already present
throws `Duplicate key "x" in TaskGroup` (or `Duplicate key 7 in TaskGroup`).

**The constructor** validates as TrueAsync's (`task_group.c:1349-1418`): `$concurrency` and `$queueLimit`
between 0 and `UINT32_MAX` (`ValueError` naming the argument); a null `$concurrency` is 0, no limit, named or
positional (section 8, item 18); a null `$queueLimit` means `2 * $concurrency`, saturated, and no limit when
`$concurrency` is 0; `$queueLimit = 0` is an unbounded queue. Without `$scope` the group makes a child scope of
the current scope (`async_scope_new(async_scope_current())`), clears the "dispose safely" bit it inherited
(section 5) and pins it. With `$scope` it holds that `Scope` object and runs its tasks in that scope itself,
as TrueAsync's code does (`site:reference/task-group/construct.md` says a child scope is made in both cases);
the group owns that scope from then on (section 5). A `Scope` whose `allowZombies()` was called draws an
`E_WARNING` (section 5) before the group takes the object, so a handler that turns the warning into an
exception leaves nothing to release. A disposed one throws `Cannot use a disposed Scope for TaskGroup`. A second
`__construct()` throws `Error`, as the channel's (S9-channel.md 8, item 4). `TaskSet` messages say
"TaskGroup", as TrueAsync's aliases do.

**`spawn()` and `spawnWithKey()`** (`task_group.c:1421-1507`):

1. A sealed group throws `Cannot spawn tasks on a closed TaskGroup`; a duplicate key throws. A group whose
   scope is cancelled or closed seals and cancels as section 5 says; then a cancelled scope throws the
   group's message, a closed one the scope's `Cannot spawn a coroutine in a closed scope`.
2. While no slot is free and the queue is full, the caller parks as a slot waiter (section 4) and checks
   again when woken; a seal while it waits throws the same message.
3. With a free slot the task starts: `async_scope_spawn()` into the group's scope, a running entry, a
   completion subscriber on the coroutine and `active_count + 1`. Otherwise the task is queued: a queued
   entry and its key at the end of `pending`.

A running task does not hold the group object (TrueAsync's does, `GC_ADDREF` at `task_group.c:844`): its
subscriber keeps a plain pointer to the group, so dropping the last reference runs the destructor at once,
which cancels the tasks (section 5; section 8, item 1). The group is freed only after its closing, which holds
it (section 5), or after a bailout skipped its destructor, when `free_obj` detaches the subscribers of the
tasks still running.

`async_scope_spawn()` copies the arguments and adds its own references to the callable (`src/scope.c:1080-1146`),
so a queued entry keeps its own counted copy and releases it after the start, whatever its outcome. A `__call`
trampoline is the exception: `zend_fcc_dup()` copies none, and `async_scope_spawn()` frees it on a refusal and
hands it to the call on a start (`src/scope.c:1088-1090`, `1121`), so the entry owns the trampoline and hands it
over without releasing it again. A scope that refuses the start (a closed scope) throws from `spawn()` itself,
as TrueAsync's `ZEND_ASYNC_SPAWN_WITH` does.

**`trySpawn()` and `trySpawnWithKey()`** are this layer's own, as Go's `errgroup.TryGo`: with a free slot
they start the task as `spawn()` does and return `true`; otherwise they return `false` and queue nothing.
A sealed group and a duplicate key throw as for `spawn()`; a `false` call takes no integer key; without a
limit they always start. A free slot implies an empty queue and no parked spawner, so a `trySpawn()` never
passes a waiting one. Section 8, item 2.

**A task whose body has returned is not cancelled.** The coroutine releases its callable while it still
runs (`src/coroutine.c:371-377`), and a closure that held the group's last reference runs the destructor there,
whose cancel would make the cancellation the outcome of a task that already returned
(`src/scheduler.c:1539-1557`): `$g->spawn(function () use ($g) { return 42; })` would lose its 42. A coroutine
gains a "body returned" bit, set before that release, whether the body returned or threw (Edmond 10:39: a
coroutine that has run cannot be cancelled). It acts only in the cancel's branch for the current,
running coroutine (`src/scheduler.c:1539-1557`): there a returned coroutine is left as it is, CANCELLED not set
(so `isCancelled()` stays false) and its outcome kept. A cancel that reaches it parked, inside a closure
destructor that suspends, cancels it as any other, so the exit walks still end it. The rule sits in the cancel
of a coroutine, so it holds for every scope cancel (a `Scope` object released from such a closure takes the
same path; TrueAsync loses the result there, probed `r28_plain.php`, `r28_lost.php`; Edmond 10:39).

**A task's end** is TrueAsync's `task_group_on_coroutine_complete()` (`task_group.c:891-998`), in two parts.
The subscriber sits in the coroutine's `callbacks` and is notified by `async_coroutine_finalize()` in
scheduler context (`src/true_async_API.c:139-141`), where no destructor and no handler may run (S9-scope.md
9, item 9; DECISIONS S9.19). Its callback only records: it marks the coroutine's exception handled
(`ASYNC_COROUTINE_F_EXCEPTION_HANDLED`), so the error does not take the route of S9-scope.md 4 (the group
keeps it), and turns the entry into a succeeded or failed one, keeping its HANDLED bit, the coroutine object
still alive. The rest runs in the subscriber's `dispose`, which `async_callbacks_free()` calls after the
notify, outside scheduler context and before the coroutine leaves its scope (`src/coroutine.c:482`,
`522-524`), so the scope is still open for the next queued task. A `dispose` whose callback did not run (an
earlier callback threw and ended the notify, `src/true_async_API.h:210-211`) reads the outcome from the
coroutine:

1. the key goes to the end of `settled`;
2. `active_count - 1`, and the drain starts queued tasks from the head of `pending` while slots are free;
   each start frees a queue place and wakes the oldest slot waiter. A group's scope that is cancelled or
   closed starts nothing: the queued tasks end as a `cancel()` ends them, the group seals and cancels the
   rest (section 5);
3. the Futures and waiters are served (section 3);
4. if nothing runs and nothing is queued, the group settles (section 3).

On a bailout the `dispose` does nothing more: no drain, no Futures served, no settle, no finally handlers, as a
coroutine's own handlers are destroyed unrun then (`src/coroutine.c:511-517`); the queued entries stay until
`free_obj` frees them, and the scheduler's unwind finalizes the running tasks (`scheduler_bailout_all`).

## 3. Results, the Futures, completion

**Settled and completed.** A group is settled when nothing runs and nothing is queued (`isFinished()`,
TrueAsync's `task_group_all_settled() && !task_group_has_pending()`); an open group can settle and then take
new tasks. It is completed when it is settled and sealed: then COMPLETED is set, the waiters of
`awaitCompletion()` wake and the finally handlers start (section 5).

**Order.** `settled` keeps the keys in the order their tasks ended. `foreach` of both classes and
`TaskSet::joinNext()` follow it, as tokio's `JoinSet::join_next()`, Swift's `TaskGroup`, .NET's
`Task.WhenEach` and Python's `asyncio.as_completed` do, and as the stub ("Yields results as they complete")
and `site:task-group.md` say. TrueAsync's iterator walks the spawn order and waits at the first unsettled
entry (`task_group.c:1153-1203`, probed `g5.php`), and its `joinNext()` is `race()`. Section 8, item 3.

**`TaskGroup` reads do not consume** (`site:task-set.md`, "Idempotency vs Consumption"):

| Method | Settles | With |
|---|---|---|
| `all()` | when the group settles | the results by key in spawn order; or rejects with a `CompositeException` of the errors in spawn order |
| `all(ignoreErrors: true)` | when the group settles | the results only |
| `race()` | with tasks already settled, at once; else at the first task to end | the first settled entry in spawn order among those settled when the call is made: its result, or its own exception (not a composite); else the first task to end |
| `any()` | at the first successful task | its result; rejects with a `CompositeException` when the group settles with errors only |

`race()` is TrueAsync's code (`task_group.c:1595-1635`): two calls can name different tasks (probed `g4.php`),
and `site:task-set.md`'s "race() always returns the same first completed task" is wrong; the documentation of
the layer says what the code does. `race()` and `any()` on an empty group throw `Cannot race on an empty
TaskGroup` and `Cannot call any() on an empty TaskGroup`; `all()` on an empty or settled group settles at
once. A call made when the answer is known settles its Future before returning it.

**`TaskSet` reads consume.** `joinNext()` takes the first key of `settled` not yet delivered, `joinAny()` the
first successful one in the same order; each delivered key leaves `tasks` and `settled`, so `count()` falls.
Pending `joinNext()` and `joinAny()` Futures take different tasks, in the order they were called. A rejected
`joinAny()` consumes the failed entries its composite carries. `joinAll()` removes every entry when it
settles, rejected or not, as TrueAsync's (`task_group.c:682`, `692`, `1583`) and the stub ("All entries are
automatically removed from the set after delivery"). `joinAny()` rejects, as `any()`, when the set settles
with failures only, and takes those failures; a second pending `joinAny()` then has nothing to take. A
pending `joinNext()` or `joinAny()` with nothing to take waits while the set is open and rejects when the set
completes, or when its destructor runs, with the message of the same call on an empty set (`Cannot race on
an empty TaskGroup`, `Cannot call any() on an empty TaskGroup`).

**The Futures** come from `async_future_new_pending()` (`src/future.h:66-71`). Each has a subscriber in
its event's `callbacks` that records it in `futures` and whose `dispose` takes it out when the Future
settles or is freed first, the pattern of the channel's `recvAsync()` (`src/channel.c:1182-1217`) in place
of TrueAsync's dispose override (`task_group.c:112-120`). At a task's end every pending Future of a matching
kind is served, oldest first; at the settle every `all()` Future and every `any()` Future still pending.
TrueAsync's loop at the settle skips the waiter after each one it resolves (`task_group.c:665-717`, probed
`g1.php`: the second of two `all()` Futures never settles and the script ends in a deadlock). The group's
`free_obj` detaches the Futures still recorded, as the channel's does (`src/channel.c:1236-1246`). A
rejected Future nobody awaits warns at its release, as every Future does (`src/future.c:107-164`);
TrueAsync left `ZEND_FUTURE_SET_EXCEPTION_CAUGHT` commented out at the same places (`task_group.c:673`).

**Results and errors.** `getResults()` returns the successful results by key; `getErrors()` the exceptions by
key; `count()` the number of entries: queued, running and settled for `TaskGroup`, undelivered for `TaskSet`.
An error needs no report when its entry is HANDLED. These set the bit:

- `getErrors()` and `suppressErrors()`: on the failed entries present, as TrueAsync's group bit, which a
  later failure clears (`task_group.c:924-925`), so a later failure is reported. No cancel marks: neither
  `cancel()`, `dispose()`, the destructor's nor an outside scope's (Edmond: an error nobody handled is
  printed; TrueAsync's `cancel()` marks);
- `all(ignoreErrors: true)` and `joinAll(ignoreErrors: true)` when they settle: on the failed entries they
  skip, as `suppressErrors()` does; TrueAsync's `all(ignoreErrors: true)` leaves them for the report, while its
  `joinAll()` removes them unreported;
- a new iterator: on the failed entries present; each step then sets it on the entry it yields;
- a rejected `all()`, `race()`, `any()`, `joinNext()`, `joinAny()` or `joinAll()`: on the entries whose
  errors its exception carries. The rejected Future warns at its release if nobody awaits it, so the error
  is reported once either way;
- a successful `race()` or `any()`: on every entry present, the running and queued ones included, so their
  later failures need no report; tasks added later are not covered.

TrueAsync marks only on `getErrors()`, `suppressErrors()`, `cancel()` and a new iterator, and a later failure
clears its group bit (`task_group.c:925`), so a caught `all()` rejection is still reported at the destruction
(probed `g10h.php`: "caught", then the scope's handler gets the `CompositeException`). Section 8, items 4 and 5.
A task's cancellation is never an error to report: the route of S9-scope.md 4 never takes one for a coroutine
(`src/coroutine.c:489`, TrueAsync's `coroutine.c:706`), and a group does the same; it is still a failed entry
for `getErrors()`, `all()` and `foreach`. TrueAsync reports a cancelled group's cancellations at its
destruction (probed `g17.php`). Section 8, item 6.

**`awaitCompletion()`** throws `TaskGroup must be closed before calling awaitCompletion()` on an open group,
returns at once on a completed one, else parks until the group completes (a TASK_GROUP waiter, section 4).
It never throws a task's error. TrueAsync gives it no cancellation token; neither does this layer. It waits
for the tasks only, as TrueAsync's code; `site:task-group.md` says it waits "as well as other coroutines in
the `Scope`".

**`foreach` and `getIterator()`.** `getIterator()` called directly throws `Error` (`An object of class
Async\TaskGroup is not a traversable object in an invalid state`, TrueAsync's message, `task_group/039`). The
iterator yields `key => [$result, null]` or `key => [null, $error]`, refuses by-reference iteration (`Cannot
iterate TaskGroup by reference`) and a step outside a coroutine, as the channel's (S9-channel.md 8, item 14).
A step with nothing to deliver parks (a TASK_GROUP waiter) until a task ends or the group completes; on a
completed group with nothing left it ends. For `TaskGroup` every iterator starts at the first settled key, so
a second `foreach` yields all entries again; for `TaskSet` each step delivers and removes the key.

## 4. Waiting: the TASK_GROUP kind

Three waits park a coroutine on a group: a `foreach` step, `awaitCompletion()` and `spawn()` on a full queue.
The destructor never waits (section 5). Each is the coroutine's waker record with the TASK_GROUP kind in one of
the group's two queues; a parked wait allocates nothing (D29). TrueAsync allocates a waiter event per
`foreach` step (`task_group.c:1230`) and a trigger per `spawn()` park (`task_group.c:1462`), and parks
without cleanup on a bailout (S3.md section 13, item 14).

- `waiters` holds the `foreach` steps and the `awaitCompletion()` callers, the kind's
  bits telling them apart. A task's end wakes every step; the completion wakes the steps and the
  `awaitCompletion()` callers.
- `slot_waiters` holds the parked spawners. A freed queue place wakes the oldest; a woken spawner that
  leaves the park, by going on or by an exception such as its cancellation, wakes the next one while a slot
  is free or the queue has room; a seal and a cancel wake all, and each throws the closed
  message. TrueAsync wakes one per freed place only (`task_group.c:761-775`), so a spawner can stay parked
  with room free: concurrency 2 and queue limit 1, A and B run, C queued, S1 and S2 parked; A ends, C starts
  and wakes S1; B ends with nothing queued; S1 starts its task directly, and S2 waits until a close.
  Section 8, item 19.

The wake takes the record out of its queue (the kind's `unlink`, D26): no reservation rides on a record, so
the CHANNEL kind's exception to D26 (DECISIONS 2026-10-08) is not needed. The woken coroutine checks the
group again, as TrueAsync's loops do (`task_group.c:1153-1249`, `1461-1486`, `1823-1857`). The kind has
`info` (`getAwaitingInfo()` names the wait), `unlink`, `abort` (a bailout or the request's end takes the
record out of its queue) and `collector_target` (section 6). A cancellation of the parked coroutine
propagates from the call, as from every wait.

The group is not an `await_*` item and not a cancellation token: since S9.27 both take `Completable` only, so
`await_all([$group])` throws the item message of S9.27 and `await($group)` is a `TypeError`, as TrueAsync's
`await()` (probed `g7.php`). TrueAsync accepts a group as an `await_*` item: `await_all()` waits for its tasks
and yields `null` (probed `g22.php`). Section 8, item 7.

## 5. Closing, cancelling, the scope and the destructor

**`close()`** seals the group, wakes the slot waiters, and completes the group if it is settled. Queued and
running tasks go on.

**`cancel(?AsyncCancellation)`** seals the group, sets CANCELLED, marks nothing HANDLED (section 3), wakes
the slot
waiters, turns every queued task into a failed entry with the cancellation, whose keys go to the end of
`settled` in queue order, and serves the Futures and `foreach` steps with them as with any task's end. Then it
cancels the scope with the given cancellation or a new `AsyncCancellation("TaskGroup cancelled")` through
`async_scope_cancel()` with `is_safely` false, so the running tasks are interrupted whatever the scope's mode,
and completes the group if it is settled. A second `cancel()` does nothing, and so does a `cancel()` on a
completed group. A `cancel()` after `close()` while tasks run or wait in the queue cancels, as the stub and the
site say ("Implicitly calls close()"). TrueAsync starts the queued tasks after a cancel (probed `g15.php`,
`g16.php`), returns on a sealed group (`task_group.c:1720-1722`), and cancels with the scope's own mode, so a
group under the global scope leaves its tasks running as zombies (`g15.php`). Section 8, items 8-10.

**`dispose()`** is `cancel()` with `AsyncCancellation("Scope is being disposed due to TaskGroup disposal")`:
the group seals, the queue never starts, `spawn()` throws. TrueAsync's cancels the scope only, so queued
tasks still start and `spawn()` still adds (probed `g6.php`); `channel/058` needs that and is excluded
(section 9). Section 8, item 11.

**A scope cancelled or closed from outside.** When the group's scope is cancelled (an external `$scope`, a
parent's cancel) or closed, the queued tasks end as a `cancel()` ends them, with a new
`AsyncCancellation("TaskGroup cancelled")` (our scope keeps no cancellation object to reuse; S9.28), and the group seals and cancels the rest, at the first drain or
`spawn()` that finds the scope so. This cancel marks no entry HANDLED: nobody saw the errors. A cancelled scope that is not closed still accepts spawns
(`src/scope.c:1088`, `644-645`), so the drain checks the scope's CANCELLED bit as well as its CLOSED bit;
TrueAsync's drain would start the queued tasks after a parent's cancel, and on a closed scope leaves the
refusal's exception pending in the coroutine's notify (`task_group.c:797-799`, `885-887`). A direct `spawn()`
into a closed scope still throws the scope's message to its caller. Section 8, item 12.

**`finally(Closure)`** stores the handler; the handlers start once, when the group completes, in a run of
`async_finally_handlers_start()` (`src/coroutine.c:621-657`) under the group's scope with the group as their
argument. A closed own scope does not refuse the run: the run gets a child scope of its own, and the worker's
spawn checks only that one (`src/iterator.c:127`). While async is not active the run is refused
(`src/coroutine.c:623-625`), and so is a run whose worker the scheduler refuses. Async is active from the
core's launch point before the script's first line (the pinned core's `Zend/zend_async_API.c:772`)
until the coroutines go at the request's end (`ZEND_ASYNC_DEACTIVATE`), and the scheduler refuses a worker
only when it cannot make its own coroutine (`src/scheduler.c:1416-1421`), so a refused run means the request
is ending or failing. The handlers are then released unrun, as a coroutine's (`src/coroutine.c:513-518`) and a
scope's at the request's end, and as TrueAsync's destructor drops them. Every handler stored before the
completion that runs therefore runs in the run's coroutine, and its error takes the scope's route (the scope's
handler, else Uncaught), never thrown out of `unset()`, `close()` or `cancel()`: the self-decision listed to
Edmond at 08:33 holds. A scheduler that cannot make its coroutine leaves its exception pending (a fiber context
that cannot be made, `src/scheduler.c:90-95`); the closing then only releases, as after a bailout (no report, no
scope calls), and the exception propagates from the call that completed the group as from a refused `spawn()`.
Since S9.23 the run's scope carries `ASYNC_SCOPE_F_FINALLY_RUN`, so no cancel of the group's scope
stops it; after a scope's deadline passed (S9.24, `ASYNC_SCOPE_F_DEADLINE_PASSED`) the run calls no handler, as
any finally run. On a completed group the handler is called at once, in the caller (TrueAsync's, `task_group/024`).
TrueAsync starts the handlers at every settle, sealed or not (`task_group.c:727-746`), against its stub
("invoked when the group is closed AND all tasks are completed"). Section 8, item 13. A handler's error takes
the run's way, as a scope's finally error does: the scope's handler, else `Uncaught` (TrueAsync's too, probed
`f1.php`). The run holds the group (`src/coroutine.c:650-654`). A scope's and a coroutine's runs release
their target and their handlers from the microtask's destructor (`iterator_dtor()`, `src/iterator.c:170-202`;
`finally_run_dtor()`, `src/coroutine.c:610-619`), whose last release is usually the scheduler's tick
(`src/scheduler.c:370-384`), where a target's destructor would run in scheduler context (DECISIONS S9.19). S9.25,
built in parallel, moves that release out of the scheduler's context so that such a destructor may wait; S9.28
builds on its release point, and the rules below bind it for the group. The group's run gains an end callback and releases there both the group and the handlers' array (a
handler that captures `$g` would free the group with it): the last worker to leave calls it from its body
(`iterator_release_coroutine()` from `iterator_worker_entry()`, `src/iterator.c:494-501`), before
`async_scope_finally_run_end()` (`src/iterator.c:92`) and with `EG(exception)` saved and restored. A worker
finished unrun (an exit, a deadline, an error's cancel walk) leaves through its finish handler
(`iterator_worker_finished()`, `src/iterator.c:105-120`), inside the notify in scheduler context; there the
callback only arms a subscriber whose `dispose` ends the closing outside the notify, as a task's end does
(section 2). After a bailout, or once the request's end has begun freeing scopes, the callback only releases
the group: no report, no scope calls. A handler stored in the group that captures `$g` makes a cycle, so
`unset($g)` cancels nothing until the GC collects it; the documentation says to use the handler's argument.

**The own scope.** It is a child scope without an object, which our scopes free as soon as they can be disposed
(`src/scope.c:210-228`, `540`); TrueAsync keeps it alive with the event's reference count and the flag
`ZEND_ASYNC_SCOPE_F_OWNER_PINNED` ("must not be disposed by parent-cascade or automatic flow",
`F:Zend/zend_async_API.h:1566-1569`; `scope.c:1544-1550`). Our scopes have no reference count, so the pin alone
does that work: a field `zend_object *owner_object` in `async_scope_t`, set to the group object, makes
`scope_can_be_disposed()` false, and the parent's cascade stops there as at a busy child.
`async_scope_release_owner()` clears the field and disposes the scope when it can. The field serves the
collector too (section 6), which a flag bit could not. The pin stops a disposal, not a close: the cascade S9.20
added (a completed scope's cancel closes its child scopes with no coroutines, `src/scope.c:652-658`; DECISIONS
2026-10-08) closes an idle group's own scope like any other; the next drain or `spawn()` then finds it closed
(above). The own scope does not inherit "dispose safely" (`src/scope.c:1162`; TrueAsync's does,
`scope.c:1331-1337`): a group's cancel interrupts its tasks. Section 8, item 10.

**An external scope** is held through its `Scope` object and read through it at each use
(`async_scope_object_from_object()->scope`). The group owns it: the closing cancels it and releases the object,
which disposes the scope, as TrueAsync's destructor does (`task_group.c:481-500`). So a second group on the
same `Scope`, or other coroutines in it, die with the first group; such code is the user's logic error
(Edmond, 2026-10-09), and the documentation of the layer says so. TrueAsync keeps the scope alive by the
event's count (`task_group.c:1402`); ours frees a cancelled scope with no coroutines while its object lives
(`scope_can_be_disposed()`, `src/scope.c:210-228`), and a second pin field would not serve two groups on one scope. When it is gone,
the group finds it closed (above): `spawn()` throws `Cannot spawn a coroutine in a closed scope`, the finally
handlers start under the current scope, and the report starts from the current scope (below). TrueAsync's
spawn into a cancelled empty `Scope` throws the same message and leaks the closure and the entry
(`task_group.c:1489`, probed `g23.php`).

A `Scope` whose `allowZombies()` was called draws an `E_WARNING` at `new TaskGroup(scope: $s)`: `TaskGroup
cancels its tasks even though the Scope allows zombies`. `allowZombies()` sets a new bit,
`ASYNC_SCOPE_F_ZOMBIES_ALLOWED`, for it: the "dispose safely" bit cannot tell the call, since every scope
inherits it from the global scope (`src/scope.c:1162`, `2148`). Section 8, item 10.

**The destructor and the closing.** Dropping the group closes it, and the destructor never waits for that
(Edmond, 2026-10-09: one algorithm everywhere, the object taken again by the closing): `unset($g); echo
"after";` prints `after` before the finally handlers run. So no context needs a rule of its own: a finished
coroutine, the scheduler's context, the GC's destructor coroutine and a task of the group all take the same
path. The closing: the tasks are cancelled, the finally handlers
run, the errors nobody saw are reported, and the scope goes. TrueAsync's destructor runs only after the last
task (a task holds the group), drops the finally handlers, reports even the delivered errors and stays silent
without a handler (`task_group.c:442-511`, probed `g9.php`, `g10h.php`, `g21.php`). Section 8, items 1, 14-16.

`dtor_obj` does, with a pending exception saved and restored, as the callbacks' notify saves it
(`src/true_async_API.c:146-147`), since a group freed while an exception unwinds would otherwise stop the
scope calls at that exception (`src/scope.c:866`):

1. take a reference to the group object for the closing (the engine keeps an object taken again in its
   destructor, `zend_objects_store_del`) and set CLOSING;
2. seal; reject the pending `TaskSet` reads with the empty set's message (section 3). No spawner, `foreach`
   step or `awaitCompletion()` is parked: each holds the group through its call frame's `$this`;
3. if the group is settled, complete it: the finally run starts now. Otherwise cancel it as `cancel()` does,
   with `AsyncCancellation("Scope is being disposed due to TaskGroup destruction")`, TrueAsync's message, but
   marking no entry HANDLED, as no cancel does, so step 5 still reports what nobody saw; the completion then comes at the last
   task's end and starts the finally run there;
4. return. The finally handlers run once: the run takes the handlers' table out of the group when it starts,
   a `finally()` call on the completed group calls its new handler at once and stores nothing, and the engine
   never calls the destructor of an object taken again a second time (`IS_OBJ_DESTRUCTOR_CALLED`). A waiting
   destructor would have needed rules for where it may park: a finished coroutine has given its stack away
   (`src/coroutine.c:443`, "There is no coroutine to suspend", `src/scheduler.c:1768-1775`; TrueAsync waits in
   one such case, probed `d5.php`, and hangs and leaks in another, `d4.php`), and a wait in the GC's
   destructor coroutine keeps the GC run open (`Zend/zend_gc.c:2069-2072` in the pinned core).

The closing ends after the completion, at the finally run's end callback, or at the completion itself when the
group has no finally handlers or their run was refused (they are then released unrun, section 5 above; the
reporter of step 5 is refused the same way, and the errors go with the request):

5. report the errors that need it: the failed entries not HANDLED and not cancellations, as one
   `CompositeException`. A reporter coroutine throws it, spawned into a new child scope of the group's scope
   (or of the current scope when the external scope is gone), as TrueAsync's `async_spawn_and_throw()`
   (`exceptions.c:335-365`). It is built as `future_drain_spawn()` builds its coroutine
   (`src/future.c:551-573`): `internal_entry` throws the composite, `extended_data` holds it,
   `extended_dispose` releases it when the reporter never runs (a bailout, the request's end); none is
   spawned while async is not active. While the graceful shutdown of `exit()` or an uncaught error runs, the
   walks would cancel a reporter unrun, so the composite goes to `async_exit_exception_add()`
   (`src/coroutine.h:110`) instead and is printed with the exit's exceptions. It carries `ASYNC_COROUTINE_F_PROTECTED`, so the cancel of step 6 waits
   and is dropped with it, as TrueAsync's thrower survives such a cancel by rethrowing from its dispose
   (`exceptions.c:312-319`); the walks at exit and shutdown clear the flag (`src/scheduler.c:726`). It carries
   `ASYNC_COROUTINE_F_HI_PRIORITY`, as TrueAsync's at priority 1 (`scope.c:1615`, `async_API.c:157-158`). The
   error then takes the route of S9-scope.md 4 from the reporter: a scope's handler gets it with the reporter
   as `$coroutine` (probed `g13.php`); with no handler on the way the unheld reporter ends the request with
   `Uncaught Async\CompositeException` (`src/coroutine.c:495-504`), as an unhandled coroutine error does;
6. cancel the group's scope, own or external, when it is not completely done (a task's child coroutine still
   runs, or another coroutine of an external scope), as TrueAsync's (`task_group.c:481-500`); "done" leaves
   out the closing's own worker, the finishing task and the reporter's scope, which are still members here; release the pin
   of the own scope, which disposes it unless the reporter or such a child still runs in it; release the
   external `Scope` object. `scope_free()` of a pinned scope (the request's end after a bailout) clears the
   group's `scope` through the owner pointer;
7. release the closing's reference to the group, which frees it unless
   someone took the object again.


`free_obj` runs `zend_object_std_dtor()` first (S9.21), then detaches the subscribers of tasks still running
(a bailout skipped the destructor), frees the entries, detaches the Futures and releases the handlers. It never
reads `scope`: after a fatal error the request's end frees the own scope with the global scope's tree
(`src/scope.c:2166`) before the object store frees the group, so TrueAsync's assertion (`task_group.c:552`) is
not ported.

## 6. Garbage collection and the async object collector

**`get_gc`** reports what TrueAsync's does (`task_group.c:555-604`): a queued task's callable and arguments,
a running task's coroutine, results, exceptions, the finally handlers, the external `Scope` object. A running
task no longer holds the group, so a group reached only from its tasks' closures is collected when nothing
else reaches those coroutines; the scheduler and the scope reach every running one, so a live group is not.

**The collector** (S7) learns three things:

- TASK_GROUP's `collector_target`: the group object as the target (its holders can spawn, close or cancel,
  which wakes every waiter), and a reach node of the group whose sources are its running tasks and, while it
  closes, its finally run (a task's end wakes the `foreach` steps, frees a slot, and the last one completes
  the group; the run's end ends the closing), the model of the SCOPE kind's completion node
  (`src/scope.c:1626-1652`);
- the group's Futures: a Future's references reporter knows a channel's waiter only
  (`async_channel_of_future_waiter()`, `src/future.c:276-285`), so it gains the group's counterpart, which
  reports the group object and its reach node as sources; without it a coroutine awaiting `all()` while the
  tasks wait in `delay()` (`task_group/041`) is reported as one that can never wake;
- the own scope's reach gains its `owner_object` as a holder beside `scope_object` (`src/scope.c:935-973`),
  since `cancel()`, `dispose()` and the destructor cancel the scope.

A coroutine parked on a group, or on its Future, that only parked coroutines reach is found; TrueAsync waits
for the global deadlock. The fuzz oracle's call goes before each wake, as in the channel.

## 7. Steps

- S9.26 This note, the plan, DECISIONS.
- S9.27 `await_*` and tokens take `Completable` only. The item check of `await.c` (`779-788`) refuses every
  non-`Completable` item with `Expected item to be an Async\Completable object` (TrueAsync's says
  `Awaitable`, section 8, item 27) and still refuses a `Timeout` as an item (DECISIONS 2026-10-06). The
  tokens of the six `await_*` (their ZPP, `await.c:1556`, `1582`, `1614`, `1646`, `1684`, `1719`),
  `Scope::awaitCompletion()` (`scope.c:1742`) and `Scope::awaitAfterCancellation()` (`scope.c:1854`) take
  `async_ce_completable`, and their stubs say `?Completable` (`Completable` for `awaitCompletion()`);
  `timeout()` returns `Completable`. The channel stops being an item and a token: the CHANNEL branches of
  `await.c` and the channel's item callbacks of S9.18 and S9.20 go (the closed channel's `ChannelException` of
  `recv()` stays; S9.27 folded `async_channel_close_exception()` into `channel_throw_closed()`, its only caller
  left). Tests: `channel/100`, `103`, `111`, `132`-`134` and `await/128` (the message) change with `changed:`
  and the DECISIONS line (each asserts the refusal, or moves to `recvAsync()` Futures where it tests the
  close); `channel/067` stays (it awaits `recvAsync()` Futures). The step's own tests open the layer 4 block of
  `tests/lists/S9.txt`.
- S9.28 The layer 4 block of `tests/lists/S9.txt` that S9.27 opened, extended (section 9), with `--XFAIL--` naming
  S9.28 or S9.29; `src/task_group.c`, `task_group.h`, `task_group.stub.php`, `task_set.stub.php`: the structure, the
  constructor and its warning, `ASYNC_SCOPE_F_ZOMBIES_ALLOWED`, `spawn()`, `spawnWithKey()`, `trySpawn()`,
  `trySpawnWithKey()`, the queue and the drain, a task's end, results, errors and the HANDLED bit, the Futures
  of both classes and their collector sources, `close()`, `cancel()`, `dispose()`, the scope cancelled or
  closed from outside, `isFinished()`, `isClosed()`, `count()`, `getIterator()`'s direct call, the owner pin
  and `async_scope_release_owner()`, the finally handlers and the run's end callback, the closing with its
  reporter coroutine (section 5), and `free_obj` (sections 2, 3, 5, 6); the coroutine's "body returned" bit
  and the running branch of the cancel (section 2, section 8 item 28), for every scope. Until S9.29,
  `awaitCompletion()` and `foreach` throw "not implemented yet", and `spawn()` on a full queue throws instead
  of parking.
- S9.29 The TASK_GROUP kind (section 4): `awaitCompletion()`, `foreach`, `spawn()` on a full queue;
  `collector_target` and the scope's holder (section 6).
- S9.30 Layer review: the Critic over S9.27-S9.29, coverage of `src/task_group.c`, Mull on the layer's diff,
  the fuzz oracle over 100 seeds of the layer's tests and `collector/`, the measurements of section 9; the
  documentation of the layer in `true-async-doc` (`race()` as the code does it, two groups on one `Scope`,
  the closing, `await_*` over `recvAsync()` Futures in place of a channel item, and that
  `await_any_or_fail()` over several `recvAsync()` Futures can take a message from a channel it does not
  return).
- S9.31 Security pass by `dev/SECURITY.md`: `$concurrency` and `$queueLimit` against `memory_limit`, the
  destructors of results and callables dropped by a cancel or a free, a Future freed after its group, a
  destructor that spawns into the group being closed, the closing's reference when the end callback never
  comes, PHP's GC during a drain.

## 8. Departures from TrueAsync

Each was Edmond's decision on 2026-10-09 (DECISIONS 2026-10-09, "S9 layer 4"), unless marked "internal".

1. **Dropping the group cancels its tasks at once** (sections 2, 5): a task does not hold the group, so the
   destructor runs when the last user reference goes. TrueAsync's task holds it, so `unset($g)` cancels
   nothing and its destructor runs after the last task. Our `Scope` cancels its coroutines when its object
   goes (`src/scope.c:1206-1211`), and tokio's `JoinSet` aborts its tasks on drop.
2. **`trySpawn()` and `trySpawnWithKey()`** (section 2), as Go's `TryGo`; TrueAsync has neither.
3. **`foreach` and `TaskSet::joinNext()` follow completion order** (section 3), as tokio, Swift, .NET and
   Python; TrueAsync's code yields in spawn order (probed `g5.php`) and its `joinNext()` is `race()`.
   `TaskGroup::race()` stays TrueAsync's (section 3).
4. **An error received through a rejected read needs no report** (section 3); TrueAsync reports a caught
   `all()` rejection again at the destruction (probed `g10h.php`).
5. **After a successful `race()` or `any()` the other tasks' errors need no report** (section 3): the call
   is made for the first result. TrueAsync reports them.
6. **A task's cancellation is not reported as an error** (section 3), as for a coroutine; TrueAsync reports a
   cancelled group's cancellations (probed `g17.php`).
7. **The group is not an `await_*` item or a token** (section 4), by S9.27; TrueAsync's `await_all()` waits
   for its tasks and yields `null` (probed `g22.php`).
8. **`cancel()` starts no queued task** (section 5), as its stub says ("Queued closures are never started");
   each becomes a failed entry with the cancellation. TrueAsync starts them when a cancelled task ends
   (probed `g15.php`, `g16.php`).
9. **`cancel()` after `close()` cancels** (section 5), as the stub and the site say; TrueAsync's returns on a
   sealed group.
10. **A group's cancel always interrupts its tasks** (section 5): the own scope does not inherit "dispose
    safely", a cancel is never safe, and a passed `Scope` with `allowZombies()` draws a warning. TrueAsync
    inherits the mode and leaves the tasks running as zombies (probed `g15.php`).
11. **`dispose()` seals the group** (section 5): it is `cancel()` with its own message. TrueAsync's leaves the
    group open and the queue draining (probed `g6.php`, `channel/058`).
12. **Queued tasks end cancelled when the scope is cancelled or closed from outside** (section 5), and the
    group seals and cancels the rest. TrueAsync's drain starts them after a parent's cancel, and on a closed
    scope leaves the refusal pending in the notify.
13. **Finally handlers start once, when the group completes** (section 5), as the stub says; TrueAsync starts
    them at every settle of an open group too.
14. **The destructor runs the finally handlers, once, without waiting for them** (section 5); TrueAsync's
    drops them.
15. **Unreported errors end the request without a handler** (section 5, step 5): `Uncaught
    Async\CompositeException`, as an unhandled coroutine error; TrueAsync's destructor only cancels the
    scopes on the way and prints nothing (probed `g9.php`, `g21.php`).
16. **The report comes after the finally handlers and is spawned after the cancel** (section 5): a handler
    may still read the errors, and the cancel does not finish the reporter unrun. TrueAsync reports first,
    and its cancel then finishes the thrower, whose dispose rethrows (`exceptions.c:312-319`).
17. **A second `__construct()` throws `Error`** (section 2); TrueAsync's swaps the scope.
18. **A null `$concurrency` is accepted**, as the stub types it (`?int`); TrueAsync parses a plain integer: a
    null, passed or filled in by a named call that skips it, draws a deprecation, a `TypeError` under
    `strict_types` (`task_group.c:1358`, `Z_PARAM_LONG`; read from code, not probed).
19. **A parked spawner passes its wake on while there is room** (section 4); TrueAsync's can stay parked with
    room free.
20. **Every pending read gets its own answer** (section 3): every pending `all()`, `any()` and `race()`
    Future settles, where TrueAsync's settle loop skips one after each it settles (probed `g1.php`, a
    deadlock); two pending `joinNext()` Futures take different tasks, where TrueAsync's both get the same one
    and the other result is lost (probed `g3.php`); a rejected `joinAny()` consumes its failures.
21. **Internal: the queue is a list of keys** (section 2): a start takes the head. TrueAsync scans `tasks`
    from the start for each start and for each "anything queued?" check (`task_group.c:300-311`, `848-889`),
    quadratic in the queue's length (probed `g11.php` on the debug build: 5 000 tasks at concurrency 10 in
    602 ms, 20 000 in 7 577 ms).
22. **Internal: a parked wait allocates nothing** (section 4), D29.
23. **Internal: the pin is an owner pointer in the scope** (section 5), since our scopes have no reference
    count and the collector needs the holder; TrueAsync has a flag and the event's count.
24. **Internal: a task's end does its work after the notify** (section 2), out of scheduler context, where our
    wait model runs callbacks (S3.md 4.6); TrueAsync does it in the callback.
25. **Internal: `getAwaitingInfo()` names the TASK_GROUP wait**, and **the collector finds a coroutine parked
    on a group or its Future that nobody else reaches** (sections 4, 6), as for the channel (S9-channel.md 8,
    items 6 and 9).
26. **Internal: `Cannot spawn tasks on a completed TaskGroup` is not ported**: COMPLETED needs a seal, and the
    closed check comes first (`task_group.c:1430-1438`), so no call reaches it.
27. **The `await_*` item message names `Completable`** (S9.27), since the check is `Completable` now;
    TrueAsync's names `Awaitable`. The narrowing itself is Edmond's (DECISIONS 2026-10-09 S9.26): TrueAsync
    takes a channel as an item and a token, types the `await_*` tokens `?Awaitable` and `timeout()` `Awaitable`.
    `trySpawn()`'s and the `allowZombies()` warning's messages are new.
28. **A cancel leaves a coroutine whose body has returned alone** (section 2), so a task whose
    closure drops the group keeps its result.
29. **No cancel marks an error handled** (section 3): an error nobody saw before `cancel()` or `dispose()` is
    printed at the closing. TrueAsync's `cancel()` marks the errors present handled.
30. **`ignoreErrors: true` handles the errors it skips**, in `all()` and `joinAll()` alike (section 3);
    TrueAsync's `all()` leaves them for the report.

Edmond accepted the six internal ones, items 21-26, on 2026-10-09 at 07:26 («я выбираю то, что лучше», "I choose
what is better"). Item 28 came later and changes every scope's cancel; Edmond chose it at 10:39 («если корутина уже была
выполнена - её нельзя по сути отменить»).

Kept as TrueAsync: every message but item 27's, "TaskGroup" in `TaskSet`'s; the closing cancelling the
group's scope while a task's child still runs in it; `race()`'s pick and its non-consuming read;
the group owning a passed `Scope` and running its tasks there; `cancel()` on an external scope cancelling that
scope's other coroutines too (probed `g8.php`); `finally()` on a completed group called at once; `all()`
settling on an open group once it settles; an idle group's own scope closed by S9.20's cascade; `joinAll()`
removing every entry; the queue limit default of `2 * $concurrency`; `count()` of `TaskGroup` counting every
entry; `awaitCompletion()` waiting for the tasks only (the site: "as well as other coroutines in the Scope").

## 9. Tests and measurements

**List** `tests/lists/S9.txt`, a block for layer 4, frozen in S9.28; 71 reference tests, each passing on the
reference build above (2026-10-09):

- `task_group/001`-`043` but `040`: 43 tests (two files carry the number 035);
- `task_set/001`-`027`: 27 tests;
- `channel/059`, which leaves `tests/lists/S9.excluded` in the same commit.

`task_group/040` needs `spawn_thread()` and ZTS and goes to `tests/lists/S9.excluded` with the reason of the
threads' outcome (`rfc-rule:2026-10-08 S10 outcomes (threads, not in the first version)`). `channel/058`
stays excluded, with the reason changed to `rfc-rule:2026-10-09 S9.26 (TaskGroup::dispose() seals the group)`.

A reference test whose output a decision changes carries `changed:` with its DECISIONS line, each judged by
the Critic in S9.28. By reading, before the code: the reports of `task_group/009`, `015`,
`035-all_synchronous_reject`, `036` and `037` (an error received through a rejected read, item 4) and of
`034`, `035-gc_traversal_all_states` and `task_set/011` (an unreported error now ends the request, item 15);
S9.28 runs the whole block and lists every other change before it freezes the block. Run, only `task_set/011`
changed (S9.28 in DECISIONS): the others print no second report, since TrueAsync is silent without a handler
and so their expected output never showed one, and in `034` a successful `any()` covers the failure.
`035-gc_traversal_all_states` never reads its first error, so it will need `changed:` once S9.29 lets it run.

By what each uses: `awaitCompletion()` (`task_group/028`-`031`, `043`, `task_set/023`), `foreach`
(`task_group/025`, `026`, `task_set/015`, `024`, `025`) and a park on a full queue (`task_group/035`
`gc_traversal_all_states`, `041`) name S9.29, 12 tests carrying `--XFAIL--`; the other 59 need only S9.28
(`task_group/030` passes now: its open-group check comes first).

Under the fuzz seeds of S9.30 the tasks of `task_group/025`, `026`, `task_set/015`, `024`, `025` end in any
order, and with completion order these tests print another order; a seed fails a test only by a crash, an
assertion, a sanitizer report, a leak, a hang (`tools/test.py:573-576`) or a new diagnostic line (`627-631`),
and an order is neither, so they pass the oracle as every order-dependent test does.

**Own tests**, one case each, written in the step that needs them; none repeats an operation hundreds or
thousands of times (Edmond, 2026-10-09, on `channel/101`): volumes belong to the measurements below only.

- S9.27: an `await_*` item and a token that are `Awaitable` but not `Completable` (a channel, a group)
  refused; `Scope::awaitCompletion()` and `awaitAfterCancellation()` with a channel refused; a `Timeout`
  item still refused; the changed tests above.
- S9.28 (a probe named here is the test's case, not its text; an item naming two cases is two tests):
  `g1.php` (two `all()` Futures), `g3.php` (two `joinNext()` Futures), `dispose()` refusing a later
  `spawn()`, `dispose()` starting no queued task, a group's `dispose()` closing a channel its scope owns (the
  own variant of `channel/058`), `g7.php` (`await()` refused), `g22.php` (`await_all()` refused), a group as
  the token of `await_all()` and of `Scope::awaitCompletion()` refused (S9.27's cases for a group),
  `g15.php`-`g16.php` (`cancel()` with queued tasks), `g10h.php` (no report after a caught `all()`), a
  rejected `race()` and a rejected `any()` read as received, a successful `race()` leaving a later failure
  unreported, a task added after it reported, `trySpawn()` with a free
  slot, with none, on a sealed group, with a duplicate key, and a `false` call taking no key; the warning for
  `allowZombies()` and none for `Scope::inherit()`; a group under the global scope interrupting its tasks on
  `cancel()`; a joinAny() entry removed when it settles later, a rejected `joinAny()` consuming its errors, a
  surplus `joinNext()` rejected at the completion, `joinNext()` in completion order; a settled open group
  taking new tasks, then `all()`; the queued tasks of a group whose external scope is cancelled ending
  cancelled, and of one whose scope is closed; the external scope freed while the group holds its object; the
  own scope surviving its parent's cancel and closed by its idle parent object's free; `unset()` of a group
  with running tasks cancelling them; a group dropped unclosed running its finally handlers after the code
  that follows `unset()`; the handlers run once when a handler keeps the group; a finally
  handler reading `getErrors()` and so leaving nothing to report; a finally handler that throws, reaching the
  scope's handler; a group whose last holder is a coroutine's argument, released after its finish; one whose
  last holder is a task's closure; `cancel()` after `close()` with a task running; `cancel()` on a completed
  group with a finally handler queued; a pending `TaskSet` read rejected by the destructor; the destructor's
  composite reaching the handler; a settled group dropped while a task's child coroutine runs, the child
  cancelled; a task whose closure drops the group keeping its result; a task whose closure drops the group
  after throwing keeping its own exception, with no cancellation as its `previous`; a `Scope` coroutine whose
  closure holds the scope's last reference keeping its result, `isCancelled()` false (`r28_lost.php`); the same
  with the scope kept alive elsewhere, unchanged (`r28_control.php`); a group dropped after the coroutines
  have gone at the request's end (an output handler) calling no finally handler, if PHP code can reach that
  point (S9.28 checks; else the case is recorded as read from code); a bailout while the finally run of a dropped group is
  pending (one fatal error covers it: a compile error takes the same path, S9.28); a group freed by its
  finally run reporting its errors; a rejected `all()` Future nobody awaits warning at its release; a
  coroutine awaiting `all()` while the tasks sleep not found by the collector; a bailout with queued tasks; a
  bailout with tasks running and the destructor skipped. Added in S9.28: a numeric string key is the integer key (Edmond, 13:56); a
  `trySpawn()` whose next integer a `spawnWithKey()` took throws once and the next call moves on.
- S9.29, the cases whose wait `awaitCompletion()` or a parked spawner is: `g4.php` (`race()` before the
  settle; after it), `g13.php` (the composite at the parent's handler), `g14.php` (a spawner parked on a full
  queue when `cancel()` comes), `g17.php` (no report after a cancel), `g9.php` and `g21.php` (an unreported
  error with no handler printing `Uncaught Async\CompositeException`); `g2.php` (two `foreach` consumers),
  `g5.php` (completion order); `close()` while `spawn()` waits on a
  full queue; the lost wakeups of section 4, by a task's start and by a
  woken spawner's cancellation; a cancelled `foreach` step and `awaitCompletion()`; a bailout
  while parked in each of the three waits, on debug and ASAN; finally handlers once per completion; for the
  collector, a `foreach` over a group whose tasks wait on a channel nobody else holds found, and not found: a
  group a running coroutine holds, a running task, a queued task behind one.

**Measurements** (S9.30, `dev/BENCHMARKS.md`), against the reference: B17, 100 000 tasks through a group of
concurrency 10, instructions and allocations per task; B18, 10 000 tasks without a limit and one `all()`.

**Core dependencies**: none.

## 10. Questions for Edmond

Edmond answered 38 questions on 2026-10-09; DECISIONS 2026-10-09 records them. Questions 35-37 came from the
Critics of this note, 38 from the Critic of its commit:

35. **The destructor's wait.** Answered: the destructor never waits; one algorithm everywhere (section 5),
    replacing answer 26's wait. The finally handlers never run twice.
36. **`cancel()` after an error nobody saw.** Answered: no cancel marks an error handled; an error nobody
    handled is printed (section 3).
37. **`ignoreErrors: true`.** Answered: it handles the errors it skips, in both classes (section 3).
38. **A cancel after the body returned.** Answered: the coroutine keeps its result, for every scope and group
    (section 2, section 8 item 28); TrueAsync's loss of the result is a bug (Edmond 10:39).
