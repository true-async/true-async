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
child scope of section 7; TrueAsync's `disposeAfterTimeout()` coroutine of the global scope
(`scope.c:699-723`) has no counterpart, its timer cancels in place (section 9, item 15). S5's chain drain serves chains of every scope and joins the global scope
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
- `dispose()` is `cancel()` with no error and the scope's flag, `disposeSafely()` the same with
  `is_safely` true (TrueAsync's `ZEND_ASYNC_SCOPE_CLOSE`, `F:1531`): a scope with nothing left to
  cancel is closed and its finally handlers run at once (`scope.c:964-974`), and since S9.20 so are its
  child scopes with no coroutine of their own (DECISIONS 2026-10-08), one whose members still
  run is only cancelled and still accepts a spawn, on the reference as on ours (probe `s9.5/d6.php`,
  `scope/101`). `disposeAfterTimeout($ms)` arms an S4 Timer op on the reactor's waits, so a script that
  ends by itself waits for it as for TrueAsync's libuv timer (`d1.php`, `scope/102`); its fire cancels
  the scope with "Scope has been disposed due to timeout" (`scope.c:627-784`; section 9, items 15-16).
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
zombies too, only on a cancelled scope, until no coroutine of the subtree is left (section 9,
item 17), and passes each error a member's route brings while it waits to the handler as
`fn(Throwable $e, Scope $scope)`, or throws it without one (`scope.c:301-483, 810-864`). Both refuse a
waiter that is a member of the scope or of a child scope (`scope.c:866-894`; ours walks up, section 9,
item 12) and mark the token used. `awaitCompletion()` returns at once on a closed or finished scope
and throws "The scope has been cancelled" on a cancelled one; `awaitAfterCancellation()` returns at
once on a gone scope, a closed one that is not cancelled, or one with nothing left to wait for.

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

Three cancels hold no Scope object:

- the route's, at every level, left out as in edge 2: `scope_hand_out_found()` also hands out the
  found waiters of each scope it visits, since the level's cancels wake them wherever they run, so
  the oracle excuses them (`scope/090`);
- the `await_*` iterator's (section 8), which cancels its scope when the walk throws: the iterator
  coroutine is reported as a holder of that scope's reach node (`iterator_coroutine`, cleared when the
  walk finishes), so the walk counts it: while it can still throw, the subtree's members and their
  waiters are not found (`scope/091`; S7.7 found them);
- S9.5: the `disposeAfterTimeout()` timer's, which no walk reaches: while it is armed on a scope that
  is not cancelled, whose fire would only close it (`scope/108`), the scope's reach node is live (`async_collector_report_live_reach()`), so its subtree's members and their waiters are
  not found, and its cancel in scheduler context wakes no found waiter (`scope/098`). The waiter of
  `awaitAfterCancellation()` uses the SCOPE kind and its completion node, zombies counting as sources
  already (`scope/097`).

The oracle runs at the notify sites, before the wake, since the notify runs its callbacks in
scheduler context: `scope_notify_completion()` checks the scope's waiters against the member that
finished or became a zombie (`async_collector_check_wake()`, which excuses one handed out or in the
bailout), and `async_scope_cancel()` and a Future's completion against the running code, both before their
notify (`async_collector_check_records_wake()`, S9.7).
The `cancel` policy hands out main too, which it does not cancel and the coroutines it cancels may
wake (`scope/093`). The route's own notify needs none: the hand-out ran first. (The Critic and the Sage, 2026-10-07;
S9.9's Critic, 2026-10-07.)

## 7. Finally handlers (D21)

