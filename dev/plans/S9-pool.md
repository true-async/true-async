# S9 notes, layer 5: Pool and the circuit breaker

Design note of the fifth S9 layer, 2026-10-12. The layer adds `Async\Pool`: a set of reusable resources (connections,
clients, any PHP value) that coroutines take with `acquire()` and give back with `release()`, with at most `max`
resources alive, `min` of them made in advance, hooks that check a resource on its way out and back, a background
healthcheck, and a circuit breaker that refuses every `acquire()` while the service behind the pool is down. Names,
signatures, messages and behaviour come from TrueAsync (`ext/async` at `tests/lists/REFERENCE`, `1fdacf8`, file names
alone below; its core, the fork `true-async/php-src` `863f6dd90cf`, below `F:`) and its documentation
(`true-async.github.io` `eadb6cc`, `en/docs/components/pool.md`, below `site:`). `true-async-doc` has full pages
(`ru/concepts/pool.md`, `ru/architecture/pool.md`, `e45c5bf`), the Russian originals of the site's, with the site's
errors (section 9). Behaviour marked "probed" was run on a debug build of that fork with that `ext/async` on
2026-10-12 (scripts `p1_release_unacquired.php`-`p12_unserialize_clone.php` in
`/mnt/project-files/s9/probes/s9.pool/`); the same build passes the 70 tests of `tests/pool`. A Critic and the Sage
checked the note on 2026-10-12; Edmond answered its three questions the same evening (section 11), and section 8
lists the departures with their reasons.

**Why this layer is the fifth.** `dev/PLAN.md` orders the layers scope, context, channels, task groups, pools,
iterators. The pool has no user inside the first version: its only C consumer in TrueAsync, the PDO pool, is out of it
(`DECISIONS.md` 2026-10-08, S10 outcomes), and comes later as `ext/pdo` hooks with the pool logic in this extension
over this layer (`dev/plans/S10.md` section 5, option B). The layer is the PHP class a programmer uses directly.

**No core change.** The pinned core (`async-core-io-2026-10-12-3`, `634cbbf94be7`) has no pool type. TrueAsync's core
carries one (`F:Zend/zend_async_API.h:2122-2168`: the struct, the circuit state, five handler unions, the strategy, six
API slots, `ZEND_ASYNC_NEW_POOL` and friends at `2712-2722`) only so that `F:ext/pdo/pdo_pool.c` can build a pool of C
handlers; no other caller outside `ext/async` exists. Here the pool, its circuit state and the strategy are the
extension's own, on layers 1 and 4 and S4's wait-record layer. The C-handler pool (`zend_async_pool_create_internal()`,
`ZEND_ASYNC_POOL_F_*_INTERNAL`, `THROW_IF_INTERNAL_RESOURCES`) is not built: it has no caller until the PDO pool, which
adds it with its own plan (section 8, item 1).

## 1. What the layer implements

| Kind | In this layer | Elsewhere |
|---|---|---|
| Classes | `Pool`, final, `Countable`, `CircuitBreaker`, `@strict-properties`, `@not-serializable` (`pool.stub.php`); `PoolException extends AsyncException`; `ServiceUnavailableException extends AsyncException` | the PDO pool: S10's later path |
| Types | `enum CircuitBreakerState { ACTIVE, INACTIVE, RECOVERING }`, `interface CircuitBreaker` (`getState()`, `activate()`, `deactivate()`, `recover()`), `interface CircuitBreakerStrategy` (`reportSuccess()`, `reportFailure()`, `shouldRecover()`), in `true_async.stub.php` as TrueAsync's `async.stub.php:319-402` | |
| `Pool` | `__construct(callable $factory, ?callable $destructor = null, ?callable $healthcheck = null, ?callable $beforeAcquire = null, ?callable $beforeRelease = null, int $min = 0, int $max = 10, int $healthcheckInterval = 0)`, `acquire(int $timeout = 0): mixed`, `tryAcquire(): mixed`, `release(mixed $resource): void`, `close()`, `isClosed()`, `count()`, `idleCount()`, `activeCount()`, `setCircuitBreakerStrategy(?CircuitBreakerStrategy)`, `getState()`, `activate()`, `deactivate()`, `recover()` | |
| Waits | the POOL wait kind: `acquire()` with no idle resource and no room (section 4) | |
| Healthcheck | a soft timer and a check coroutine (section 5) | |
| Collector | POOL's `collector_target`, the pool's `get_gc` (section 6) | |

