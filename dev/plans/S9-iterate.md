# S9 notes, layer 6: iterate()

Design note of the sixth S9 layer, 2026-10-13. The layer adds `Async\iterate()`: a call that runs a callable for each
element of an array or a `Traversable` in coroutines of a new child scope, at most `$concurrency` at a time, and
returns when the walk ends, with what the coroutines the callable spawned do decided by `$cancelPending`. Names,
signature, messages and behaviour come from TrueAsync (`ext/async` at `tests/lists/REFERENCE`, `1fdacf8`, file names
alone below; its core, the fork `true-async/php-src` `863f6dd90cf`) and its documentation (`true-async.github.io`
`eadb6cc`, `en/docs/reference/iterate.md`, below `site:`). `true-async-doc` has no page for it. Behaviour marked
"probed" was run on a debug build of that fork with that `ext/async` on 2026-10-13 (scripts `i1_*.php`-`i18_*.php` in
`/mnt/project-files/s9/probes/s9.iterate/`); the same build passes the 20 tests of `tests/iterate`. This extension
has no `iterate()` yet, so nothing here was run on ours.

**Why this layer is the sixth.** `dev/PLAN.md` orders the S9 layers scope, context, channels, task groups, pools,
iterators, FileSystemWatcher, runtime information. TrueAsync's only public iterator is `iterate()`
(`async.stub.php:244-253`); FileSystemWatcher has its own iterator (`fs_watcher.c:596-721`) and is the next layer.

**Most of it is built.** S9.6 ported TrueAsync's iterator core (`iterator.c:235-613`) as `src/iterator.c` for the
finally handlers (`dev/plans/S9-scope.md` section 7): the workers, the shared position, the microtask that adds a
worker while a handler waits, the safe move of a `Traversable` that suspends, the exception that cancels the
iterator's scope. `TrueAsync\Test\iterate()` already walks with it from tests (`internal/066`, `067`), without a wait.
The layer adds the function, the caller's wait and the end of the walk; no core change (`async_API` of TrueAsync's
core has `ZEND_ASYNC_NEW_ITERATOR_SCOPE` only for `ext/async` itself).

## 1. What the layer implements

| Kind | In this layer | Elsewhere |
|---|---|---|
| Function | `iterate(iterable $iterable, callable $callback, int $concurrency = 0, bool $cancelPending = true): void` in `true_async.stub.php`, as `async.stub.php:253` | |
| Iterator core | a hook for the walk's end, so a waiter takes the walk's exception instead of the last worker (section 3) | the core itself: S9.6 |
| Waits | the ITERATE wait kind: the caller until the walk ends; then the SCOPE kind until the iterator's scope completes (section 3) | |
| Collector | ITERATE's `collector_target` (section 4) | |

What a user gets, from `site:iterate.md`:

```php
spawn(function () {
    iterate(range(1, 100), function (int $userId) {
        $data = file_get_contents("https://api.example.com/users/$userId");
        if ($data === false) {
            return false;                       // stops the walk; running callbacks finish
        }
    }, concurrency: 10);                        // at most 10 callbacks at a time

    echo "All users processed\n";               // after the walk and after the coroutines the callbacks spawned
});
```

## 2. The behaviour, as TrueAsync's

From `async.c:962-1161` and the probes; every row holds for this layer unless section 6 says otherwise.

