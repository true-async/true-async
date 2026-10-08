# S9 notes, layer 2: Context

Design note of the second S9 layer, 2026-10-08. The layer adds `Async\Context` and
`Async\ContextException`, the context of a coroutine (`coroutine_context()`, `Coroutine::getContext()`)
and the context of a scope (`current_context()`, `root_context()`), whose `find()`, `get()` and
`has()` read the contexts of the parent scopes too. Names, signatures, messages and behaviour come
from TrueAsync (`ext/async` at `tests/lists/REFERENCE`, `1fdacf8`, file names alone below) and its
core (the fork `true-async/php-src` `863f6dd90cf`, below `F:`); a departure is listed in section 7
with its reason. The storage itself comes from the pinned core (`async-core-io-2026-10-07-6`
`0145ca90d78`, below `R:` for `Zend/zend_async_API.h` and `R.c:` for `Zend/zend_async_API.c`).
Behaviour marked "probed" was run on a debug build of that fork with that `ext/async` on 2026-10-08
(scripts `c1.php`-`c13.php` in `/mnt/project-files/s9/probes/s9.context/`); the same build passes the
17 tests of section 8.

Layer 1 is built (`dev/plans/S9-scope.md`, S9.1-S9.9); this layer stands on its scope tree and adds
one field to `async_scope_t`. It needs no core change: the pinned core already owns the context's
layout and operations (section 2), and the scope is the extension's own.

## 1. What the layer implements

| Kind | In this layer | Elsewhere |
|---|---|---|
| Classes | `Context` (final), `ContextException extends AsyncException` (`context.stub.php`) | |
| `Context` | `find`, `get`, `has`, `findLocal`, `getLocal`, `hasLocal`, `set(key, value, replace = false)`, `unset`; `new Context()` | |
| `Coroutine` | `getContext()` (`coroutine.stub.php:24-26`) | |
| Functions | `current_context()`, `coroutine_context()`, `root_context()`, `request_context()` (`async.stub.php:195-235`) | the request scope behind `request_context()`: with an exported C API (section 4) |
| Internal | the context of a scope, the walk up the scope tree, the context factory of the core's slot, the `get_gc` of `Context` | the internal (C) context: done in S3 (`src/coroutine.c:40`, `206`) |

## 2. The storage

**The core owns the layout.** `zend_coroutine_t` carries `zend_object *context`, lazy, beside the
embedded `internal_context` (`R:144-145`). The core defines `zend_async_context_t { HashTable
string_keys; HashTable object_keys; zend_object std; }` (`R:556-577`) and its operations
(`R.c:211-289`): `tables_init`, `tables_destroy`, `entry_find`, `entry_set`, `entry_unset`,
`entry_gc`. An object key's entry owns the key object as well as the value, so a key whose object
died cannot alias the next object with its handle (`R:565-568`). The coroutine-level view
(`zend_async_context_get/find/set/unset`, `R.c:291-379`) mints a context lazily through
`zend_async_new_context_fn`, which the provider fills (`R:579-582`). TrueAsync's
`async_context_t` (`context.h:24-31`: `values`, `keys`, the function pointers of `F:1893-1908`) has no
counterpart: the RFC core replaced it.

**Our class wraps the core's struct** with one field in front, as the core allows (`R:558-563`):

```c
typedef struct {
	async_scope_t *scope;          /* the scope whose context this is; NULL for a coroutine's context,
	                                  for `new Context()`, and once the scope is freed */
	zend_async_context_t context;  /* the core's tables; std last */
} async_context_t;
```

`scope` is a borrowed pointer, as TrueAsync's (`context.h:28`): the scope holds the context, and its
free clears the pointer (`scope.c:1257-1261`). MINIT sets `zend_async_new_context_fn` to the class's
factory once our scheduler registered (a disabled extension registers no class), and MSHUTDOWN
resets it when ours set it: the core's unregister leaves it (`R.c:657-694`). The bridge
`ext-scheduler-hook` sets it in MINIT whatever happens and resets it in MSHUTDOWN
(`async_scheduler_hook.c:1359`, `1373`). The two extensions register the same class name and cannot
load together; neither can their schedulers.