What a user gets, from `site:pool.md`:

```php
$pool = new Async\Pool(
    factory: fn() => new Redis('127.0.0.1'),
    destructor: fn(Redis $r) => $r->close(),
    healthcheck: fn(Redis $r) => $r->ping() === true,      // idle resources, every 30 s
    beforeAcquire: fn(Redis $r) => $r->isConnected(),      // false: destroyed, the next one is taken
    min: 2, max: 10, healthcheckInterval: 30_000,
);

$redis = $pool->acquire();          // an idle one, a new one, or waits for a release
try {
    $redis->get('key');
} finally {
    $pool->release($redis);         // back to the idle buffer, or destroyed if beforeRelease says false
}

$pool->deactivate();                // the circuit breaker: acquire() throws ServiceUnavailableException
$pool->close();                     // waiters get PoolException, idle resources are destroyed
```

## 2. The pool

**State.** The idle resources in a ring buffer (TrueAsync's `circular_buffer_t` of zvals; ours: S3's ring or a
`zval` array of the same shape, chosen in S9.34), the count of resources out or held by a hook (`active`), the count
of resources handed out to callers (by object, PHP resource handle or other value, section 8 item 4), the count of places reserved
for woken waiters (`reserved`), the waiters' queue (`async_wait_queue_t`, as the channel's and the group's), the five
callables as `zend_fcall_info_cache` plus their `zval`, the circuit state, the strategy object, the healthcheck timer,
its interval and the running check coroutine. `count()` is `idle + active`, never above `max`; a resource counts as
active while a hook runs on it (`beforeAcquire`, the factory, `beforeRelease`, the healthcheck), as TrueAsync's
`056-pool_counts_resource_during_hooks`, because a hook may suspend.

**Constructor.** As TrueAsync (`pool.c:1365-1442`): `$min < 0`, `$max < 1`, `$min > $max`, `$healthcheckInterval < 0`
throw `ValueError` with its messages (`010`); a second `__construct()` throws `PoolException("Pool is already
initialized")` and changes nothing (`069`); the pre-warm calls the factory `$min` times in the constructor and stops at
the first exception, which the constructor throws (`025`, `050`). An object made without its constructor (Reflection's
`newInstanceWithoutConstructor()`; `unserialize()` is refused, section 8 item 3) answers as TrueAsync's
`068-pool_without_a_pool_throws`: `close()` and `isClosed()` return, `count()`, `idleCount()`, `activeCount()` return 0,
`tryAcquire()` returns `null`, the others throw `PoolException("Pool is not initialized")`.

**`acquire()`**, TrueAsync's `zend_async_pool_acquire()` (`pool.c:928-1022`), in this order:

1. closed: `PoolException("Pool is closed")` (`006`);
2. the circuit state INACTIVE: `ServiceUnavailableException("Service is unavailable (circuit breaker is open)")`
   (`041`); with a strategy set, the pool first asks its `shouldRecover()`, and on `true` moves to RECOVERING and
   goes on (section 3);
3. a place this caller may take: one reserved for it by its wake, or an unreserved one (`max - active - reserved > 0`);
4. an idle resource and a place: it is taken, `active` grows before `beforeAcquire` runs; a `false` (or a throw:
   question 1) destroys it and the caller starts again at 1 (`008`, `021`);
5. no idle resource, `idle + active < max` and a place: `active` grows before the factory runs (it may suspend:
   `024-pool_max_size_concurrent_factory`); a fresh resource skips `beforeAcquire` (`008`); a factory exception
   passes to the caller after the next waiter is woken (`047`); a factory call that fails without an exception (an
   unusable callable) throws `PoolException("Failed to create pool resource")` (`pool.c:170-177`); a factory
   returning `null` hands out `null`;
6. otherwise the caller waits (section 4) and starts again at 1 when woken.

**`tryAcquire()`** is steps 1-5 without the wait: `null` when closed, INACTIVE (after the same `shouldRecover()`) or full (`014`, `019`, `042`), and it
never takes a place reserved for a woken waiter (`061`). A factory that returns `null` makes `tryAcquire()`'s `null`
ambiguous (probed `p11_null_resource.php`); as TrueAsync, the documentation says so.