`Scope::finally(\Closure)` and `Coroutine::finally(\Closure)` store the closure; at the scope's
disposal or the coroutine's finish the handlers run through an iterator in a child scope of the
scope (or of the coroutine's scope), with the scope or the coroutine as the argument
(`coroutine.c:1225-1350`, `scope.c:1720-1753`). One handler error goes on as itself, two or more as a
`CompositeException` (`coroutine.c:1195-1215`; `coroutine/017`, `bailout/015` expect "Caught single
exception"), through the route of section 4. A handler added to a disposed scope or to a finished
coroutine runs at once (`scope.c:581-625`, `coroutine.c:1395-1411`; `coroutine/015`).

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

**As built (S9.6).** `src/iterator.c` ports `iterator.c:235-613` with the two contracts above: each
worker and the queued microtask hold a reference, and a finish handler releases the slot of a worker
that never ran. A walk that throws cancels the iterator's scope with "Cancellation of the iterator due
to an exception" and keeps the error in `exception`, and the last worker to leave ends with it, where
TrueAsync notifies a completion event (no waiter needs one in this layer; `iterate()` of the
iterators layer adds its wait): one that ran throws it, one that never ran routes it from its finish
handler (`internal/068`). A walk that one worker stops while another moves a suspending Traversable
stays stopped; TrueAsync's end of the move restarts it and the walk runs on (`internal/069`, probe
`s9.6/c2.php`). `TrueAsync\Test\iterate()` runs the core from a test (`internal/066` prints what
TrueAsync's `Async\iterate()` prints for the same walks, `internal/067`). The finally run
(`src/coroutine.c`, "Finally handlers") is an iterator with no concurrency limit and high priority in
a new child scope of the target's scope; `finally_handler_call` collects the handlers' errors in the
iterator's `exception`, a cancellation of a handler included, as TrueAsync's (`scope/117`), and an
exit stops the run. `scope_dispose` starts the child scopes' handlers, then its own, and the scope stays
until those runs end (`ASYNC_SCOPE_F_DISPOSING` keeps the walk from re-entering); `cancel()` of a scope
with nothing to cancel runs them too (`scope/114`).

The bailout trace: after a fatal error PHP marks every object destructed (`main/main.c:1486`), so the
scope object's `free_obj` runs in `zend_call_destructors` without `__destruct`, disposes the scope and
starts its finally run; the scheduler's last run after the destructors (`main.c:1938`) runs it, after
the shutdown functions. TrueAsync takes the same path (gdb on the reference); no change of the bailout
rule. A coroutine that took the bailout drops its own handlers unrun, as TrueAsync's (`bailout/016`),
and so does a scope disposed while the bailout unwinds the coroutines (a zombie's fatal error after
`disposeSafely()`): its run's worker is finalized unrun with the rest, and nothing runs after a fatal
error but the scheduler's run after the shutdown destructors (`bailout/017`); the reference prints
nothing there either (probe `s9.6/c3.php`). The plan's reading that TrueAsync's reference to a
coroutine with handlers keeps an unheld coroutine's error from ending the request does not hold:
probed, the reference ends the request as ours does (`s9.6/f6.php`); ours runs the handlers first
(`scope/111`, section 9, item 18).

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
   bookkeeping (`dev/plans/S3.md:1270`). A child scope keeps its index in its parent's vector the same
   way (S9.8, `scope/125`): freeing 100 000 child scopes newest first searched for 5.8 s on the debug
   build, and takes 0.03 s now.
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
   cascade, which TrueAsync runs after the handler (the Sage, 2026-10-07, Final). One route of ours
   does run in a notify (S9.6): an iterator worker cancelled before it ran and leaving last has no body
   to end with the iterator's error, so its finish handler makes the error its outcome and takes
   finalize's steps in its notify, where a handler that suspends throws as on the route of a finished
   coroutine (`internal/068`).
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
15. **The `disposeAfterTimeout()` timer cancels in its notify** (S9.5), where TrueAsync spawns a
    coroutine of the global scope that cancels (`scope.c:676-723`): the cancel only queues, so it needs
    no coroutine, and none can be cancelled unstarted by a cancel of the global scope, which would
    drop the timeout. The scope's free withdraws the timer, so the object's destruction disposing an
    empty scope ends the wait for it (`scope/099`); TrueAsync's timer holds a reference to the scope and
    the script still waits (probe `s9.5/d8.php`), and leaks 32 bytes there.
16. **Of several `disposeAfterTimeout()` calls the earliest deadline wins** (S9.5, `scope/100`): one
    timer per scope. TrueAsync arms one per call, and a later one cancels a cancelled scope again,
    which closes it while zombies run (section 4, the second `cancel()`; probe `d9.php`).
17. **`awaitAfterCancellation()` returns once no coroutine of the subtree is left**, as its comment
    and the thread pool's use of it say (`scope.c:374-377`, TrueAsync's CHANGELOG "drained"); the
    reference returns at the first member's end, its check reading "completely done" as true for any
    cancelled scope (probe `s9.5/d3.php`, `scope/095`). Its handler runs in the waiting coroutine
    after the wake, not in the finishing one inside the notify, which runs in scheduler context
    (D26): the handler may suspend (`scope/096`). The error reaches the waiter as its waker's error,
    which a cancel of the waiter takes as its previous (`scope/105`). An error that comes while the
    handler runs finds the wait unlinked and climbs on as if nobody waited (`scope/104`); the
    reference loses every error after the first (probes `d4.php`, `d5.php`), and a scope-held intake
    was rejected (the Sage, DECISIONS 2026-10-07). A cancelled scope is waited on once closed too, where
    the reference returns at once (`scope/107`); a closed scope that is not cancelled returns at once,
    as there, though zombies may run in its cancelled child scopes (`scope/109`, `scope/110`).
18. **A coroutine's finally handlers start after the error route and the unheld check** (S9.6,
    `scope/111`): an error that cancels the coroutine's scope or ends the request runs them first.
    TrueAsync starts them before the route, in a child of the scope the error then cancels, so the
    cascade cancels them unrun (probes `s9.6/f6.php`, `pa.php`).