**Handlers.** `free_obj` destroys the tables (`zend_async_context_tables_destroy`). TrueAsync
destroys them in `dtor_obj` (`context.c:368-375`), so a destructor that runs later in the shutdown
and reads the context finds a destroyed table: probed (`c3.php`), the debug build stops with
`ht=... is already destroyed` in `async_context_find_local`. `get_gc` reports the entries through
`zend_async_context_entry_gc`; TrueAsync's `Context` has no `get_gc` (`context.c:390-395`), so a
context that holds itself is never collected: probed (`c1.php`), 1 000 such contexts free nothing in
`gc_collect_cycles()` and keep 1.9 MB until the request ends. `clone_obj` is NULL, the class is final,
`@strict-properties` and `@not-serializable`, and `new Context()` makes a context with no scope, as
TrueAsync's `create_object` (`context.c:362-366`; `coroutine/021` constructs one).

**Methods**, as TrueAsync (`context.c:230-360`):

- The key is parsed as any value and refused unless a string or an object, with TrueAsync's
  `TypeError` "must be of type string|object, int given" (`context.c:209-215`, `context/004`).
  `Z_PARAM_OBJ_OR_STR` would turn the int `123` into the string `"123"` in weak mode, which
  `context/004` refuses.
- `find()` and `findLocal()` return the value or null; `get()` and `getLocal()` throw
  `ContextException` "Context key \"%s\" not found", or "Context key of type %s not found" for an
  object key (`context.c:220-228`); `has()` and `hasLocal()` return whether the key is held, a
  stored null counting as held.
- `set($key, $value, $replace = false)` refuses a key this context already holds with
  `AsyncException` "Context key already exists and replace is false" (`context.c:336-339`); a key
  only a parent's context holds does not count. It returns the same object. Replacing a string key's
  value goes through the core's `entry_set`, whose `zend_hash_update` runs the old value's destructor
  while the slot still holds it (`R.c:240`), so a destructor that reads the key gets the value being
  destroyed; TrueAsync has the same (`context.c:77`). The object-key path copies first (`R.c:244-258`).
  A core fix, `dev/RFC-CHANGES.md` 16 (`async-core` `b7c70909437`, pinned in
  `async-core-io-2026-10-08` `662dfe91919` before S9.11): the new value is written first and the old
  one released last, on both paths (`context/017`). `unset()` removes the
  key from this context only and returns the same object (`context.c:346-360`; the bridge's returns a
  bool).

## 3. The context of a coroutine

`coroutine_context()` and `Coroutine::getContext()` return `zend_async_context_get()` of the current
or the given coroutine: one object per coroutine, made at the first call, its `scope` NULL, so its
`find()` reads this context only, as TrueAsync's (`context.c:42-51`; `coroutine_context()` at
`async.c:829-856`, `getContext()` at `coroutine.c:1361-1378`). A finished coroutine still gets one
(`context/003`). What S3 built stays: the coroutine releases its context in `dtor_obj`
(`src/coroutine.c:116-121`) and in `free_obj`, which runs the same release, and reports the context
object to the GC (`src/coroutine.c:268-269`). `coroutine_context()` refuses in scheduler context, as
every function of `src/true_async.c` (`THROW_IF_UNAVAILABLE`, `php_true_async.h:96-100`); TrueAsync's
`THROW_IF_SCHEDULER_CONTEXT` does the same (`async.c:834`). It and `current_coroutine()` also refuse,
with "The current coroutine is not defined", while the current coroutine's object is being freed: its
`free_obj` runs a WeakMap value's destructor while it is still current, and a context made then would
never be released (`context/020`; `dev/RFC-CHANGES.md` 17 for C callers); `getContext()` refuses the
same object, which a WeakReference still returns there, with "The coroutine is being freed". Uncaught,
the refusal ends the request as an exit exception (`context/024`). While `dtor_obj` releases the
coroutine's values, a destructor that asks gets a new, empty context, which `free_obj` releases
(`context/025`).

Main is a coroutine from the script's first opcode, so `coroutine_context()` at the top level is main's
context, distinct from `current_context()` (`common/current_context_at_root`). A shutdown function or
a destructor at shutdown runs in the new main our scheduler mints after main finishes
(`src/scheduler.c:1302-1304`), so its `coroutine_context()` is empty: probed (`c13.php`), TrueAsync's
is empty there too.

## 4. The context of a scope

`async_scope_t` gains `zend_object *context` (lazy, NULL until asked for; 8 bytes per scope). Two
functions make it on the first call, link it (`scope`) and return it (`async.c:799-827`, `876-903`):

- `current_context()`: the context of `async_scope_current()` (`src/scope.c:128-133`), the current
  coroutine's scope or the global scope. At the top level it is the global scope's, the object
  `root_context()` returns (`context/009`, `context/010`);