**`release($resource)`**, TrueAsync's `pool.c:1083-1118`: `beforeRelease` runs first (it may suspend); a `false`
reports a failure to the strategy, then destroys the resource (the caller's own reference stays valid: `067`) and
wakes the next waiter, which may now make a resource (`054`, `064`); otherwise the strategy gets a success, then the
resource goes to the idle buffer and wakes the next waiter, or is destroyed when the pool is closed (`015`). `active`
drops at the push or the destroy, after the strategy's call: TrueAsync drops it before, so a strategy that suspends
lets another `acquire()` make a resource while the released one is counted nowhere, and `count()` passes `max`
(section 8, item 6). A release with nothing handed out throws (section 8, item 4); a release of a value the pool did
not hand out, or of one already released: question 2. Both checks run when `release()` starts, before `beforeRelease`
can suspend: a second release of the same object while the first is in its hook would otherwise pass. They count the
resources handed out to callers, not `active`, which also counts the resources hooks and the check hold.

**`close()`**, TrueAsync's `pool.c:1120-1176`: idempotent (`013`); marks the pool closed, stops the healthcheck,
rejects every waiter with its own `PoolException("Pool is closed")` (`032`, `036`, `063`; TrueAsync shares one object,
section 8 item 11), then destroys every idle
resource, collecting the destructors' exceptions as a chain through `previous` and throwing the last one after the loop
(`018`); an exception in flight when `close()` starts (a destructor of the pool during an unwind) is kept (`057`).
Resources still out are destroyed at their `release()` (`015`). The pool's destructor (`dtor_obj`) closes it (`028`),
and `free_obj` only frees memory, as the channel's and the group's (`src/channel.c:1657`, `src/task_group.c:2561`):
PHP's GC calls `dtor_obj` while every object of the garbage is intact (`F:Zend/zend_gc.c:1914-1940`) and `free_obj`
one by one after (`2424-2444`), so a `destructor` closure capturing `$this` in a cycle with the pool would run already
freed under TrueAsync's close in `free_obj` (by reading, not probed).

**Hooks.** `factory()` and `destructor($r)` exceptions pass to the caller (`017`, `018`, `051`). TrueAsync swallows the
exceptions of `beforeAcquire`, `beforeRelease`, `healthcheck` and the strategy, and takes a throw as `false`
(`021`, `022`, `049`; probed `p4_hook_throws.php`: a `TypeError` inside `beforeAcquire` is seen nowhere and every
idle resource is destroyed and remade): question 1. A cancellation that reaches the caller while a hook is suspended
(inside a `ping()`) is cleared with the rest (`pool.c:271-273`), so the cancelled coroutine goes on. Here it passes to
the caller, for `beforeAcquire`, `beforeRelease` and the healthcheck alike, and the resource is destroyed: a `ping()`
cut short leaves its reply unread for the next user, a `ROLLBACK` cut short leaves a transaction open. The destroy
wakes the next waiter, reports nothing to the strategy (the resource did not fail), and runs the destructor with the
cancellation set aside, which reaches the caller even when the destructor throws (section 8, item 7).

## 3. The circuit breaker

`getState()`, `activate()`, `deactivate()`, `recover()` set and read one field, as TrueAsync (`pool.c:1610-1663`,
`040`). INACTIVE refuses `acquire()` with `ServiceUnavailableException` and makes `tryAcquire()` return `null` (`041`,
`042`); a waiter woken while the pool is INACTIVE takes the exception and hands its reservation to the next waiter,
which takes it too (`066`). `setCircuitBreakerStrategy(?CircuitBreakerStrategy)` keeps the object (`044`);
`reportSuccess($pool)` runs at a release the pool keeps, `reportFailure($pool, $error)` at a release `beforeRelease`
refuses, with `$error` the `Exception` it threw, or an `Exception("Resource validation failed")` for a `false`
(`045`, `052`, DECISIONS 2026-10-12 S9.34). The strategy changes the state by calling the pool it is given.

TrueAsync's documentation and code disagree here (probed `p7_should_recover.php`): nothing calls `shouldRecover()`,
RECOVERING lets every `acquire()` through as ACTIVE does, and the factory's, `beforeAcquire`'s and the healthcheck's
failures never reach `reportFailure()`, while `site:pool.md` and the tutorial (`site:tutors/13-pool.md:143-144`) say the
pool asks `shouldRecover()` while INACTIVE and RECOVERING lets limited requests through. Here, by Edmond's answer to
question 3 (B): while the pool is INACTIVE and a strategy is set, `acquire()` and `tryAcquire()` call
`shouldRecover()`; on `true` the pool moves to RECOVERING and serves the call, on `false` the call is refused as
before; RECOVERING serves as ACTIVE, and the strategy moves the pool on from its reports (`activate()` after a
success, `deactivate()` after a failure). Nothing parks on INACTIVE, and with no strategy INACTIVE stays until
`activate()` or `recover()`. An exception from the strategy is swallowed by TrueAsync (probed `p8_strategy_throws.php`);
here it passes to the caller of `release()` or `acquire()` once the call's own work is done (question 1, C).

## 4. Waiting: the POOL kind

`acquire()` with no place parks the caller on a POOL record in the pool's queue (`async_wait_link_outside()`, as the
channel's and the group's waits), with a timer of `$timeout` ms as a second wait when it is above 0, whose end
throws `TimeoutException` (`031`, `055`; section 8, item 10). TrueAsync's rules,
which its tests `058`-`066` pin and which this layer keeps:

- **Order.** Waiters are served in arrival order (`059`); a coroutine that releases and acquires again goes behind the
  waiters already parked (`058-pool_release_does_not_barge`).
- **Reservations.** A release, a refused `beforeRelease`, a failed factory, a resource the healthcheck destroyed, or a
  woken waiter that leaves, marks the oldest unreserved waiter reserved and queues its wake; until it runs, its place
  (an idle resource or room for a new one) is taken by nobody else, `tryAcquire()` included (`061`); surplus places
  stay free for anyone (`065`).
- **A waiter that leaves.** Cancelled, timed out, or refused by INACTIVE after its wake: it gives its reservation to
  the next unreserved waiter (`060`, `062`, `064`, `066`); with nobody left the resource stays idle (`062`).
- **Close.** Every waiter is taken out of the queue and woken with its own `PoolException` (`063`).

POOL's `info` prints TrueAsync's line `Pool(idle=%zu, active=%u, reserved=%u, max=%u)` (`pool.c:629-639`) in
`getAwaitingInfo()` and the deadlock report; `unlink` takes the record out of the queue; `abort` (a bailout, the
request's end) drops the record's reservation without a wake; `collector_target` reports the pool (section 6).

**Deadlock.** A pool's waiters at the global deadlock are woken as every parked coroutine is (S4's deadlock
resolution), with the caller's `DeadlockError` route; TrueAsync does the same (probed `p6_deadlock.php`). The pool is
not closed by it: unlike a channel, a pool has no other side whose loss the deadlock reports.

**A GC run inside a walk.** A release inside `close()`'s loops (a destructor, a woken waiter's exception) can start a
GC run that lets other coroutines run (S9.31, S9.33): `close()` takes each waiter and each idle resource out before the
call that releases, and re-reads the queue and the buffer after it, as `channel.c` after S9.33.

## 5. The healthcheck

TrueAsync runs the healthcheck inside the timer's callback, outside any coroutine (`pool.c:689-765`): a check that
suspends, as any real ping does, ends the request with `DeadlockError` (probed `p3_healthcheck_suspends.php`), and
`Async\current_coroutine()` inside it throws, which the swallowing hides (probed `p10_healthcheck_context.php`). Here
(section 8, item 2):

- the timer is a **soft** one (`src/channel.c:234-238`): it fires while anything else keeps the loop running and keeps
  nothing alive, as TrueAsync's `ZEND_ASYNC_EVENT_SET_HIDDEN` (probed `p9_healthcheck_keeps_alive.php`: the script
  ends). It is one-shot, as the channel's, and is armed again when a check ends, so no tick comes while a check runs;
  the pool is in a registry withdrawn at RSHUTDOWN, as the channels with a timer (`src/channel.c:402`), since after a
  fatal error no `dtor_obj` runs and `free_obj` comes after the reactor is gone;