19. **A finally run's error is its last worker's own error** (S9.6, `scope/115`, `scope/117`): the
    iterator ends that worker with it, and it goes up the route from the run's child scope.
    TrueAsync first offers it to the target scope's own handler with the finished coroutine and
    throws it in the worker only when that declines (`coroutine.c:1238-1265`), so with both handlers
    set its own handler takes a `Coroutine::finally()` error where ours the child scope handler does
    (probe `f12.php`).
20. **`exit()` in a finally handler ends the request** (S9.6, `scope/113`), as item 10 for a scope's
    handler; TrueAsync's run takes the exit as its worker's and the script goes on to its end, then
    exits with the status (probe `pc.php`).
21. **Handlers left when the scheduler is off are dropped unrun** (`async_finally_handlers_start`):
    nothing would run their coroutine. Handlers whose run the scheduler refuses for want of a stack
    stay with their scope: a closed scope's disposal starts them again, and a disposal's own refusal
    releases them unrun with the scope, after its walk (S9.8, `scope/123`); a coroutine's are released
    unrun at its finish, outside any walk.
22. **A walk another worker stopped during a move stays stopped** (S9.7, `internal/069`): the move's
    end restores STARTED only from MOVING. TrueAsync's `ITERATOR_SAFE_MOVING_END` restores it
    whatever the state, so a worker whose generator step suspended restarts a walk another worker's
    `return false` had stopped. `valid()` runs inside the same guard, which the reference calls
    outside it; a Traversable without `get_current_key` gives the position as the key, where the
    reference calls the NULL handler (`iterator.c:445`).
23. **`awaitCompletion()` waits again while the subtree still runs when the waiter runs** (S9.7,
    `scope.c` awaitCompletion's loop): woken by another enqueue than the scope's, or by the scope's
    with a member spawned before it ran (`scope/122`); TrueAsync returns at the first wake
    (`scope.c:301-372`).
24. **`awaitCompletion()` counts running coroutines of the subtree, not child scopes** (`scope/119`):
    a scope whose child scopes have no coroutine left returns at once, where TrueAsync waits while
    `scopes.length != 0` (`scope.c:344`).
25. **A provider returning a Scope whose scope was freed throws** "Scope object has been disposed"
    (`scope_provide`, `scope/119`); TrueAsync reads the NULL scope as no scope and spawns in the
    current one (`async_API.c:32-58`).
26. **A chain of nested scopes deep enough overflows the C stack** in the subtree walks, ours and the
    reference's alike (50 000 `Scope::inherit()` in a coroutine, probe `s9.7/deep2.php`); accepted in
    S9.8 (`dev/SECURITY.md`, 2026-10-08).
27. **A member's finish walks each parent's other child scopes only** (S9.8, `scope/124`): the walk up
    skips the child scope it found completed, where TrueAsync's `scope_check_completion_and_notify`
    walks each parent's whole subtree again (`scope.c:1575-1592`), O(N^2) a finish in a chain of N.
28. **The object reports the handlers to the GC only while no coroutine is in the scope or in its
    child scopes** (2026-10-08, `scope/070`, `scope/126`). Such a coroutine reaches them through the
    error route, which no object reports, so TrueAsync's `scope_object_gc` (`scope.c:1418-1472`),
    which reports them always, lets the GC call the destructor of an object a handler's closure
    holds, and the destructor cancels the running scope (seen on ours before the fix: the member got
    "Scope is being disposed due to object destruction"). A child scope without coroutines routes no
    error now, so a cycle through it is collected, as TrueAsync's. A child scope the user still holds
    may spawn later and route an error to the parent's handler: S9.12 stops the reports while a child
    scope has its object and is not cancelled, and gives the parent's object back to the root buffer
    when a child scope is freed (S9-context.md, section 5, "As built"; `scope/127`). Before, a GC run
    while the child was idle closed the parent and the later error reached its handler with a
    stand-in. When
    a coroutine leaves its scope and parents without coroutines, their objects with handlers go back to
    the GC's root buffer, which dropped them as live, after the disposal.
29. **A running finally handler outlives every scope cancel** (S9.23, Edmond 2026-10-09; section 13): its
    run scope carries `ASYNC_SCOPE_F_FINALLY_RUN`, which the cascades of `async_scope_cancel()` and the
    error route skip, and of the scope cancels only a dispose timer's fire reaches it. TrueAsync's cascade
    cancels the run of a scope that was not cancelled before it started (`scope.c:1021-1036`), and its
    timer fire on a cancelled scope only closes it.
30. **After a scope's deadline interrupts its members, no finally handler is called under it** (S9.24,
    Edmond 2026-10-09; section 14). TrueAsync calls every finally handler whatever the deadline
    (`coroutine.c:1281`, `scope.c:1203-1236`).