- `root_context()`: the context of the global scope (`ASYNC_G(global_scope)`), the main scope of
  TrueAsync (`async.c:876-903`; TrueAsync's CHANGELOG, "root_context() was not the main Scope's
  context").

Both refuse with `THROW_IF_UNAVAILABLE` (`php_true_async.h:96-100`), as `coroutine_context()`: in
scheduler context, and once async is off, where `ASYNC_G(global_scope)` may already be NULL
(`src/scope.c:1855`). TrueAsync launches the scheduler first, since its scopes exist only after the
launch (`async.c:806-809`, `context/009`); ours exist from the first opcode (layer 1 note, section 3).
`current_context()` also refuses, with "The current scope is not defined", while a finished coroutine
that left a scope other than the global one releases what it held: the global scope's context would hand
a `new Scope()`'s destructors the root values (`context/038`, the Critic; TrueAsync throws the same on a
NULL scope, `async.c:811-813`). One that left the global scope reads the root context.

`request_context()` returns null, the reference's answer whenever no embedder marked a request scope
(probed `c8.php`; nothing in `ext/async` marks one, `async.c:905-927`, `F:1491-1492`,
`scope.c:1328`). The request scope comes with an exported C API (PLAN, Fog), since only embedding C
code (TrueAsync Server) marks one; until then the layer adds no field for it.

**The walk.** `find()`, `get()` and `has()` read this context, then the context of each ancestor of
`scope`, skipping a scope with no context, until a scope with no parent (`context.c:26-71`): the
global scope, a `new Scope()` (probed `c6.php`: its coroutines do not see the root's values;
`context/010`) or the private root scope of the core's GC coroutines (layer 1 note, section 3;
probed `c5.php`, a destructor run by the GC finds nothing of the root, as on ours). The walk is a
loop over `parent_scope`, linear in the depth and with no recursion, so the 43 000-deep chains that
overflow the recursive walks of layer 1 (layer 1 note, section 9, item 26) do not overflow it.
Coroutines of `Scope::finally()` and of `await_*` run in child scopes and see their parent's values
(probed `c5.php`).

**Lifetime.** The scope holds one reference to its context. `scope_free` (`src/scope.c:315`) clears
`scope` and hands the reference to `released_values`, which its caller releases after the walk, as it
does the handlers' closures (`src/scope.c:302-310`): the release runs destructors, and one that
disposes the parent scope inside `scope_dispose` would leave it reading the freed parent
(`src/scope.c:442-447`). TrueAsync releases it in place (`scope.c:1257-1261`). A `Context` held
past its scope answers from its own table (probed `c6.php`: `find('root')` is null once the scope is
gone, `find('own')` still answers). A closed or cancelled scope still makes and returns its context: a
zombie or a finally handler of a disposed scope reads it (probed `c7.php`).

The global and the engine scope's contexts are released at RSHUTDOWN, after the shutdown functions and
destructors, whose `root_context()` still reads the root one (section 7, item 3). That release can run
PHP code: an object made after the destructor pass (an output handler runs after it, `main/main.c` of
the fork, destructors at 1930, `php_output_end_all` at 1955) has its destructor still to run, and a
destructor that throws there becomes a fatal error and a bailout, which skips the rest of the
teardown. Other releases of the teardown carry the same exposure today: user scopes disposed inside
`async_scope_request_shutdown`'s loop (`src/scope.c:1846`, their handlers and, with this layer, their
contexts) and the coroutine objects released at `src/scheduler.c:2359` (their results and arguments,
and their contexts). S9.13 collects the user values these releases drop into one array released as the
teardown's last step, with a test in which an output handler's object throws from its destructor at
RSHUTDOWN.

**The engine's coroutines.** The core's GC run and the shutdown pass's destructor iterators join the
private root scope (`engine_scope`, `src/scheduler.c:1368`; layer 1 note, section 3). A destructor
that runs there, in a GC run or in the shutdown pass after an earlier destructor suspended, gets the
engine scope's context from `current_context()`: one context shared by all of them until RSHUTDOWN,
whose walk does not reach the root. TrueAsync's GC scope behaves the same (probed `c5.php`); the
root context is reached through `root_context()`.

