# S9 notes, layer 1: Scope

Design note of the first S9 layer, 2026-10-07. The layer adds `Async\Scope`, `ScopeProvider`,
`SpawnStrategy` and `spawn_with()`; the global scope every coroutine joins by default; the
routing of a coroutine's unhandled error through its scope and the scope's parents; zombie
coroutines (the cancel slot's `is_safely`); `Scope::finally()` and `Coroutine::finally()` through
an iterator in a child scope (D21); and the child scope of an `await_*` call over a Traversable,
which `await/062` waits for. Names, signatures, messages and behaviour come from TrueAsync
(`ext/async` at `tests/lists/REFERENCE`, `1fdacf8`, file names alone below) and its core API (the
fork `true-async/php-src` `863f6dd90cf`, below `F:` for `Zend/zend_async_API.h`); a departure is
listed in section 9 with its reason. Behaviour marked "probed" was run on a debug build of that
fork with that `ext/async` on 2026-10-07 (short scripts `p1.php`-`p9.php`; each probe's setup and output is quoted where it is used); the
same build passes the 161 tests of `scope/`, `spawnWith/`, `coroutine/`, `edge_cases/`,
`bailout/`, `spawn/`, `sleep/007`, `stream/045` and `await/062`, 2 skipped.

The scheduler RFC defines no scope (`php-async-core-rfc/scheduler_rfc.md:42-61`, "Scope: what this
RFC deliberately does not define"); its only trace is the cancel slot's `is_safely`
(`zend_async_API.h:289-293`) and `get_coroutine_count`, which leaves zombies out (`:432`). The
layer therefore lives in the extension whole and needs no core change.

## 1. What the layer implements

| Kind | In this layer | Later |
|---|---|---|
| Classes | `Scope` (final), `ScopeProvider` and `SpawnStrategy extends ScopeProvider` (`scope.stub.php:7-30`) | `Context`: layer 2. `TaskGroup`, `TaskSet`, `Channel`, `Pool`: their layers |
| `Scope` | `inherit`, `__construct`, `provideScope`, `asNotSafely`, `allowZombies`, `spawn`, `cancel`, `awaitCompletion`, `awaitAfterCancellation`, `isFinished`, `isClosed`, `isCancelled`, `setExceptionHandler`, `setChildScopeExceptionHandler`, `finally`, `dispose`, `disposeSafely`, `disposeAfterTimeout`, `getChildScopes` (`scope.stub.php:36-116`) | |
| `Coroutine` | `finally(\Closure)` (`coroutine.stub.php:118`, D21) | `getContext()`: layer 2 |
| Functions | `spawn_with(ScopeProvider, callable, ...$args)` (`async.stub.php:38`) | `current_context`, `coroutine_context`, `root_context`, `request_context`: layer 2. `iterate()`: the iterators layer |
| Internal | the global scope; the child scope of `await_*` over a Traversable; the iterator core (`iterator.c`) that runs finally handlers; zombies in the cancel slot and in `get_coroutine_count` | `iterate()` on the same iterator core |

TrueAsync has no `current_scope()`; neither does this layer. The request scope
(`F: request_scope`, `request_context()`) belongs to Context and comes with layer 2.

## 2. The scope

**Two allocations, as TrueAsync** (`F:1474-1482`): the internal `async_scope_t` and the PHP object
`Async\Scope`. The internal scope outlives its object until its last coroutine finishes; the
object's destruction starts the disposal. The global scope has no object.

```c
struct _async_scope_s {
	async_event_t event;                  /* waiters of awaitCompletion(); type bit and flags */
	zend_object *scope_object;            /* NULL for the global scope and after the object died */
	async_scope_t *parent_scope;
	async_scopes_vector_t child_scopes;
	async_coroutines_vector_t coroutines; /* members, each knowing its index (section 9, item 1) */
	uint32_t active_coroutines_count;     /* members that are not zombies */
	uint32_t zombie_coroutines_count;
	zend_string *filename;                /* where the scope was created, for awaiting info */
	uint32_t lineno;
	zend_fcall_info_cache exception_handler;       /* setExceptionHandler(); not initialized when unset */
	zend_fcall_info_cache child_exception_handler; /* setChildScopeExceptionHandler() */
	HashTable *finally_handlers;          /* lazy */
};
```

- The flags are TrueAsync's (`F:1560-1569`): CLOSED, DISPOSE_SAFELY, CANCELLED, DISPOSING, and the
  waiters' "error handled" bit of section 4, each a named bit among the event's own (13-30,
  `true_async_API.h:223-234`) that no other awaitable test reads: bit 30 is the Timeout's
  (`src/timeout.h:26`) and `await.c` tests it on any event, so the scope takes none of the bits
  `await.c` reads. `OWNER_PINNED` is a TaskGroup's and comes with that layer.
- The scope's functions are plain C functions of `src/scope.c`, not the fork's function pointers
  in the struct (`F:1494-1529`): the fork has one implementation of each, and TaskGroup, the only
  other owner, uses the same ones (`task_group.c`).
- The coroutine keeps its pointer `scope` (`src/coroutine.h:34`, D11) and gains the reserved flag
  `ASYNC_COROUTINE_F_ZOMBIE` (bit 20, `dev/plans/S3.md:70`), its index in the scope's vector, and
  `finally_handlers` (lazy `HashTable *`, as `coroutine.c` of TrueAsync). The size check in
  `src/coroutine.h` moves with them; the 512 B bin is kept or the step says why not.
- Two request globals in `ASYNC_G`, both TrueAsync's: the global scope (`F:2205` `main_scope`), and the count of zombies, so
  `get_coroutine_count` returns the registry less the zombies, as the slot's contract says
  (`zend_async_API.h:432`); TrueAsync keeps the active count in the core's globals (`F:2199`), which
  the RFC core has not. Neither is touched on a hot path: the count changes when a coroutine
  becomes a zombie and when a zombie leaves the registry, at each of the four places a coroutine
  leaves it (`src/coroutine.c:413`, `src/future.c:545`, `src/await.c:1222`, `src/true_async.c:295`).

## 3. Which scope a coroutine joins

`spawn()` puts the coroutine in the current coroutine's scope, or the global scope when there is
no current one; `$scope->spawn()` and `spawn_with($scope, ...)` in that scope; `spawn_with($provider,
...)` in what `provideScope()` returns: `null` means the current scope as for `spawn()`, anything
but a `Scope` or `null` is refused (`async_API.c:32-58`; `spawnWith/005`, `spawn/019`). A current
coroutine with no scope of its own (a Fiber's, section 3 below) spawns into the global scope, as
TrueAsync (`async_API.c:91-95`). A closed scope refuses with "Cannot spawn a coroutine in a closed scope"
(`async_API.c:103-106`). A `SpawnStrategy` gets `beforeCoroutineEnqueue()` before the enqueue and
`afterCoroutineEnqueue()` after it; the array the first returns is released unread, as TrueAsync
(`async_API.c:117-200`). The coroutine enters the scope's vector before its first enqueue: D11's
hook point, which S3 planned and the code never got (`src/true_async.c` `Async_spawn`,
`src/scheduler.c:1417` the CREATED case).

The global scope is created with the main coroutine at the scheduler launch
(`scheduler.c:1201-1225`) and has DISPOSE_SAFELY. `Scope::inherit()` without an argument takes the
current scope, at the top level the global one; `new Scope()` has no parent and no DISPOSE_SAFELY
(`scope.c:147-192, 1304-1381`, TrueAsync `4a3c121`).

Coroutines the extension creates for itself join the scope TrueAsync gives their counterpart: the
`await_*` iterator coroutine a child scope of the caller's (section 8), a finally coroutine the
child scope of section 7, the `disposeAfterTimeout()` coroutine the global scope
(`scope.c:699-723`). S5's chain drain serves chains of every scope and joins the global scope
(S5.md section 8, item 1, keeps its departure from the iterator).

Coroutines the core creates: the RFC core creates them only through `ZEND_ASYNC_GC_NEW_COROUTINE`
(`Zend/zend_gc.c:2140, 2226`, `Zend/zend_objects_API.c:103`, `Zend/zend_execute_API.c:268` of the
pinned core), which falls back to `new_coroutine` when the slot is empty (`zend_async_API.h:615-618`),
and ours is empty today (`src/scheduler.c:2191-2215`). The layer fills `gc_new_coroutine`: its
coroutines join one private root scope of the request without DISPOSE_SAFELY, as the fork's GC
scope (`zend_gc.c:2243-2246`), so a user scope's `cancel()`, `dispose()` or a sibling's error never
reaches the garbage collector's destructors. `new_coroutine` then serves only other extensions and
follows `spawn()`. A Fiber's coroutine comes from the fiber slot (`src/scheduler.c:1873`) and has no
scope, as the fork (`zend_fibers.c:1305` passes NULL). The scheduler's own coroutine has no scope;
TrueAsync gives it a scope that refuses spawn (`async_API.c:108`), ours is never in the registry and
nothing spawns through it.

`Scope::inherit()` at the top level before any spawn gives a scope with no parent in TrueAsync,
since its global scope exists only after the scheduler's launch (`scope.c:165-185`); ours exists
from the script's first opcode ("the top-level script is a coroutine from its first opcode",
`zend_async_API.h:294-299`), so the scope is always a child of the global one (section 9). With
section 4's route this changes one program: `Scope::inherit()->asNotSafely()` before any spawn, then
an unawaited error in it. TrueAsync cancels only that scope; ours climbs and cancels main and every
coroutine of the request, as `p6.php` shows TrueAsync doing once anything has been spawned.

## 4. A coroutine's unhandled error

TrueAsync's route, at the coroutine's finish (`coroutine.c:700-723`, `scope.c:942-1080`). The error
ends at the coroutine when it is a cancellation, or when a waiter was parked on the coroutine at
the finish: the waker's resolve callback marks the error handled whenever it passes one on (fork
`Zend/zend_async_API.c:1261-1266`), and `coroutine.c:704-709` skips the route. Probed (`p7.php`):
`$a` throws while main waits in `await($a)`; `$b`, spawned after `$a`, still runs. Any other error
goes to the coroutine's scope (`catch_or_cancel` in CATCH mode), with `is_safely` taken once, from
the DISPOSE_SAFELY flag of the coroutine's own scope (`coroutine.c:719`), and passed unchanged to
every child scope, coroutine and parent the route reaches (`scope.c:1029-1031, 1041, 1067-1068`):

1. a scope that received the error from a child scope tries its `setChildScopeExceptionHandler()`
   handler, then every scope its `setExceptionHandler()` handler, called as
   `fn(Scope $scope, Coroutine $coroutine, Throwable $e)`; a handler that returns without throwing
   ends the route (`scope.c:996-1002, 1594-1718`). From the scheduler's context the handler runs in a
   coroutine spawned to throw the error (`scope.c:1608-1620`); ours never routes from there (section
   9, item 9). A scope whose object has died gets a stand-in `Scope` object for the call
   (`scope.c:1620-1636`). A handler that throws has its exception go on in place of the error, with
   the error as its previous (`scope.c:1010-1014`, probe `s9.3/q3.php`);
2. otherwise the scope is marked CANCELLED, its child scopes and coroutines are cancelled (each
   with a fresh cancellation, not the error), and the scope's waiters are woken with the error; a
   waiter that takes it marks the scope's event handled and the route ends there
   (`scope.c:1004-1062`). With safe disposal the cascade's first zombie mark wakes the waiters as
   completed (no active coroutine is left), before the error's wake: the waiter returns, and the
   error goes on to the parent, on the reference as on ours (probes `s9.4/w1.php`, `w2.php`;
   `scope/082`, `083`). A child scope the cancellation leaves empty is closed (section 5), so
   after an unhandled error of the global scope its empty child scopes refuse a spawn while the
   global scope still spawns, on the reference as on ours (probe `s9.3/q14.php`, `scope/074`);
3. the route then continues in the parent scope, up to the global one (`scope.c:1064-1069`).

After the route, the error is rethrown into the scheduler when nobody holds the coroutine object,
and kept for a later `await()` otherwise (`coroutine.c:737-745`), which is S3's rule today
(`src/coroutine.c:453-460`).

**The waiter's mark.** Our `await()` record marks nothing at its wake on purpose
(`src/scheduler.c:1894-1899`): "handled" in our finalize becomes "observed" (`src/coroutine.c:439-460`),
and a woken waiter may be cancelled before it reads the outcome. The route needs only the fact that
a waiter was woken with the error; how it gets it without changing "observed" is in section 9,
item 6. The scope's own waiters of step 2 sit on the scope's event, not on the coroutine's
callbacks: whether one of them took the error decides only whether the route stops at that scope.

**What the route does to the global scope**, by the origin's flag. Probed:

- an error of a coroutine of the global scope (DISPOSE_SAFELY): every coroutine that has not
  started is cancelled and every started one, main included, becomes a zombie and runs on.
  `p5.php`: `$failing` (held by main) throws while `$queued`, spawned after it, has not run;
  TrueAsync prints `queued cancelled: true` and `Async\AsyncCancellation: Coroutine cancelled`.
  `p4.php`: three coroutines in `delay(50)` and main; one held coroutine throws; `runtime_stats()`
  gives `coroutines_active` 4 before and 0 after, and the four finish normally. Our extension today
  runs `$queued` (no scope; read from `src/coroutine.c:389-477`, not run);
- an error of a coroutine of `Scope::inherit()->asNotSafely()`, a child of the global scope: the
  route climbs and cancels every coroutine of the request for real. `p6.php`: main parked in
  `delay(100)` gets `AsyncCancellation: Coroutine cancelled`, a coroutine in `delay(50)` too, and a
  queued one never runs;
- an error in `new Scope()` (no parent, no DISPOSE_SAFELY) stays in it. `p2.php`: the sibling in
  `delay(50)` gets `AsyncCancellation: Coroutine cancelled`, `isCancelled()` is true, main's
  `await($b)` still gets the error.

In all three the error itself still reaches whoever awaits the coroutine, and an unheld one still
ends the request through the graceful shutdown, as today. What the global scope does with such an
error is the question of section 12.

## 5. Cancellation, zombies, disposal

- `cancel(?AsyncCancellation)` is `catch_or_cancel` in CANCEL mode with the scope's DISPOSE_SAFELY
  (`scope.c:282-299`): the error goes to the child scopes, recursively, and to the coroutines as
  given, and the route does not climb to the parent. A scope that `cancel()` leaves with no members
  is closed (`scope.c:964-974`); `scope/004` and `026` need it, so it comes with `cancel()` in S9.2.
- `dispose()` closes the scope and cancels as the flag says, `disposeSafely()` closes it with
  `is_safely` true, `disposeAfterTimeout($ms)` arms an S4 Timer op whose fire spawns a coroutine in
  the global scope that cancels the scope (`scope.c:627-784`). A scope with no members left is
  closed and its finally handlers run at once (`scope.c:964-974`).
- `isFinished()`, `isClosed()` are true and `isCancelled()` reads the object's own copy of the flag
  once the internal scope is gone (`scope.c:485-519`); `getChildScopes()` lists only child scopes
  that still have an object (`scope.c:786-808`).
- The object's destruction disposes the scope, or cancels it with "Scope is being disposed due to
  object destruction" (`scope.c:1395-1416, 1476-1500`).
- Zombies: our cancel slot stops ignoring `is_safely` (`src/scheduler.c:1834-1837`). A started
  coroutine is flagged ZOMBIE, leaves its scope's active count and the zombie-less coroutine count,
  and runs on; the error passed is released; a coroutine that has not started is cancelled as now
  (`coroutine.c:956-992`). A protected coroutine keeps the deferred cancellation, as TrueAsync's
  protection check comes first (`coroutine.c:937-950`).
- A zombie stays in the registry, so the deadlock count, `get_coroutines()`, graceful shutdown,
  `exit()` and the D16 deadline see it as today; TrueAsync counts the registry in its deadlock too
  (`scheduler.c:761-762`). Probed (`p3.php`): a script whose last scope was `disposeSafely()`d prints
  `main end` and then `zombie finished`, so a zombie keeps the request running. TrueAsync's
  documentation says the opposite ("Zombie coroutines do not prevent the application from shutting
  down", `zombie-coroutines.md`); the layer follows the code.

## 6. Waiting on a scope

`awaitCompletion(Awaitable $cancellation)` waits until the scope and its child scopes have no active
member; `awaitAfterCancellation(?callable $errorHandler, ?Awaitable $cancellation)` waits for
zombies too, only on a cancelled scope, and passes each error to the handler as `fn(Throwable $e,
Scope $scope)` (`scope.c:301-483, 810-864`). Both refuse a waiter that is a member of the scope or
of a child scope (`scope.c:866-894`; ours walks up, section 9, item 12), mark the token used, return
at once on a closed or finished scope, and throw "The scope has been cancelled" on a cancelled one.

The scope is an event of S4's wait-record layer: a new kind SCOPE (`async_wait_kind_t`) with its
`info` (`await: scope created at <file>:<line>`, after TrueAsync's `scope_info`, `scope.c:1166-1182`),
its wake enqueuing only (D26), and the token in `records[1]` as `async_await_coroutine()` does
(`src/scheduler.c:1933-2007`). A member's finish or zombie mark notifies the scope's waiters and
then its parents' when they complete in turn (`scope.c:1575-1592`). The layer's type-dispatching
helpers in `src/await.c` (`async_awaitable_release`, `await_outcome`, `token_record_info`,
`await_record_info`, `await_trigger_of`) assume "not a coroutine and not a Timeout is a Future"; a
scope is never an `await()` target or an `await_*` item (TrueAsync accepts neither), so none of
them changes, and the SCOPE kind is used only by the two methods.

**The collector** (S7.md 3.4 and 10, `dev/DECISIONS.md` 2026-10-06 and 2026-10-07): a scope may
cancel coroutines it does not hold through PHP values. The membership vector holds bare pointers, as
TrueAsync's (`scope.c:47`), and a scope belongs to no one (Edmond, 2026-10-07), so the walk never
follows a scope's members as references. Of the three edges this note first planned:

1. a held Scope object reaches every member of its subtree, since `cancel()` cascades into child
   scopes (`scope.c:1020-1044`), whatever the safe flag: S7.7's reach nodes
   (`async_scope_collector_reach()`, S7.md 10);
2. a member reaching the members of its scope's subtree and of every ancestor's through the route
   is left out by design (S7.md 10, DECISIONS 2026-10-07): any live coroutine could make an unsafe
   origin later, so a tree would be all or nothing; the oracle excuses the route's wakes and cancels
   (`scope_hand_out_found()`);
3. S9.9: the SCOPE kind's `collector_target` reports the awaited scope's completion node, a reach
   node keyed by the scope's event (`async_collector_report_reach_target()`), live once a coroutine
   of the subtree is, zombies included (`async_collector_report_reach_source()` per member, and a
   child scope's node per child), and owning no reference. The waiter wakes when the last active
   member finishes or is marked a zombie, when a member's error passes the scope on its route, or on
   a cancel of the scope or an ancestor. A member that is no candidate is live already, a candidate
   is live through a live holder by edge 1, and a waiter is parked only while the subtree has an
   active member; so the waiter is live once any member is, and is found only when every member is.
   Any member suffices because one can throw into the route even when another never finishes; a miss
   is possible there, a false finding is not. One node per scope and run, so a run adds edges in the
   members plus the waiters, not their product (the Critic: an edge per member per waiter makes 10
   million for 10 000 members and 1 000 waiters; with one node per scope a run over them took 42 ms
   on the debug build, 2026-10-07).

Two cancels hold no Scope object:

- the route's, at every level, left out as in edge 2: `scope_hand_out_found()` also hands out the
  found waiters of each scope it visits, since the level's cancels wake them wherever they run, so
  the oracle excuses them (`scope/090`);
- the `await_*` iterator's (section 8), which cancels its scope when the walk throws: the iterator
  coroutine is reported as a holder of that scope's reach node (`iterator_coroutine`, cleared when the
  walk finishes), so the walk counts it: while it can still throw, the subtree's members and their
  waiters are not found (`scope/091`; S7.7 found them).

The oracle runs at the notify sites, before the wake, since the notify runs its callbacks in
scheduler context: `scope_notify_completion()` checks the scope's waiters against the member that
finished or became a zombie (`async_collector_check_wake()`, which excuses one handed out or in the
bailout), and `async_scope_cancel()` against the running code (`async_collector_check_event_wake()`).
The `cancel` policy hands out main too, which it does not cancel and the coroutines it cancels may
wake (`scope/093`). The route's own notify needs none: the hand-out ran first. (The Critic and the Sage, 2026-10-07;
S9.9's Critic, 2026-10-07.)

## 7. Finally handlers (D21)

`Scope::finally(callable)` and `Coroutine::finally(\Closure)` store the callable; at the scope's
disposal or the coroutine's finish the handlers run through an iterator in a child scope of the
scope (or of the coroutine's scope), with the scope or the coroutine as the argument
(`coroutine.c:1225-1350`, `scope.c:1720-1753`). One handler error goes on as itself, two or more as a
`CompositeException` (`coroutine.c:1195-1215`; `coroutine/017`, `bailout/015` expect "Caught single
exception"), through the route of section 4. A handler added to a disposed scope or to a finished
coroutine runs at once (`scope.c:581-625`, `coroutine.c:1395-1411`; `coroutine/015`). TrueAsync adds
a reference to the coroutine for its handlers (`coroutine.c:1349`) before the "nobody holds it"
check (`coroutine.c:737`), so an unheld coroutine with handlers keeps its unhandled error instead of
ending the request; the layer keeps that and says so in a test.

Bailout: `Coroutine::finally` handlers are destroyed unrun when the coroutine took a bailout
(`coroutine.c:1334-1340`). Scope handlers have no such check in practice (`scope.c:1730` reads
`ZEND_ASYNC_SCOPE_IS_BAILOUT`, which nothing sets), and `bailout/013`-`015` expect them to run after
the fatal error and the shutdown functions, when the scope object is destroyed. Our scheduler runs
nothing after a bailout (`src/scheduler.c:938`); how the core's shutdown destructors and the
finally coroutine they spawn run at that point is traced in S9.6 on those three tests before the
code, and an answer that needs a change of the bailout rule goes to Edmond.

D21 says "through an iterator in a child scope, no interim version", so the layer ports the core of
`iterator.c` (613 lines): `async_iterator_new`, its worker coroutines in the iterator's scope, the
microtask that refills them, `async_iterator_run_in_coroutine` and the exception path
(`iterator.c:235-613`). `iterate()` reuses it in the iterators layer. S5's chain drain stays as
built (S5.md section 8, item 1). The port follows our contracts where they differ from the fork's,
which S5 already met (`src/future.c:458`):

- the RFC core's microtask: our defer slot takes the caller's reference (`src/scheduler.c:1857-1868`)
  where TrueAsync's add takes one of its own (`async_API.c:244`), and `ZEND_ASYNC_MICROTASK_RELEASE`
  calls `dtor` and then frees (`zend_async_API.h:341-349`, `src/scheduler.c:382`), while
  `iterator_dtor` counts down and frees itself (`iterator.c:159-206`). The iterator's `dtor` only
  releases what it holds;
- a worker's end: TrueAsync's `extended_dispose` runs at the coroutine's finish
  (`coroutine.c:687-691`), ours at the object's free (`src/coroutine.c:180`), which depends on who
  else holds the object. The iterator learns that a worker ended from a finish handler
  (`async_finish_handler_add`, `src/true_async_API.c:341`), as the interrupt coroutine does
  (`src/scheduler.c:852`).

## 8. `await_*` over a Traversable

TrueAsync runs the iterator coroutine of an `await_*` call over a Traversable in a child scope of
the caller's scope, so a coroutine `current()` spawns belongs to it, and an exception of the
Traversable cancels that scope (`async_API.c:1072-1092`, `iterator.c:601-613`). The layer gives
S5's iterator coroutine (`src/await.c:1210-1221`) that child scope and cancels it on the
Traversable's exception; `await/062` loses its `--XFAIL--` in that step.

## 9. Departures from TrueAsync

1. **A member leaves its scope in O(1)**: the coroutine keeps its index in the scope's vector, and
   the removal moves the last member into the gap and updates its index, as S5.6 did for callbacks.
   TrueAsync searches the vector (`scope.c:58-74`), which made its 100 000-coroutine run 88 % scope
   bookkeeping (`dev/plans/S3.md:1270`).
2. **Waits on a scope are S4 wait records**: a bailout while parked in `awaitCompletion()` or
   `awaitAfterCancellation()` leaves no waiter in the scope's vector, which TrueAsync's parks
   without `zend_try` do (`scope.c:370, 439`; `dev/plans/S3.md:1382`, reference bug 14).
3. **The scope reports its coroutines to the collector** through non-owning edges (section 6);
   TrueAsync keeps bare pointers (`scope.c:47`) and has no collector.
4. **No function pointers in the scope struct** (section 2).
5. **The coroutines of `gc_new_coroutine` share one private root scope** (section 3): the fork
   puts the GC's destructors in a child of its own GC scope (`zend_gc.c:2127-2128, 2243`) and the
   shutdown destructors in the main scope (`zend_objects_API.c:102`); the RFC core passes one slot for
   both.
6. **The route is skipped when the coroutine's notify woke a waiter record**, not by a mark the
   waiter's wake sets (section 4): `async_callbacks_notify` returns whether it called a record
   (`ASYNC_CALLBACK_F_RECORD`, `true_async_API.h:78`), which covers `await()`, `await_*` and the
   tokens waiting on the coroutine, and the route runs only when no record was called, the error is
   not handled and is no cancellation. TrueAsync's resolve callback marks the coroutine handled
   (fork `Zend/zend_async_API.c:1261-1266`), which in ours would mean "observed" and drop an
   exception the woken waiter never read (`src/coroutine.c:147, 443-446`). The teardown wake in
   `async_callbacks_free` (`src/true_async_API.c:169`) runs only after a callback threw, which ends
   the request, and is not counted. (The Sage, 2026-10-07, Final.)
7. **`Scope::inherit()` at the top level always has the global scope as parent** (section 3), so
   an unawaited error in `Scope::inherit()->asNotSafely()` made before any spawn cancels the whole
   request, where TrueAsync's parentless scope keeps it local.

8. **A `SpawnStrategy` whose scope has no object gets a stand-in `Scope`** for both hooks, the one
   the route's handler call already builds (`scope.c:1620-1636`). TrueAsync wraps the missing
   object as `ZVAL_OBJ(NULL)` (`async_API.c:130, 189`) and crashes: probed (`p9.php`), a strategy
   whose `provideScope()` returns `null` at the top level, after one spawn, ends in a segmentation
   fault. A reference bug, for Edmond.

9. **A handler cannot park** (S9.3, `scope/065`): it runs while its coroutine is finished and still
   current, where `suspend()`, `await()` and `delay()` throw `Error`, so TrueAsync's coroutine spawned
   to throw the error from the scheduler's context (`exceptions.c:335`) is not ported: no route of
   ours runs there. TrueAsync's handler parks the finished coroutine (probe `s9.3/q2.php`); ours has
   given its fiber context back and left the registry before its notify (`src/coroutine.c`, finalize),
   so a parked handler would be invisible to `exit()`, the deadlock count and the bailout walk.
   An `Error` the handler does not catch is its exception (step 1). TrueAsync's parking is a side
   effect of its finalize running on the live fiber, and a reference bug for Edmond: a parked handler
   survives `$scope->cancel()` and its coroutine's `cancel()`, the script's end waits for it, and one
   stuck on a pending Future reports the deadlock twice (probes `s9.3/q15.php`, `q16.php`). Running
   handlers in a coroutine of their own was weighed and rejected: it reorders the route against the
   cascade, which TrueAsync runs after the handler (the Sage, 2026-10-07, Final).
10. **`exit()` in a handler ends the request** as `exit()` in a coroutine does (D16), and the route
    stops there (S9.3, `scope/066`). TrueAsync chains the exit object as the handler's exception and
    goes on: `exit(3)` in a handler ends that request with status 255 (probe `s9.3/q13.php`).
11. **A child scope handler that throws leaves the scope's own handler uncalled**, written as one
    choice of handler (S9.3, `scope/063`). TrueAsync calls the own handler with the child handler's
    exception pending, which `zend_call_function` refuses without a call: the same outcome (probe
    `s9.3/q8.php`).
12. **`awaitCompletion()` finds a waiter of the scope by walking up from the waiter's scope**
    (S9.4, `scope/045`, `051`), where TrueAsync walks the awaited scope's subtree down, refusing past
    a depth of 64 (`scope.c:866-894`). Same answer for every tree, in the depth of the waiter's scope
    instead of the subtree's size, and no depth limit is left to refuse a deep tree.
13. **`awaitCompletion()` in a finished coroutine throws** "awaitCompletion() requires a running
    coroutine" (S9.4, `scope/079`), as `await()` does there; TrueAsync parks it, as item 9 says
    for any wait in a handler.
14. **A `provideScope()` declared to return by reference works** (S9.9, `spawnWith/017`): the
    reference is unwrapped; TrueAsync's `async_provide_scope()` (`async_API.c:32-58`) reads the
    reference's type and throws "Scope provider must return an instance of Async\Scope" for a valid
    null or Scope (the S7 thread's Critic).

The probes of S9.3 are `/mnt/project-files/s9/probes/s9.3/q1.php`-`q16.php`; on the reference and
on ours they print the same but for items 9 and 10 and for S3's report of an unobserved exception of
a held coroutine at the end, which TrueAsync drops (`q11.php`, `q12.php`, `scope/060`).

Kept as TrueAsync and recorded for Edmond, not changed here: a zombie keeps the request running
(section 5); the array `SpawnStrategy::beforeCoroutineEnqueue()` returns is released unread
(`spawnWith/007`-`009` return `[]`). What the global scope does with an unhandled error is
section 12.

## 10. Tests and the core

**List** `tests/lists/S9.txt`, frozen in S9.1 under a comment naming this layer (later layers
append their own): 91 tests, each with `--XFAIL--` naming its step (S9.2 35, S9.3 5, S9.4 13,
S9.5 14, S9.6 24, assigned by the methods each test calls; a test that passes earlier loses its
section in the step that makes it pass). Counted on 2026-10-07 over the
reference at `REFERENCE`:

- `scope/`: 56 of 57; `scope/052` reads `current_context()` and waits for layer 2 in
  `tests/lists/S9.excluded`;
- `spawnWith/`: all 12;
- 21 `component:S9` lines of `tests/lists/S3.excluded` (spawn_with 4, Scope 2, Scope::finally 3,
  Coroutine::finally 7, Scope and Coroutine::finally 3, allowZombies 1, awaitCompletion 1), which
  leave that file in the same commit; the other 10 (Context 3, Channel 1, the six statistics
  functions) stay;
- `sleep/007` and `stream/045` from `tests/lists/S6.excluded`.

`await/062` stays in `S5.txt`; its `--XFAIL--` names S9.4. `edge_cases/010` needs the INI renamed
to `true_async.debug_deadlock` with a `changed:` tag, as `edge_cases/001`-`003` (DECISIONS
2026-10-01). `bailout/013`-`015` skip on ASAN lanes (`USE_ZEND_ALLOC=0`), as `bailout/001`-`003`.
TrueAsync's fuzzy features over scopes (`fuzzy-tests/`, 7 files) are not ported; no list has ported
that directory.

**Own tests**, written in the step that needs them:

- S9.2: the O(1) removal under 100 000 members (members leave out of order, the scope ends empty);
  the core's `get_coroutine_count` with zombies, through `TrueAsync\Test\coroutine_count()`
  (`src/test_hooks.c:1243`);
- S9.3: `p5.php`, `p6.php` and `p7.php` as `scope/059`-`061` (`p4.php` reads `runtime_stats()`,
  which no list has); `scope/062`-`075` for the handlers' calls, release and departures (section 9,
  items 9-11) and the Critic's findings;
- S9.4: a bailout while parked in `awaitCompletion()`; `scope/076`-`083` for the wake by
  `cancel()` and by the route, the deadlock report, the refusal in a handler, the `await_*`
  iterator's scope, and safe disposal's early wake (section 4, step 2);
- S9.9: the collector's edges of section 6, `scope/084`-`093`: a waiter in `awaitCompletion()` found
  with its scope's stuck members, not found while a member may finish or the scope is held, a member
  of a child scope, a found waiter woken by a handed-out member's end and error, the `cancel` policy,
  the route's excuse, the `await_*` iterator as its scope's holder, a fatal error after a run, and
  main found and woken by a coroutine the policy cancelled;
- S9.5: `p3.php` (a zombie keeps the request running).

**Core dependencies**: none. `is_safely` and `get_coroutine_count` are in the pinned core
(API version 2).

**Measurements** (stage review, `dev/BENCHMARKS.md`): spawn to finish with the scope against the
same code before S9.2, and against the reference, at 1, 1 000 and 100 000 coroutines; one
`awaitCompletion()` over N in 1 000 and 100 000 members.

## 11. Steps

- S9.1 This note and `tests/lists/S9.txt` (the layer's frozen list).
- S9.2 The scope and the global scope; spawn into a scope; `Scope` without waiting, errors and
  disposal (`inherit`, `__construct`, `spawn`, `cancel` in CANCEL mode with its cascade into child
  scopes and coroutines and the close of a scope it leaves empty, `is*`, `getChildScopes`,
  `asNotSafely`, `allowZombies`); `spawn_with`, `ScopeProvider`, `SpawnStrategy` (with item 8 of
  section 9), `provideScope()` errors; the coroutines of section 3; the
  cancel slot's `is_safely`, `ASYNC_COROUTINE_F_ZOMBIE`, the zombie counts and
  `get_coroutine_count` (moved here from S9.5: `cancel()` and the route pass `is_safely`, the Sage,
  2026-10-07, Final).
- S9.3 The error route of section 4 (CATCH mode) with both handlers, the notify's return of item 6
  of section 9 and step 2's cascade of fresh cancellations; no waiters yet.
- S9.4 Waiting (section 6), the route's wake of the scope's waiters with the error (section 4,
  step 2) and the `await_*` child scope (section 8).
- S9.9 The collector's edges of section 6, split from S9.4 to wait for S7.7: the SCOPE kind's
  completion node, the route's hand-out of waiters, the `await_*` iterator as its scope's holder.
- S9.5 Disposal (section 5): `dispose*`, `awaitAfterCancellation`, the object's destruction.
- S9.6 The iterator core and both `finally` methods (section 7), the bailout trace first.
- S9.7 Stage review: the Critic over S9.2-S9.6, coverage of `src/scope.c` and the iterator, Mull on
  the layer's diff, the fuzz oracle over 100 seeds, the measurements of section 10.
- S9.8 Security pass by `dev/SECURITY.md`.

## 12. Question for Edmond

When an unhandled error of a coroutine, with no waiter and no handler, reaches the global scope,
the global scope can:

1. do as TrueAsync: cancel with the flag of the scope where the error started. A coroutine of the
   global scope makes every started coroutine a zombie and cancels every unstarted one (`p4.php`,
   `p5.php`); a coroutine of `Scope::inherit()->asNotSafely()` cancels main and every coroutine of
   the request (`p6.php`);
2. cancel with its own flag only: zombies and cancelled unstarted coroutines, never a real
   cancellation of main;
3. do nothing: the error touches only its own coroutine, kept for `await()` or reported as
   unobserved, as today.

Measured on the reference build with each variant patched into `scope.c` (an experiment, not a
design): 1 and 2 pass the same 862 reference tests (one `io/082` failure in all three, a network
test); 3 fails 18 more, two of them in this layer's list (`scope/017`, `scope/018`: a finally
handler's error no longer reaches `setExceptionHandler()`) and 16 of `task_group/` and `task_set/`;
their cause is not traced.

**Edmond, 2026-10-07: option 1** ("сделай как в TrueAsync"); recorded in `dev/DECISIONS.md`.

(The Sage worded the question, 2026-10-07, Final.)