- a tick starts one **check coroutine** (an internal entry, as the group's reporter, `src/task_group.c:1538-1575`) in a
  scope of its own under the global scope; no check starts once the graceful shutdown began (as `task_group.c:1549`
  refuses its reporter);
- the tick takes a counted reference to the pool's object for the check, released by the coroutine's
  `extended_dispose` (as `task_group.c:1527-1536`), so a check cancelled before its first line still lets the pool go;
  a free during a check waits for its end, and the collector sees the pool reached while it runs;
- the check takes an idle resource only while an unreserved place exists (`tryAcquire()`'s rule, `061`), so a place
  reserved for a woken waiter stays its; it counts the resource as active during the call (as TrueAsync), returns a
  healthy one through `release()`'s path (push, then wake the next unreserved waiter), destroys an unhealthy one and
  wakes the next waiter for the place (TrueAsync wakes one after its loop, `pool.c:764`), and while `count() < min`
  reserves a place (`active` grows) before its factory call, as `acquire()` does;
- a refill factory's exception is dropped: the place stays free, and the next `acquire()` calls the factory and throws
  to a caller who can handle it (a database down at every tick would otherwise add one error per tick); the
  healthcheck's own exceptions follow question 1's answer (C), with no caller: an `Exception` means
  unhealthy and an `Error` is printed; a destructor's exception is printed. Printed means the unobserved route
  (`async_unobserved_exception_add()`, `src/coroutine.c:299`), which prints at the request's end; never the scope's
  error route, which at the global scope cancels every coroutine of the request (`src/scope.c:957-985`);
- `close()` cancels a running check; the resource it holds is destroyed when the check's call returns, and until then
  `count()` still counts it; a destructor exception there is printed, not chained into `close()`'s (`018`); a
  healthcheck that calls `close()` on its own pool cancels its own check at its next suspension;
- a running check is an ordinary coroutine: a script that ends during a check waits for it (a ping to a dead host
  with a 10 s connect timeout delays the end by 10 s). The documentation tells the programmer to give the
  healthcheck its own IO timeout.

Its tests `048` and `049` wait with a top-level `delay()` for ticks (S14.9's rule) and change to wait for the event
they check (section 10). A soft timer keeps nothing from the global deadlock (`src/channel.c:234-238`), so a test
that parks main until a check runs needs another wait that keeps the loop running: a side coroutine in a long
`delay()`, cancelled once the event came.

## 6. Garbage collection and the async object collector

`get_gc` reports the idle resources, the five callables and the strategy, as TrueAsync's `pool.c:1308-1359`
(`053-pool_gc_factory_cycle`: a factory closure capturing the object that holds the pool is collected). The resources
out are not the pool's: the coroutine holding one reports it.

POOL's `collector_target` reports the pool as the waiter's target: whoever reaches the pool can end the wait with a
release, a `close()` or a free. A coroutine parked in `acquire()` on a pool no live coroutine reaches can never wake:
the holders of its resources need the pool object to release them. The healthcheck is the only other source of a free
place: it runs only over unreserved idle resources, which a pool with parked unreserved waiters has none of, and while
it runs its coroutine holds the pool (section 5). The collector cancels such a waiter as any other (S7).

## 7. Steps

- S9.34 The layer 5 block of `tests/lists/S9.txt` (section 10) with `--XFAIL--` naming S9.35 where a test needs the
  wait or the healthcheck; `src/pool.c`, `pool.h`, `pool.stub.php`; the exceptions and the circuit-breaker types in
  `exceptions.stub.php` and `true_async.stub.php`; the constructor and its pre-warm, `tryAcquire()`, `release()`,
  `close()`, the counts, the hooks, the circuit breaker and the strategy calls, `get_gc` and `free_obj`; `acquire()`
  that never waits (a full pool throws "not implemented yet" until S9.35).
- S9.35 The POOL kind (section 4): `acquire()`'s wait, the reservations, the caller's cancellation and timeout, `close()`
  waking waiters, `collector_target`; the healthcheck's timer and coroutine (section 5).
- S9.36 Layer review: the Critic over S9.34-S9.35, coverage of `src/pool.c`, Mull on the layer's diff, 100 fuzz seeds
  over the layer's tests and `collector/`, the measurements of section 10; the layer in `true-async-doc`
  (`ru/concepts/pool.md`, `ru/architecture/pool.md`), with the errors of section 9 corrected.
- S9.37 Security pass by `dev/SECURITY.md`: `$min` and `$max` against `memory_limit` (`max` up to `PHP_INT_MAX` sizes
  nothing up front), the destructors of resources dropped by `close()` and a free, a hook that closes or frees the
  pool while it runs, a hook that re-enters `acquire()` or `release()` of its own pool, the check coroutine and a
  free of the pool, PHP's GC during `close()`'s loops.

## 8. Departures from TrueAsync

Decided here by the project's rules (a TrueAsync bug or a rule of this project), each in `DECISIONS.md` 2026-10-12
S9.32, or Edmond's answers to questions 1-3 (items 12-14).

1. **No pool type in the core, no C-handler pool** (introduction): the core has no pool slots, and the PDO pool, the
   only C user, is out of the first version; it adds C handlers when it comes, with the circuit state and handler
   flags of `F:Zend/zend_async_API.h:2122-2168` as a model.
2. **The healthcheck runs in a coroutine** (section 5): TrueAsync's check cannot suspend, so a check over the network
   ends the request (probed `p3`); the soft timer keeps TrueAsync's "keeps nothing alive".
3. **`@not-serializable`**, as every class of this extension (`src/channel.stub.php:29`, `scope.stub.php:32`):
   TrueAsync's `Pool` serializes to `O:10:"Async\Pool":0:{}` and unserializes into an object without a pool (probed
   `p12`). `068` changes to build that object with `newInstanceWithoutConstructor()`.
4. **`release()` with nothing handed out throws `PoolException`**: TrueAsync decrements an unsigned zero, so
   `activeCount()` reads 4294967295 and every later room check is wrong (probed `p1`, `p2`). The check counts the resources handed out,
   taken when `release()` starts (section 2). Which values `release()` accepts beyond
   that: question 2.
5. **Tests wait for events, not for time** (S14.9's rule): `024-pool_max_size_concurrent_factory`, `048`, `049`, `054`,
   `056` change (section 10).
6. **`active` drops after the strategy's call** (section 2): TrueAsync's order lets `count()` pass `max` when the
   strategy suspends (by reading `pool.c:1092-1107`).
7. **A cancellation inside a hook reaches the caller** (section 2): TrueAsync clears it with the hook's exceptions, so
   a cancelled coroutine keeps running; the interrupted resource is destroyed, with no report to the strategy.
8. **The pool closes in `dtor_obj`, not `free_obj`** (section 2): user callables run while the GC's garbage is intact.
9. **The check's exceptions are printed or dropped, never routed** (section 5), and the check respects reservations,
   returns a resource through `release()`'s path and wakes a waiter per place it frees: a check that can suspend
   otherwise takes a place reserved for a woken waiter or leaves a waiter parked beside an idle resource
   (TrueAsync's check never suspends, so it never met either).
10. **`acquire(int $timeout = 0)` as TrueAsync**, not a cancellation token as the other waits of this extension take:
   a token would break TrueAsync's `acquire(5000)` with a `TypeError`, and `catch (TimeoutException)` would stop
   matching, since a `timeout()` token throws `OperationCanceledException` with the `TimeoutException` as its
   previous (`src/true_async.stub.php:53-57`). A token can come later as a second parameter without breaking anyone
   (`dev/PLAN.md` TODO).
11. **Each waiter rejected by `close()` gets its own `PoolException`**, as the channel's waiters
   (`src/channel.c:111`): TrueAsync's one shared object (`pool.c:1145-1148`) would carry into every waiter the
   `previous` one waiter's cancellation chains onto it (DECISIONS 2026-10-08 S9.18).
12. **A hook's `Exception` is a silent rejection, its `Error` destroys the resource and passes to the caller**
   (question 1, C); in the check an `Error` is printed; a strategy's exception passes to the caller.
13. **`release()` refuses an object the pool did not hand out or already got back** (question 2, B), by a weak-key
   count per object; scalars and arrays keep item 4's check.
14. **`shouldRecover()` is asked while INACTIVE** (question 3, B; section 3).

## 9. Errors in TrueAsync's documentation

For S9.36's pages, each checked against `pool.c` at `1fdacf8`: an `acquire()` timeout throws `TimeoutException`, not
`PoolException` (`site:reference/pool/acquire.md:31,40`; `true-async-doc` `ru/concepts/pool.md:75`); `tryAcquire()`
after `close()` returns `null`, it does not throw (`site:reference/pool/close.md:21`); RECOVERING behaves as ACTIVE and
`shouldRecover()` is never called (`site:components/pool.md:180`, `recover.md:20-23`; `ru/concepts/pool.md:155`);
`reportFailure()` is called only for a release `beforeRelease` refuses, with a synthetic exception
(`site:tutors/13-pool.md:143-144`; `ru/concepts/pool.md:195-196`); `healthcheckInterval: 0` disables the check, it
does not "check only on acquire" (`site:reference/pool/construct.md:72`); `CircuitBreakerStrategy` is an interface,
not a class to instantiate (`site:reference/pool/set-circuit-breaker-strategy.md:50`); `PoolException` extends
`AsyncException` (`site:components/exceptions.md:33` puts it under `\Exception`). What questions 1-3 change is
documented as decided.

## 10. Tests and measurements

**List** `tests/lists/S9.txt`, a block for layer 5, frozen in S9.34: the 70 reference tests of `tests/pool`
(`001`-`069`; two files carry 024 and two 030, `039` does not exist), each passing on the reference build above
(2026-10-12). None needs PDO or threads.

Changed with `changed:` and a DECISIONS line, judged by the Critic in S9.34: `068` (section 8, item 3); `048`, `049`
(they wait for a check, not for 120 and 100 ms), `054` (its waiters park before the broken release by a flag, not by
`delay(30)`), `024-pool_max_size_concurrent_factory` (the count is read once all three factories have started, not at
75 ms), `056` (the count is read at each hook's start and end, not every 10 ms). By reading, the answers change none of the
70: `021`, `022`, `049` throw an `Exception` (question 1, C), no test releases an object twice or a foreign one
(question 2), and the circuit-breaker tests `040`-`045`, `052`, `066` either set no strategy while INACTIVE or never
deactivate (question 3);
S9.34 lists every change before it freezes the block.

By what each uses: a wait in `acquire()` (`006`, `012`, `024`, `029`-`038`, `041`, `043`, `045`-`047`, `051`, `052`,
`054`, `056`, `058`-`067`; those whose `acquire()` finds a place at once are listed by S9.34 after a run) and the
healthcheck (`048`, `049`) name S9.35 with `--XFAIL--`; the others need only S9.34.

**Own tests**, one case each, written in the step that needs them; none repeats an operation hundreds of times and
none waits for time (Edmond, 2026-10-09 and 2026-10-12):

- S9.34: `release()` with nothing handed out; a second release of an object while the first waits in `beforeRelease`; `serialize()` refused; a factory returning `null` and `tryAcquire()`; a hook
  that calls `close()` on its own pool; a hook that frees the last reference to its pool; `close()` from a destructor
  of a resource while `close()` runs; `setCircuitBreakerStrategy()` replacing a strategy whose `reportSuccess()` is
  running; a strategy that suspends in `reportSuccess()` on a pool of `max: 1` while another coroutine acquires
  (`count()` stays 1, item 6); a cancel reaching a coroutine suspended inside `beforeAcquire` (item 7); a pool in a
  garbage cycle with a `destructor` closure capturing `$this` and idle resources, collected by `gc_collect_cycles()`
  (item 8); the cases of questions 1-3 as answered.
- S9.35: a waiter cancelled after its wake with the resource reserved, while a second waits (TrueAsync's `060`, plus
  one where the cancel comes before the wake); `acquire()` timing out after its wake; a waiter woken by a refused
  factory; the healthcheck suspending inside the check (the probe `p3`); `close()` during a running check; a tick
  skipped while the check runs; the check making resources up to `min`; a check freeing a place a waiter takes; a
  waiter that parks while the check holds the only resource, woken when the check returns it; a resource reserved
  for a woken waiter left alone by a tick; a refill factory that throws, printed while the other coroutines run on;
  no check started after `graceful_shutdown()`; a foreign release while the check holds the only resource; a free of the pool's last reference during a check; a
  bailout while parked in `acquire()`, on debug and ASAN; for the collector: a waiter on a pool nobody reaches
  found, and not found: a pool a running coroutine holds, a pool whose resource a suspended coroutine holds with the
  pool; `getAwaitingInfo()`'s line.

**Measurements** (S9.36, `dev/BENCHMARKS.md`), against the reference: B21, 100 000 `acquire()`/`release()` pairs from
10 coroutines over a pool of `max: 2`, instructions and allocations per pair; B22, the same over a pool of `max: 10`
with no waits.

**Core dependencies**: none.

## 11. Questions for Edmond

Asked one at a time on 2026-10-12, each with the PHP code and what the programmer sees; all three answered. The Critic's and the Sage's
findings of 2026-10-12 are in the sections above; `acquire()`'s argument, first a question, is item 10 of section 8
by the Sage's ruling (TrueAsync's signature, no API removed).

1. **Exceptions inside `beforeAcquire`, `beforeRelease`, the healthcheck and the strategy.** TrueAsync swallows them:
   a throw is `false` for the hooks and nothing for the strategy, so a bug (a `TypeError`) is seen nowhere and every
   idle resource is destroyed and remade (probed `p4`, `p8`, `p10`). A throw is also how a real check fails:
   `Redis::ping()` and PDO in its default exception mode throw on a dead connection, and TrueAsync's `021`, `022`,
   `049` pin "rejected, take the next" with a `RuntimeException`. Options: (A) as TrueAsync; (B) every throw destroys
   the resource and passes to the caller of `acquire()` or `release()`: after a database restart each `acquire()`
   throws once per dead idle connection, and the programmer's fix, a `catch` returning `false`, hides a `TypeError`
   again; (C) by PHP's own split: an `Exception` is a rejection and silent, as TrueAsync (`021`, `022`, `049`
   unchanged); an `Error` (a bug: `TypeError`, `Error` of a call on null) destroys the resource and passes to the
   caller, as a factory exception does; in the check, with no caller, an `Error` is printed at the request's end; a
   strategy's exception, always a bug, passes to the caller of `release()` once the release is done. Printing every
   rejection instead (through the unobserved route) is not an option: that route prints at the request's end as a
   fatal "Uncaught" with exit status 255 and keeps each exception, and the dead connection in its trace, until then
   (`src/scheduler.c:1293-1322`). Answered: C (Edmond, 2026-10-12 18:38).
2. **Which values `release()` accepts.** TrueAsync accepts any value: a foreign one enters the idle buffer, and an
   object released twice is handed to two coroutines at once, which mixes their traffic (probed `p2`). Options: (A) as
   TrueAsync, with item 4's check; (B) the pool counts, per object, how many times it handed it out and not yet got it
   back, in a weak-key table (`zend_weakrefs_hash_add()`/`_del()`, as `WeakMap`, destroyed in `free_obj`), and
   refuses with `PoolException` a release of an object not out; a count, not a set, because a factory may return one
   shared object (`fn() => $client`, a pool used as a semaphore); scalars and arrays keep (A). The table holds no
   reference, so an object leaked by a dead holder (`p5`) is still freed, and a freed object's handle reused by a new
   one is not taken for it. The cost is one table add and delete per pair (B21 measures it). Under (A) item 4's check
   can blame the wrong caller: a double release while another coroutine's resource is out brings the count to 0, and
   the innocent holder's `release()` throws. Answered: B, a TrueAsync bug, provided B keeps the code small (Edmond,
   2026-10-12 18:39); the table is PHP's own weak-reference hash, about a call per `acquire()` and `release()`, and
   S9.34 reports the lines it took.
3. **The circuit breaker's recovery.** Options: (A) as TrueAsync's code: `shouldRecover()` never called, RECOVERING
   as ACTIVE, the state changes only by `activate()`, `deactivate()`, `recover()`; (B) as TrueAsync's documentation
   says it is asked: while INACTIVE with a strategy set, `acquire()` and `tryAcquire()` call `shouldRecover()`, and on
   `true` the pool moves to RECOVERING and serves the call; RECOVERING serves as ACTIVE, and the strategy moves the
   pool on from its reports (`activate()` or `deactivate()`), as in (A); nothing parks on INACTIVE; (C) no circuit
   breaker in this layer (an interface nobody implements but the pool, three types, 9 tests). Answered: B (Edmond,
   2026-10-12 18:39).

Not a question (`dev/PLAN.md`, TODO): a coroutine that ends holding a resource keeps it out for good (probed
`p5_cancel_holder.php`: a pool of `max: 1` is then empty forever); `try`/`finally` is the programmer's, as in
TrueAsync. A `$pool->use(fn($r) => ...)` that releases in its own `finally` would remove the case.