**A Fiber.** A Fiber's coroutine has no scope in ours (`src/scheduler.c:1458-1462`), so
`async_scope_current()` gives the global scope inside a Fiber, as `spawn()` there already does; the
comment there says "as in TrueAsync's fork", and the layer 1 note cites `zend_fibers.c:1305`, which
creates the coroutine. The fork puts it into the current scope at `new Fiber()`
(`zend_fiber_object_create`, `F:` `Zend/zend_fibers.c:1315-1318`, `scope.c:925-931`). Probed on the
reference: `current_context()` in a Fiber started in a scoped coroutine reads that scope's context
(`c4.php`, `c10.php`), `spawn()` in it joins that scope (`c9.php`), the scope's `cancel()`
force-closes the Fiber and its next `Fiber::suspend()` is a fatal error "Cannot suspend in a
force-closed fiber" (`c10.php`), and a Fiber suspended and never resumed keeps its scope unfinished:
`awaitCompletion(timeout(100))` ends with the timeout, and `dispose()` finishes the scope
(`c14.php`). On our core a cancelled Fiber may suspend again (D5, `scheduler/044`; `fiber/030`
excluded), and the cancellation reaches it once, so a Fiber that catches it or suspends in `finally`
would keep a cancelled scope's `awaitAfterCancellation()` and `dispose()` waiting; and destroying a
Scope object cancels the Fibers in it (`src/scope.c:993-998`), so a long-lived Fiber first made inside
a user scope (an event loop's) ends with that scope. Question 1, section 9.

## 5. Garbage collection

TrueAsync's coroutine and scope objects report their context's values, not the context object
(`coroutine.c:332-350`, `scope.c:1451-1470`), and `Context` reports nothing. Probed: a context that
holds itself (`c1.php`), a finished coroutine whose context holds the coroutine (`c12.php`) and a
scope whose context holds the scope's object (`c11.php`) are all left to the request's end;
`gc_collect_cycles()` returns 0 for each, and a control cycle of two plain objects in the same
script is collected (the probes' known answer).

Ours, so that each cycle collects:

- `Context` reports its entries (section 2);
- a coroutine reports its context object (built in S3, `src/coroutine.c:268-269`);
- a scope object reports its scope's context object only while no coroutine is in the scope or in a
  child scope, in any state (queued, suspended, zombie), and the scope is not of the
  request's lifetime (`ASYNC_SCOPE_F_REQUEST_LIFETIME`, the global and the engine scope, which
  `root_context()` and
  the engine's coroutines reach without a member). While members run, they reach the context through
  `current_context()`, an edge no object reports, so a reported context would let a cycle "object →
  context → value → object" look unreachable, and PHP's collector would call the object's destructor
  (marked at `Zend/zend_gc.c:2400-2410` of the pinned core, called at `2433-2466`), which cancels the
  running scope (`src/scope.c:993-998`). With no member left, the destructor detaches the object and
  disposes the scope: it frees it, or starts its finally handlers and frees it after them
  (`src/scope.c:435-436`). The collector frees nothing reached from an object whose destructor it
  called in that pass and runs again (`zend_gc.c:2385-2431`), when the detached object reports
  nothing.
- when a coroutine leaves its scope and parents without coroutines, their objects go back to
  PHP's root buffer, so a cycle formed while coroutines ran is collected by the next run, which found
  the object live before (the Sage, 2026-10-08): `scope_objects_give_back_to_gc()` keeps a reference in
  `released_values`, released after the disposal, since a collection the release starts may free
  the scopes. S9.12 adds "has a context" to `scope_has_handlers()`, the test both `get_gc` and the
  give-back use, and names it after what the object reports (`scope_has_user_values()`).

The rule and the return to the root buffer already hold for the handlers: the handlers' fix of layer 1
(the Critic, 2026-10-08; S9-scope.md 9 item 28), committed apart from this layer. S9.12 adds the
context to what the object reports under the same rule.

**As built in S9.12** (the Critic, 2026-10-08): the rule is "only the object reaches the scope"
(`scope_is_reached_only_by_object()`): no coroutine in the scope, and every child scope can be disposed,
that is, has no coroutine and no object or is cancelled, down the subtree. An idle child scope the
script holds reaches the parent's context through its context's `find()` and the parent's handlers
through its spawns, so while it has its object the parent's object reports nothing; before, a GC run
then closed the parent (its finally handlers ran) and a later `get('server')->spawn()` threw "Scope
object has been disposed" (`context/037`; for the handlers `scope/127`, which item 28 of the layer 1
note had accepted). When a child scope is freed and its parent stays, the parent's object goes back to
the root buffer, so the cycle collects once the last child scope goes. Not collected until the request's
end: a parent whose own context or handler holds its child scope's object, as TrueAsync collects no
cycle through a context. The test is recursive and linear in the subtree, as layer 1's
`scope_has_coroutines()` was, and every scope object with a context pays it in a GC run now, not only
one with handlers (layer 1 note, section 9, items 26, 27).

Accepted: PHP calls the destructors of one garbage cycle in no defined order and checks only that each
has not run yet (`zend_gc.c:1922-1939`), so in a cycle "scope object → context → value → scope
object" a value whose destructor spawns into the scope runs before the scope object's destructor,
which then cancels that new coroutine ("Scope is being disposed due to object destruction").

The async object collector (S7) needs no change: it reads `Context` through its `get_gc`
(`src/collector.c:617-619`) and a coroutine through its own, and it does not walk a scope object
(`src/collector.c:610-615`), so what a scope's context holds counts as held from outside.

## 6. Steps

- S9.10 This note.
- S9.11 The list block for layer 2 in `tests/lists/S9.txt` (section 8), with `--XFAIL--` naming
  S9.11 or S9.12; `Context` and `ContextException`, the factory slot, `coroutine_context()`,
  `Coroutine::getContext()` (sections 2, 3). Its `set()` waits for the core fix of
  `dev/RFC-CHANGES.md` 16 in the pinned core (section 8).
- S9.12 The context of a scope, `current_context()`, `root_context()`, `request_context()`, the walk,
  the context in the scope object's `get_gc` under the handlers' rule (sections 4, 5).
- S9.13 The teardown's user values released as its last step (section 4): `async_scope_remove_coroutine`
  takes the array its shutdown loop passes, for the handlers, contexts and scope objects it drops.
- S9.14 Layer review: the Critic over S9.11-S9.13, coverage of `src/context.c` and the new lines of
  `src/scope.c`, Mull on the layer's diff, the fuzz oracle over 100 seeds of the layer's tests, the
  measurements of section 8.
- S9.15 Security pass by `dev/SECURITY.md`.

Question 1 answered "as TrueAsync" adds a step before S9.12: a Fiber's coroutine joins the current
scope at `new Fiber()`, with `c9.php`, `c10.php`, `c14.php` as own tests, D5's interplay settled, and
layer 1's note corrected.

## 7. Departures from TrueAsync

1. **The storage is the core's** (section 2): the class wraps `zend_async_context_t`; TrueAsync's
   `async_context_t` with function pointers has no place in the RFC core.
2. **`Context` frees its tables in `free_obj` and reports its entries to the GC** (section 2): a
   destructor at shutdown reads a context safely (`c3.php` stops the reference's debug build), and
   cycles through a context collect (`c1.php`, `c11.php`, `c12.php` collect nothing on the
   reference).
3. **The root context lasts until the request's end**: the global scope is freed at RSHUTDOWN, so
   `root_context()` in a shutdown function or a destructor at shutdown still reads the values the
   script set, and so does `current_context()` in a shutdown function and in a destructor that runs on
   main (one run by the engine's coroutines gets the engine scope's context, section 4). Probed
   (`c13.php`), the reference answers null for all of them there.
4. **A scope object reports its context only while nothing but the object reaches the scope, and
   returns to the root buffer when its last member leaves** (section 5); TrueAsync's reports the
   context's values whatever runs (`scope.c:1451-1470`) and collects none of these cycles (`c11.php`).
5. **The context is released after the scope tree's walk** (section 4), as our handlers are; TrueAsync
   releases it inside `scope_dispose`.
6. **`current_context()` in a Fiber is the root context** while a Fiber's coroutine has no scope
   (section 4, question 1); layer 1's note records the placement as a departure.
7. **`request_context()` is null without a field behind it** (section 4): TrueAsync's request scope is
   a scope field nothing in `ext/async` sets.

8. **An object key's entry releases its key before its value**, the core's order
   (`async_context_entry_dtor`), on `unset()` and when the Context is freed, where a string key's
   values go first; TrueAsync releases the values, then the keys (`context.c:106-110`, `373-374`).
9. **`current_context()` in a Future's `map()`, `catch()` and `finally()` callbacks is the root
   context**: S5's chain drain runs them in a coroutine of the global scope, which serves the chains of
   every scope (layer 1 note, section 3); TrueAsync runs the mapper in the scope captured at `map()`
   (`future.c:1593-1600`, `1751`), so a callback there reads its subscriber's scope values. Found by the
   Critic in S9.12 (`context/039`); a change to S5's drain, open for Edmond (PLAN, Open questions).

Kept as TrueAsync and noted: `new Context()` is public and makes a context the walk never leaves;
a held context of a freed scope stops answering for the parents (`c6.php`); `set()` and `unset()`
return the context, not a status.

## 8. Tests and measurements

**List** `tests/lists/S9.txt`, a block for layer 2, frozen in S9.11; 17 tests, each passing on the
reference build above (2026-10-08, 17 of 17):

- `context/001`-`013`, a group no list has ported yet (`context/README.md` is not a test);
- `coroutine/012`, `coroutine/021` and `common/current_context_at_root` from `tests/lists/S3.excluded`
  (`component:S9 (Context)`), which leave that file in the same commit;
- `scope/052` from `tests/lists/S9.excluded`, which leaves that file.

`context/001`, `003`-`005`, `coroutine/012`, `021` need only S9.11; the rest name S9.12.
TrueAsync's `fuzzy-tests/context/context.feature` is not ported, as no fuzzy test is.

**Own tests**, written in the step that needs them:

- S9.11: `c1.php` (a context that holds itself collects), `c3.php` (a destructor at shutdown reads a
  context whose destructor phase passed), `c12.php` (a coroutine whose context holds it collects), a
  replaced string key whose old value's destructor reads the key, sets eight other keys (the table
  grows) and unsets the key, on ASAN; `clone` and `serialize()` refused; `set()` replacing an object
  key's value keeps one entry;
- S9.12: `c6.php` (a context held past its scope), `c11.php` (a scope whose context holds its object
  collects), `c13.php` (the root context in a shutdown function and a destructor), `c7.php` (the scope
  object of a scope with a running zombie is not collected and the zombie reads it: "collected: 0",
  "zombie reads: Async\Scope", as the reference), a running scope whose context
  holds its object survives `gc_collect_cycles()`, the same cycle collected by a second
  `gc_collect_cycles()` after the last member ends, a scope with pending finally handlers whose
  object the GC destroys, a context released by `scope_free` whose value's destructor disposes the
  parent scope, `find()` from the bottom of 10 000 nested scopes (50 000 overflow layer 1's recursive
  walks, layer 1 note, section 9, item 26), `current_context()` and `root_context()` refused in
  scheduler context (a finish handler's notify), `request_context()` null (`c8.php`), `current_context()`
  in a Fiber (`c4.php`, by the answer to question 1);
- S9.13: an output handler's object whose destructor throws at RSHUTDOWN, the teardown completing.

**Core dependencies**: the context API is in the pinned core (`R:556-625`, API version 3).
`dev/RFC-CHANGES.md` 16: the string-key replace of `zend_async_context_entry_set` corrupts the heap
when the old value's destructor writes to the same context; the fix is one commit on `async-core`
(the scheduler RFC is ours), which a core update brings in before S9.11's `set()` lands. Pushing
`async-core` updates php/php-src#22561 and waits for Edmond's word. Done: `b7c70909437`, pinned
`662dfe91919`.

**Measurements** (S9.14, `dev/BENCHMARKS.md`): `find()` of a missing key at depth 1, 10 and 1 000,
and `coroutine_context()->set()`/`get()` per coroutine at 1 000 coroutines, against the reference.

## 9. Questions for Edmond

1. **Which scope a Fiber's coroutine joins** (section 4). Options:
   1. as TrueAsync: the current scope at `new Fiber()`. `spawn()` and `current_context()` in a Fiber
      act on that scope; its `cancel()` cancels the Fiber, and on our core (D5) a Fiber that catches
      the cancellation or suspends in `finally` waits again, holding `dispose()` and
      `awaitAfterCancellation()`; a suspended or unstarted Fiber holds `awaitCompletion()`; a Scope
      object's destruction cancels the Fibers in it, an event loop's included. D5's interplay needs
      deciding in the same step;
   2. as now: no scope. `spawn()` in a Fiber goes to the global scope and `current_context()` there is
      the root context; layer 1's note records the placement as a departure from the fork.

   Recommended: 2. A Fiber runs code that does not know about scopes (an event loop, a library's
   generator-like flow); on our core option 1 lets such a Fiber hold a cancelled scope's disposal for
   good, and the layer 1 tests pass either way.

   Answered by Edmond on 2026-10-08: option 2. Section 6's extra step is not taken
   (`dev/DECISIONS.md`, 2026-10-08).

Taken without a question: `request_context()` is ported and returns null until an exported C API lets
an embedder mark a request scope (section 4; the Sage, 2026-10-08).