| Case | What happens |
|---|---|
| Arguments | `$iterable` neither array nor `Traversable`: `TypeError` (`type_error_invalid_argument`); `getIterator()` throws: its exception, no callback runs (`014`) |
| Where | from a coroutine, or from the main script, which starts the scheduler (`010`); from the scheduler context: `Error`. In a destructor and in a finally handler it works (probed `i14`) |
| Scope | the callbacks run in a new child scope of the caller's scope; a cancel of the caller's scope reaches them (probed `i16`) |
| Order | with no limit, the first worker calls the first callback; while it waits, the microtask adds a worker for the next element (`i15`: `begin 1, end 1, begin 2, begin 3, end 2, end 3` when each callback suspends once) |
| Array | copied at the call: a change of the caller's array is not seen (`i13`); an `ArrayObject` that grows while walked is (`i13`) |
| Keys | any type a `Traversable` yields, passed as is (`i12`) |
| By reference | a callable taking `&$value` gets PHP's warning "must be passed by reference, value given", as `array_map()` gives (`i6`) |
| `false` | a callback returning `false` stops the walk; callbacks already running finish, they are not cancelled, whatever `$cancelPending` (`003`, probed `i3`) |
| Exception in a callback | the walk stops, the iterator's scope is cancelled ("Cancellation of the iterator due to an exception"): the other callbacks and every coroutine they spawned, whatever `$cancelPending` (`006`, `011`, probed `i4`); `iterate()` throws the first exception (`i9`) |
| Exception of the `Traversable` | thrown by `iterate()` (`i7`) |
| `$cancelPending: true` | once the walk ended, the coroutines the callbacks spawned that still run are cancelled with "Cancellation of pending coroutines after iterator completion", and `iterate()` waits until they end (`012`; probed `i10`: a child that catches the cancellation and waits 20 ms more is waited for) |
| `$cancelPending: false` | `iterate()` waits until they end (`013`) |
| A spawned coroutine's error | after the walk, it reaches the caller as `iterate()`'s exception (probed `i8`, `$cancelPending: false`); during the walk it goes up the caller's scope route instead: the caller, its siblings and the request end with "Fatal error: Uncaught" (probed `i22`); section 6, item 6 |
| Caller's scope cancelled before the call | the callbacks run (probed `i21`), as a spawn in a cancelled scope does (`dev/PLAN.md` open questions, "A child scope made under a cancelled scope") |
| Nested | `iterate()` in a callback of `iterate()` works (probed `i19`) |
| Slow last callback | waited for, never cancelled as "pending" (`017`) |
| `$concurrency` | `0`: no limit; `n`: at most `n` callbacks at once, for arrays and `Traversable`s alike (`008`, `016`, `018`, `019`) |
| Return | nothing (`i12`) |

## 3. The design

**The scope.** A new child scope of the caller's, with DISPOSE_SAFELY cleared at once, as `async.c:1016` does:
`async_scope_new()` copies the flag from the parent (`src/scope.c:1238`) and the global scope has it
(`src/scope.c:2224`), so without the clear every cancel of the walk (`iterator_take_exception()`, `$cancelPending`)
would leave started coroutines running as zombies (`src/scheduler.c:1694-1697`) and the scope wait, which skips
zombies, would return under them.

**The wait.** `iterate()` builds the iterator with `async_iterator_new()` in a larger struct (`iterator_size`) that
holds the waiter, its record, the errors it collected and a reference of its own to the iterator's microtask (as
`async.c:1066`), so the last worker's release cannot free the struct before the caller reads it. It starts the first
worker (a refused start throws at once, nothing parks) and parks the caller on one ITERATE record, linked on the
iterator scope's event for both waits, the walk and the scope's completion. TrueAsync parks on a completion event that
the last worker notifies (`iterator.c:58-110`, `async.c:1068-1075`); here the last worker's
`iterator_release_coroutine()` calls a new hook of the iterator, set by `iterate()`, which stores the walk's exception
in the struct, even when no caller waits any more, and wakes the waiter if there is one. Without the hook (the finally
runs, the test hook) the last worker ends with it as today (`dev/plans/S9-scope.md` section 7, "As built").