31. **A finally run's last worker releases the handlers in its body** (S9.25; section 15), so a destructor of
    what they hold may wait, except after an `exit()`. TrueAsync releases them at the microtask's last release, in scheduler context,
    where its probe ends the request.

The probes of S9.6 are in `/mnt/project-files/s9/probes/s9.6/`. The probes of S9.3 are `/mnt/project-files/s9/probes/s9.3/q1.php`-`q16.php`; on the reference and
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
- S9.5: `p3.php` (a zombie keeps the request running) as `scope/094`; `scope/095`-`110` for the
  departures of section 9, items 15-17, the collector's edges of the timer and of the waiter in
  `awaitAfterCancellation()`, `dispose()` of a running scope, the timer after a fork and on a closed
  scope, and errors that come while the handler runs or as the waiter is cancelled.
- S9.6: `internal/066`-`069` for the iterator core through `TrueAsync\Test\iterate()`;
  `scope/111`-`117`, `coroutine/040` and `bailout/016`, `017` for section 9, items 18-20, where the
  handlers' errors go, a handler that suspends, a handler added late, and the handlers after a bailout.
- S9.7: `collector/073`-`078`, `scope/118`-`122`, `internal/070`, `071` for the backlog, the Critic's
  findings and the Mull survivors (the layer review below).

**Core dependencies**: none. `is_safely` and `get_coroutine_count` are in the pinned core
(API version 2).

**Measurements** (stage review, `dev/BENCHMARKS.md`): spawn to finish with the scope against the
same code before S9.2, and against the reference, at 1, 1 000 and 100 000 coroutines; one
`awaitCompletion()` over N in 1 000 and 100 000 members.