**The record's wake** is its own, not `awaitCompletion()`'s (`src/scope.c:1674-1686`, which wakes on the first error),
because the scope's event brings three things that look alike: a member's end, a cancel of the scope (the notify of
`src/scope.c:738`, its cancellation as the exception) and an error of the scope's route (`src/scope.c:979`). The wake
ignores a member's end until the walk has ended and the scope has no coroutine left; ignores a cancel's notify; and
stores a route error as handled and keeps waiting, so a spawned coroutine's error during the walk stops the walk
(section 6, item 6) by the walk's own path: the route's error cancels the scope as a callback's exception does. The
caller wakes only by the hook (the walk's end), by the scope's completion, or by its own waker (a cancel). The kind has
`unlink` and `abort`, which clear the hook's waiter; the hook still stores the exception, so after a bailout or a
caller that left nothing is lost and nothing is woken.

**After the walk.** As `async.c:1083-1122`: if the iterator's scope still has coroutines, `$cancelPending` cancels it
with TrueAsync's message, and the caller waits on the same record until the scope completes; a cancelled scope is
expected here (no check as `src/scope.c:1841`). Then the scope is disposed, and the walk's exception, the collected
route errors and the caller's cancellation are merged by `pool_exception_settle()`'s rule (S9.36): an exit stays the
outcome, a cancellation goes on top, otherwise the walk's exception on top and the others as its previous, as
`async.c:1137-1153`. A route error is a callback's exception from the walk's side, so with no caller's cancel the walk's
exception is the first of them (`i9`).

**The caller's cancellation** during either wait (question 1, answered B): the iterator's scope is cancelled with the
caller's cancellation (not safely, so no started coroutine is left as a zombie), the caller waits on the same record
until the scope completes and throws its cancellation, the walk's exception and the route errors as its previous. A
second cancel during that wait ends it at once: the record is unlinked, the scope is not disposed, what still runs
stays in it under the caller's scope and is freed with its last coroutine; an error it ends with later goes up the
route of the caller's scope, the caller having left.

**A cancel from above.** A cancel of the caller's scope reaches the iterator's scope as a child's (`i16`); the record
ignores its notify, and the caller wakes by its own waker when the cancel reaches it, which is the B path above. When
the cancel is safe (the parent's DISPOSE_SAFELY, `src/scope.c:1472`, `727`), started coroutines become zombies,
the caller among them, as for every child scope; S9.40 runs `i16` with a safe and an unsafe parent and records what
each gives.

**Arguments.** `Z_PARAM_ITERABLE`, as the test hook (`src/test_hooks.c:1035`); `$concurrency` out of `0`-`UINT_MAX`
is a `ValueError` (section 6, item 1); the array is duplicated, as the test hook does, so the walk's hash iterator
never sees the caller's array.

## 4. Garbage collection and the collector

The iterator holds the array or the `Traversable` and the callable; no PHP object holds the iterator, so PHP's GC
never walks it, as in S9.6 and in TrueAsync. A cycle through the callable's closure (a callback that captures an
object holding the walked array) is freed when the walk ends. The ITERATE record's `collector_target` reports the
iterator's scope, whose workers end the wait: a caller whose walk waits on something nobody reaches is found through
the workers' own records, as for `awaitCompletion()`.

## 5. Steps

- S9.40 The layer's block of `tests/lists/S9.txt` (section 8); `iterate()` in `true_async.c` and the stub, the
  iterator's end hook, the ITERATE kind, the wait after the walk and the caller's cancellation by question 1; own tests
  for each row of sections 2 and 6 the reference tests leave out.
- S9.41 Layer review: the Critic over S9.40, coverage of the layer's lines, Mull on its diff, 100 fuzz seeds over the
  layer's tests, the measurement of section 8; a `true-async-doc` page for `iterate()` with the errors of section 6
  corrected.
- S9.42 Security pass by `dev/SECURITY.md`: a callable or a `Traversable` that frees, closes or re-enters what the walk
  holds (the iterator's scope disposed from a callback, `iterate()` over the same generator from inside its own
  callback), a GC run that a release in the walk starts, the caller freed while it waits.

## 6. Departures from TrueAsync

Decided here by the project's rules, each to go into `DECISIONS.md` with S9.40.

1. **`$concurrency` below 0 or above `UINT_MAX` is a `ValueError`**, "must be between 0 and 4294967295", as the test
   hook does. TrueAsync casts it to `unsigned int`, so `-1` means no limit and passes silently (probed `i5`): a
   programmer's error caught, as `Pool`'s `ValueError`s are.
2. **`exit()` in a callback ends the request.** In TrueAsync the walk stops and `iterate()` returns as if nothing
   happened: the caller goes on, prints `after iterate`, and the script ends normally (probed `i11`). Here `exit()` is
   the graceful cancellation it is everywhere (`DECISIONS.md`): the caller's `finally` runs, its next line does not.
3. **No leak where TrueAsync leaks**: a `Traversable` that throws leaks 32 bytes of `iterator.c:328` (probed `i7`); an
   own test pins it on ASAN. The leak of `i17` (a caller cancelled while it waits for the spawned coroutines) goes with
   question 1's answer: the scope stays under the caller's and is freed when its coroutines end.
4. **The walk's end is a hook, not an event** (section 3): our iterator has no completion event since S9.6, and a
   waiter is the only user.
5. **A walk stopped during a move stays stopped** (S9.6, `dev/plans/S9-scope.md` section 9, item 22, `internal/069`):
   unchanged; `iterate()` inherits it.
6. **A spawned coroutine's error during the walk is `iterate()`'s exception**, as after the walk: it stops the walk
   like a callback's exception. In TrueAsync the outcome depends on when the child fails: after the walk `iterate()`
   throws it (`i8`); during the walk it cancels the caller and its siblings and ends the request with "Uncaught"
   (`i22`), because nothing waits on the scope's event then (`scope.c:977-985` here).

Item 1 follows Edmond's rule of 2026-10-12 (pools): a programmer's error is caught even where TrueAsync does not, when
the check is small.

TrueAsync's documentation, to correct in the doc page of S9.41: `site:iterate.md` calls `$concurrency = 0` "the
default limit" (it is no limit, `iterator.c:142`), and its note says running callbacks are cancelled after a `false`
when `cancelPending = true` (they finish, `i3`; only spawned coroutines are cancelled).

## 7. Questions for Edmond

Asked one at a time, each with the PHP code and what the programmer sees; answered.

1. **What a cancel of the caller does to the walk.** TrueAsync has no rule here; what happens is a side effect of its
   code, in which the first cancel ends the wait for the walk and the code after the walk runs early
   (`async.c:1083-1110`). With `cancelPending: false` the walk goes on to its last element and only then does
   `iterate()` throw (probed `i1`; `i18`: `await($caller, timeout(40))` returns at 40 ms and the walk runs on). With
   `cancelPending: true` the callbacks are cancelled at once, with the message "after iterator completion" (`i2`). A
   cancel while `iterate()` waits for the spawned coroutines leaves at once, they run on, and their scope leaks
   (`i17`, `i20`). Options: (A) as TrueAsync; (B) a cancel of the caller cancels the iterator's scope (callbacks and
   spawned coroutines, whatever `$cancelPending`), waits until they end and throws the cancellation, as an exception
   in a callback already does (`i4`); a second cancel during that wait leaves at once, and what still runs stays in
   the iterator's scope under the caller's scope, which frees it when it ends. Answered: B, TrueAsync's behaviour is a bug, a
   cancel stops the walk too (Edmond, 2026-10-13 03:38).

## 8. Tests and measurements

**List** `tests/lists/S9.txt`, a block for layer 6, frozen in S9.40: the 20 reference tests of `tests/iterate`
(`001`-`019`, `type_error_invalid_argument`), each passing on the reference build above (2026-10-13). `016`-`019` wait
with `delay(20)` in callbacks and compare peaks: they count workers parked on a timer at once, not elapsed time, so
they stay as they are; S9.40 lists any change before it freezes the block.

**Measurement**: B23, `iterate()` over 10 000 elements with no limit and with `concurrency: 10`, callbacks that
suspend once, instructions and allocations against the reference, as B21 and B22 (`dev/BENCHMARKS.md`).