**The layer review** (S9.7, 2026-10-07). Coverage on the debug lane: `src/scope.c` 775 of 819 lines,
`src/iterator.c` 212 of 248 (before S9.7's tests). Mull (`tools/mull.py --diff-ref d196cbd --source
src/scope.c --source src/iterator.c`, the 247 tests of `S9.txt` and `collector/`): 175 mutants, 26 not
killed, 15 killed for time, line numbers as in that run; after it, `collector/078` and `scope/120`
kill `scope.c:170`, `1303`, `1307`, and `scope/122` kills `1434` in the FIFO order (checked by
hand). The other 22, by what answers them:

- Seen only by ASAN, as Mull builds the debug module: `scope.c:45`, two mutants (the vector's
  growth), `57` (a read past the vector when the child is missing), `831` (a read past the
  arguments), `298`, `302` (a freed scope's later child or coroutine keeps its pointer).
- Unreachable: `scope.c:81` (every add takes a new coroutine, never a zombie); `147` (a scope with
  zombies is cancelled and returns at the flag check before the sum); `192` (a child scope that can
  be disposed is disposed at once, so none waits in the vector ahead of one that cannot);
  `iterator.c:352`, `353`, `360` (an INDIRECT slot: the callers hand in a copied array or the
  handlers' table).
- Equivalent: `scope.c:1190` (a timer re-armed at the same deadline); `1452` (an extra wake of
  `awaitAfterCancellation()` waits again in its loop); `1780` (`scope_free` detaches what the
  request's removal skips).
- Not covered by a test: `scope.c:328` (finally handlers no disposal started, freed at the request's
  end); `486` (a stand-in's `isCancelled()` after its scope is gone); `751` (a cancel refused for want
  of a stack); `577`, `588`, `598` (the oracle's hand-out with two found members in one routed scope:
  a mutant only narrows the excuse); `iterator.c:401` (the index is the key only for a Traversable
  without `get_current_key`, item 22).

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

## 13. A running finally handler outlives every scope cancel (S9.23)

Edmond agreed, 2026-10-09 06:50 («согласен именно так и нужно! отдельный флаг для особого Scope,
который человек поставить не может»): a finally handler that has started is stopped by no scope cancel,
first, repeated or a parent's; from outside only a deadline stops it, through a flag PHP cannot set.

**Before S9.23.** The handlers of `Coroutine::finally()` and `Scope::finally()` run in a worker of a new child
scope R of the target's scope S (`async_finally_handlers_start()`, `src/coroutine.c`; TrueAsync's
`coroutine.c:1281-1282`). Whether a cancel reaches R depends on S's state. Under a cancelled S, the
completed branch of `async_scope_cancel()` skips a child scope with coroutines (`scope/140`). Under an S
whose member returned by itself, the first `cancel()` or `dispose()` of S or of an ancestor takes the
cancelling branch, which cancels every child scope, R included, and the handler's wait throws
`AsyncCancellation` (probed on the debug build). A `Scope::finally()` run that `scope_dispose()` starts
after the object's release leaves S neither closed nor cancelled, so a parent's cancel reaches it the same
way. TrueAsync's cancelling branch cancels every child scope too (`scope.c:1021-1036`; read, not run). No
deadline stops a handler that hangs under a cancelled or closed S: the dispose timer's fire on a cancelled
scope only closes it (`scope_dispose_timer_fire()` into the completed branch), and `disposeAfterTimeout()`
returns at once on a closed scope.

**Design** (revised after two design Critics).

1. `ASYNC_SCOPE_F_FINALLY_RUN` (`src/scope.h`, `ASYNC_EVENT_F_TYPE_SHIFT + 5`), set only by
   `async_finally_handlers_start()` on the scope R it makes. It is cleared when the run's last worker
   leaves: the last branch of `iterator_release_coroutine()` (`src/iterator.c:146`) calls
   `async_scope_finally_run_end()`, while the leaving worker still keeps R alive. Not at the iterator's
   FINISHED state, which comes while a handler still waits, and not in its dtor, which can run after R is
   freed. A refused start frees R; RSHUTDOWN frees every scope; after a bailout the flag stays on R until
   the request ends. R has no object of its own and no PHP API sets or reads the flag; a stand-in object a
   SpawnStrategy's hook gets for R reaches `async_scope_cancel()` and `disposeAfterTimeout()`, which
   return at once on a flagged scope (`scope/154`, `155`).
2. The cascades skip a flagged child: the cancelling branch of `async_scope_cancel()` and the error
   route's loop in `async_scope_catch()` (`src/scope.c:893-899`) test the flag; the completed branch skips any
   child with coroutines, and a flagged R always holds its worker, while `async_scope_cancel()` returns at
   once on a flagged scope. R and its subtree are then out of reach of any cancel of S or of S's
   ancestors; the flag holds from the run's start, before its workers exist. An error of a coroutine a
   handler spawned routes from R and cancels R's coroutines, the worker included, as any scope's: a
   failure inside the cleanup, not a cancel from outside it.
3. The deadline has its own cancel. `scope_dispose_timer_fire()` first walks S's subtree; on every
   flagged scope it sets `ASYNC_SCOPE_F_CLOSED` (no new worker, `src/iterator.c:181`) and cancels every
   coroutine of that scope and of the scopes below it with `is_safely = false`, whatever their state, so
   a second fire reaches a handler that caught the first, and a zombie stops. It does not go through
   `async_scope_cancel()`, so no completed branch closes an idle scope there and starts its handlers.
   The walk rereads each vector's data and length at every step. Then the fire cancels S as today.
4. A close keeps the deadline while it has work: the completed branch, after the starts it makes,
   disarms an armed dispose timer only when no flagged scope is below S (a scan of the subtree), so a
   later `dispose()` or the object's release does not withdraw it (`scope/153`), and
   `disposeAfterTimeout()` is accepted on a closed scope with a flagged scope below it. A run that ends
   before the fire leaves the timer armed, since a run its worker's own handlers start after the first
   one ended still needs it (`scope/156`); the script waits for it, at most the timeout the caller
   gave, as it waits for an open scope's.
5. The collector counts a flagged scope's node as live reach while an ancestor's dispose timer is armed,
   and an armed timer of a closed scope no longer makes its own node live (`src/scope.c:958-963`; `scope/157`).
6. Unchanged, and recorded as what stops a handler besides the deadline: exit and graceful shutdown
   (`scheduler_cancel_all()`, then D16 after `ASYNC_EXIT_DEADLINE_MS`), the deadlock resolution
   (`src/scheduler.c:808`), the collector's cancel policy (`src/collector.c:1109`), and a `cancel()` of the
   worker's own Coroutine object (reachable through `Async\get_coroutines()` or a scope handler's
   `$coroutine` argument): the shield is against scope cancels.
7. Not in this step: the completed branch's cascade of a child made under S after S's cancel (PLAN open
   question). The flag removes what blocked it (`scope/140`), and the question stays Edmond's. A
   coroutine a handler spawned and left running under a cancelled S is such a child once the run ended:
   neither S's cancel nor its deadline reaches it (the code Critic).

**Open for Edmond.** (a) A run that starts after the deadline fired (a member the fire cancelled unwinds
and then starts its handlers): bounding it needs S to remember that its deadline passed (asked
2026-10-09; answered 07:57, section 14). (b) A handler that catches the deadline's cancellation and waits
again outlives one fire; D16's exit repeats an uncatchable one.

**Departure from TrueAsync** (section 9): TrueAsync cancels a running finally run from its scope's or an
ancestor's cancel, and its dispose timer does not reach a cancelled scope's children.

**Tests**, one case each: a member that returned by itself, its handler waiting, then `cancel()` of its scope:
the handler ends (`scope/142`); the parent's cancel (`143`); a sibling's unhandled error routed through S
(`144`); two handlers on one target, the fast one ended (`145`); a `Scope::finally()` run after the object's
release and the parent's cancel (`146`); `disposeAfterTimeout()` on a cancelled scope stops a hanging handler
(`147`), under `allowZombies()` too (`148`), after `cancel()` and `dispose()` closed the scope (`149`), and a
second timer after a handler caught the first fire (`150`); the fire still runs an idle child's
`Scope::finally()`, which guards the walk's order (`151`, passes before S9.23; reversed in S9.24, section 14);
a held inner scope left by an ended run is closed by S's cancel, which guards the flag's clear (`152`);
`dispose()` keeps the timer for the `Scope::finally()` run it starts (`153`); a stand-in's `cancel()` and
`disposeAfterTimeout()` leave the run alone (`154`, `155`); a closed scope's timer stops a run its worker's
own handlers start after the first run ended (`156`); the collector does not report a handler a deadline below
will stop (`157`). `scope/117` (ours) cancelled its waiting handler with `cancel()`; it moves to
`disposeAfterTimeout()`, which keeps its case (a cancellation among the handlers' errors).

## 14. After a scope's deadline interrupts its members, no finally handler is called under it (S9.24)

Edmond agreed, 2026-10-09 07:57 («да именно.. и видимо в этом случае флаг говорит никакие другие finallly
больше не стартуют!»), answering section 13's open question (a): once a scope's dispose timer has fired, no
finally handler starts under that scope or under any scope below it.

**Before S9.24.** The fire cancels the members of S (section 13, item 3). A member that unwinds afterwards
starts its `Coroutine::finally()` handlers in a new run under S when it finishes (`src/coroutine.c:510-519`);
so does S's own `Scope::finally()` when S completes and its disposal starts the handlers
(`scope_finally_start()`), and an idle child scope's handlers that the fire's own cancel starts when it
closes the child (the completed branch, `scope/151`). The timer is gone by then, so nothing bounds such a
run: a handler that waits hangs the script.

**Design** (revised after the design Critic, the code Critic, its re-check and two quality Critics).

1. `ASYNC_SCOPE_F_DEADLINE_PASSED` (`src/scope.h`, `ASYNC_EVENT_F_TYPE_SHIFT + 6`, the next free bit of the
   scope's range 13-28). It is never cleared; no PHP API sets or reads it; `disposeAfterTimeout()` still arms
   a second timer, whose fire cancels the runs that caught the first (`scope/150`). Two places set it:
   - `scope_dispose_timer_fire()` on S, before the walk over the runs below and the cancel of S, only when
     that cancel interrupts a member (item 2);
   - the fire's walk (section 13, item 3) on every finally run's scope it closes, whatever S is, so the
     stopped run calls none of its handlers not yet called and the runs its workers start call none
     (`scope/161`, `165`).
2. Only when the fire interrupts a member: `scope_deadline_interrupts_member()` follows the branches of
   `async_scope_cancel()` (a closed scope and a finally run's scope return at once, a completed one, a
   cancelled one counting, passes the cancel only to its child scopes without coroutines, any other cancels
   its coroutines, zombies included, and every child scope but a finally run's) and tells whether the cancel reaches a
   coroutine. A member made under a cancelled S after its cancel, in a grandchild scope the cascade
   reaches, is such a member (`scope/172`, the second re-check Critic). Without a member
   to interrupt, the deadline bounds nothing, so S's own `Scope::finally()` after its members returned
   (`scope/170`) or after the fire stopped the only run below it (`171`), a zombie `disposeSafely()` made
   (`166`) and a member of a cancelled child that went on after catching (`168`) keep their handlers (the
   re-check and quality Critics). Not on a safe scope (`ASYNC_SCOPE_F_DISPOSE_SAFELY`, every
   `Scope::inherit()` made from the global scope): its cancel turns started members into zombies that run
   on, so a zombie's handlers run when it finishes (`scope/164`); without this a member that finished its
   work would lose its cleanup (the design Critic; probed on the debug build). Edmond agreed, 2026-10-09
   10:04 («да»), after keeping the global scope safe, as TrueAsync's (10:03, «да остаётся мягким»), so a
   top-level `Scope::inherit()` is safe too. Never on a request-lifetime scope
   (`ASYNC_SCOPE_F_REQUEST_LIFETIME`): a SpawnStrategy's stand-in of the global scope can clear its safe bit
   with `asNotSafely()` and arm a timer, and the flag would then drop every finally handler for the rest
   of the request (`scope/167`, the code Critic). The flag is S's, for its whole subtree: once one member is
   interrupted, a survivor elsewhere below S (a member of a cancelled child that catches and goes on, a
   zombie of a child disposed safely) loses its handlers too (the re-check Critic; read from code, not
   run). A per-scope flag would keep them; it waits for Edmond's answer on the safe-scope default.
3. A run under a flagged scope calls no handler: `finally_handler_call()` returns at once when
   `async_scope_is_past_deadline()` (a walk up `parent_scope` from the run's scope) finds the flag. The check
   is at each call, not at the start: the run still starts, as before, and its worker releases the handlers
   as it does after calling them, so every caller (a coroutine's finish, a member cancelled before it ran, a
   scope's disposal, the completed branch) keeps its path. A refusal at the start would instead release the
   handlers inside a finished coroutine's finalize, and the scope's put-back would keep them past the
   scope's cancel (the code Critic).
4. The walk still goes before the cancel of S: on an S left unflagged (item 2), the runs that the cancel
   starts, for S itself and for its idle child scopes, call their handlers, and a walk after it would stop
   them (`scope/169`, `170`, the quality Critics).
5. Unchanged: `Coroutine::finally()` on a finished coroutine and `Scope::finally()` on a disposed scope call
   the handler at once in the caller's coroutine (`src/coroutine.c:930-938`, `src/scope.c:2018-2022`);
   that is a call, not a run under S. A run started before the fire is stopped by the fire's walk as in
   section 13. A coroutine spawned into a flagged S after the fire calls none of its handlers either,
   though nothing bounds its body: when the fire takes the cancelling branch, S is cancelled, not closed,
   and accepts spawns (read from `async_scope_spawn()`, not run); when it takes the completed branch, as in
   `scope/172`, S is closed and `spawn()` throws, but a `Scope::inherit()` made under it still accepts them
   (the re-check Critics). PLAN open question.
6. Found on the way, not S9.24's: a finally run releases its handlers in the scheduler's context, so a
   destructor of what a handler holds cannot wait ("The operation cannot be executed in the scheduler
   context"), with or without a deadline (probed on 31fec2b). The next step, S9.25.

**Departure from TrueAsync**: TrueAsync calls every finally handler whatever the deadline
(`coroutine.c:1281`, `scope.c:1203-1236`).

**Tests**, one case each, each failing before S9.24 unless marked: a member the deadline cancelled, its
`Coroutine::finally()` handler not called (`scope/158`); the scope's own `Scope::finally()` after its member
unwound from the deadline (`159`); a member of a child scope cancelled by its parent's deadline (`160`); a
finally worker's own `Coroutine::finally()` added in a run the deadline stopped (`161`), and the same in a
safe scope (`165`); a dropped handler's closure is released and its captured object's destructor runs before
the script goes on (`162`, passes before); a child scope's deadline leaves its parent's `Scope::finally()` to
run (`163`, passes before); a safe scope's zombie keeps its handler (`164`, passes before); a zombie that
`disposeSafely()` made keeps its handler after a later fire (`166`, passes before); a fire through an unsafe
stand-in of the global scope leaves a later coroutine's handler to run (`167`, passes before); a completed
parent's fire leaves the handler of a cancelled child's member that went on (`168`, passes before); a safe
scope's fire runs an idle child's `Scope::finally()` (`169`, passes before); an idle scope's fire runs its own
`Scope::finally()` (`170`, passes before), and so does the fire of a scope whose only running coroutine is a
finally worker (`171`, passes before); a member of a grandchild made under a cancelled scope, cancelled by
that scope's fire (`172`). Each guard but 162 and 163 fails without its part of the change (checked by undoing
it). `scope/151` (ours, S9.23) expected the fire to run an idle child's `Scope::finally()`; under Edmond's
rule the fire of an unsafe scope with a running member drops it, so 151 now expects no output from the handler
(`changed:`); the walk's order it guarded is guarded by 169. Its file name still says "runs": a list line is
frozen once pushed.

## 15. A finally run releases its handlers where a destructor may wait (S9.25)

**Before S9.25.** A finally run's iterator holds the handlers' array and, through `finally_run_dtor()`, the
coroutine or scope it passes them (`src/coroutine.c:610-619`). Both go at the microtask's last release in
`iterator_dtor()` (`src/iterator.c`). The worker's entry queues the microtask (`iterator_defer()` in
`iterator_run()`), so when a worker ran and no handler suspended, the last release happens in the tick
(`scheduler_tick()`, in scheduler context): a `__destruct()` of what a handler's closure captured that calls `delay()` throws "The operation
cannot be executed in the scheduler context", and the request ends. It fails so with and without a deadline,
for `Coroutine::finally()` and `Scope::finally()` alike (probed on 8b9ba78, debug build). TrueAsync releases at
the same point (`iterator_dtor()` from `execute_microtasks()`, scheduler context set, `scheduler.c:1525-1540`),
and its probe also ends the request, with "The scheduler cannot be started when is already enabled"
(true-async/php-src `true-async` 829cde6d with php-async 1fdacf8).

**Design** (revised after the design Critic, the code Critic and its re-check).

1. `iterator_dispose()` lets go of what the walk holds and what the caller's `extended_dtor` releases (a
   finally run's target): the hash iterator, the array, the Traversable's iterator and the callable, each
   detached before its release. `iterator_dtor()` calls it, then releases the exception, so the microtask's
   release finds nothing left after it.
2. The last worker to leave calls it in its body when it ran: `iterator_release_coroutine()` with `has_run`,
   after the state goes `FINISHED` and before `async_scope_finally_run_end()` and `iterator_end_worker()`.
   So:
   - a destructor there runs in that worker, in the run's scope, and may suspend it; a `spawn()` in it goes
     into the run's scope, as one from a handler, and throws after a fire closed the run;
   - no scope cancel interrupts the wait, as none interrupts a handler (section 13; `scope/177`), while a
     dispose timer's fire that lands during the wait does (`scope/178`). A destructor that starts after the
     fire (the fire stopped the handler, or the run started after it) is not bounded by it: only a second
     `disposeAfterTimeout()` stops it, as it stops a handler that catches the deadline and waits again (PLAN
     open question). Before S9.25 such a destructor failed at once;
   - after an exit (a handler called `exit()`) the worker does not release but queues the microtask once more,
     so the tick lets go, as before S9.25, and a destructor there cannot wait: a destructor's error in the
     worker would take the exit's place as its outcome (`zend_exception_set_previous()` drops an exit), and
     the request would go on. Without the queueing the worker's own release is the last one when a handler
     suspended (the tick dropped the microtask's reference meanwhile), and the exit was lost so before S9.25
     too (`scope/184`; `183` without the wait). Keeping the release in the worker would need the exit taken
     out around it, and a waiting destructor would delay the exit;
   - the walk's error is thrown after the release, by one rule in `iterator_end_worker()`: an exit stays, a
     cancellation (the worker's own, or one that ended a destructor's wait) goes under the walk's error, and
     a destructor's error goes over it, as PHP chains a destructor's error over a pending one
     (`zend_objects_destroy_object()`). So a fire that interrupts the wait loses no handler's error (`182`),
     and a destructor's error takes the route of a handler's (`180`, `181`).
3. Unchanged: a run whose worker a fire's walk cancelled before it ran, or whose worker was never spawned,
   leaves the release to the microtask's last one, in the worker's finish handler (inside its notify, in
   scheduler context) or in the tick, where nothing can suspend. Any coroutine cancelled before it ran is
   released the same way: it finishes with no stack (`coroutine_finish_unrun()`), and a destructor that
   waits in what a `spawn()` closure captured fails with "Cannot switch coroutines in the current execution
   context" (probes, debug build; the finally run's with the scheduler-context error); PLAN open question.

**Departure from TrueAsync:** the release point (item 2); TrueAsync's fails as ours did.

**Tests**, one case each, each but 183 failing before S9.25: a `Coroutine::finally()` handler's capture (`scope/173`)
and a `Scope::finally()` handler's (`174`) after the handler ran; the same two when the fire keeps the handler
from being called (`175`, `176`); `Scope::cancel()` does not interrupt the destructor's wait (`177`, which also
fails if the release is moved after `async_scope_finally_run_end()`) and a fire does (`178`); a coroutine's
result that the run's release of the coroutine frees (`179`); a destructor's error goes to the scope's
exception handler (`180`) with the handler's error as its previous (`181`); a fire that interrupts the wait
keeps the handler's error (`182`, which also fails if the walk's error is thrown before the release); a
handler's `exit()` still ends the request when a destructor of what it held throws (`183`, which passes
before S9.25 and fails without the exit check), also after the handler waited (`184`). Not
covered: a run of several workers (one release of one array, whichever worker leaves last) and the test hook's
Traversable and callable, whose release in the worker runs a generator's `finally` with a pending cancellation
stashed as for a destructor (`zend_generator_dtor_storage()`; read from the core, not run).
