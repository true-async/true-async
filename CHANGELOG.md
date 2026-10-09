# Changelog

All notable changes to True Async are recorded here. The format follows
[Keep a Changelog 1.1.0](https://keepachangelog.com/en/1.1.0/), and versions follow
[Semantic Versioning 2.0.0](https://semver.org/spec/v2.0.0.html). Only what a user of the extension
can see goes here; tests, tools and CI are in the git history.

## [Unreleased]

### Added

- The `true_async` module: builds with `phpize` against a core built from `async-core-io`, and
  inside the core tree on Windows; `configure` stops on a core without the scheduler API.
- INI `true_async.enable` (system, default `0`): the extension registers as the core's scheduler
  only when it is on.
- A `phpinfo()` section with the version and the INI entries.
- `Async\spawn()`, `Async\current_coroutine()` and `Async\get_coroutines()`: spawned coroutines
  run in FIFO order once the script's main code ends, before the shutdown functions; an
  unhandled exception in a coroutine nobody holds ends the request as an uncaught exception.
- `Async\Coroutine`: `getId()`, `asHiPriority()`, `getResult()`, `getException()`,
  `getSpawnFileAndLine()`, `getSpawnLocation()` and the state methods `isStarted()`, `isQueued()`,
  `isRunning()`, `isSuspended()` (false for the running coroutine), `isCancelled()`,
  `isCancellationRequested()`, `isCompleted()`.
- `Async\signal()`: a Future that completes when the process receives the signal, or fails when its
  cancellation completes first. On Windows, in the main thread of the CLI, Ctrl+C, Ctrl+Break and
  closing the console complete the Futures of `SIGINT`, `SIGBREAK` and `SIGHUP`. While such a Future
  waits, Ctrl+C and Ctrl+Break do not end the process; closing the console still ends it a few
  seconds later. Futures of other cases complete only through their cancellation; awaited without
  one, they never return. Elsewhere on Windows `Async\signal()` throws `Error`, unless its
  cancellation has already completed.
- `Async\suspend()`: the current coroutine, main included, yields to the coroutines queued ahead of
  it and runs again in its turn; refused in the scheduler's own work.
- `Async\Coroutine::getSuspendFileAndLine()`, `getSuspendLocation()` and `getTrace()` for a
  coroutine parked in `suspend()`.
- `Async\await()`: waits for a coroutine to finish and returns its result, or throws its
  exception (the same object at every call, and the exception no longer ends the request as
  uncaught); refused for the current coroutine itself.
- `Async\Coroutine::getAwaitingInfo()`: `await: coroutine #<id>` for a coroutine parked in
  `await()`, an empty array otherwise.
- A garbage collection runs in its own coroutine and the code that started it, main included,
  waits for it to end, destructors included.
- `Async\Coroutine::cancel()`: cancels a coroutine with `AsyncCancellation` (or the given one),
  delivered at its next switch; a coroutine cancelled before it ran finishes without running.
- `Async\protect()`: a cancellation requested inside the closure waits until the outermost
  `protect()` returns.
- `Async\graceful_shutdown()`: cancels every coroutine once, with the given cancellation or
  "Graceful shutdown". An unhandled exception that ends the request and `exit()` in a coroutine
  start it too, so the other coroutines can run their `catch` and `finally` blocks.
- A deadlock ends the request with `Async\DeadlockError` after every waiting coroutine gets
  `AsyncCancellation("Deadlock detected")`; `true_async.debug_deadlock` (default on) prints which
  coroutine waits for which, where `display_errors` shows the error.
- An exception nobody observed (by `await()` or `getException()`) is thrown where the last
  reference to its coroutine goes; one still unobserved at the request's end is printed as uncaught.
- A `Fiber` runs as a coroutine of the scheduler: `Async\suspend()` and `Async\await()` work inside
  it, its caller waits for it as for any coroutine, and `exit()` in it ends the request as in a
  coroutine. Fibers left suspended when nothing else runs are closed with no deadlock reported.
- A destructor run at the end of the request may wait or yield: the remaining destructors run in
  the meantime, so one destructor can wait for another.
- A parked coroutine takes no VM page from `memory_limit`; up to 1024 finished stacks are reused.
- `Async\get_deadlocked_coroutines()` and INI `true_async.partial_deadlock` (`report`, `cancel`, `off`), `true_async.partial_deadlock_interval` (ms, 5000 by default, at least 1000 or 0): finds coroutines that can never wake; a coroutine whose scope, or a parent scope, has a `Scope` object live code holds is not found ([S7](dev/plans/S7.md)).
- `Async\Scope`, `Async\ScopeProvider`, `Async\SpawnStrategy` and `Async\spawn_with()`: a coroutine
  is spawned into a scope, the current coroutine's or at the top level the global scope;
  `Scope::cancel()` cancels the scope's coroutines and child scopes, and with safe disposal
  (`allowZombies()`, or inherited from the global scope) a started coroutine runs on to its end
  instead ([S9](dev/plans/S9-scope.md)).
- `Scope::awaitCompletion()` waits until no coroutine of the scope or of its child scopes runs
  ([S9](dev/plans/S9-scope.md)); `get_deadlocked_coroutines()` and `true_async.partial_deadlock`
  find a waiter there once no coroutine of that subtree can run, and leave the subtree of an
  `await_*()` walk alone while the walk can still throw.
- `Scope::setExceptionHandler()` and `Scope::setChildScopeExceptionHandler()`: an error of a
  coroutine nobody awaits goes to its scope's handler, then up through the parent scopes; a scope
  without a handler that takes it is cancelled with its child scopes and coroutines on the way, so
  an error in the global scope cancels the coroutines that have not started yet
  ([S9](dev/plans/S9-scope.md)).
- `Scope::dispose()`, `disposeSafely()`, `disposeAfterTimeout()` and `awaitAfterCancellation()`:
  disposal cancels the scope with no error, or closes it with its idle child scopes when nothing is
  left to cancel; the timeout disposes the scope once it passes; `awaitAfterCancellation()` waits
  until no coroutine of a cancelled scope's subtree is left, zombies included, and hands the errors
  that come meanwhile to its handler ([S9](dev/plans/S9-scope.md)).
- `Scope::finally()` and `Coroutine::finally()`: the handlers run in coroutines of a child scope once
  the scope is disposed or the coroutine finishes, also when the coroutine's error cancels its scope
  or ends the request; one handler's error goes up from that child scope as itself (under the error of a
  destructor of what it held, if that throws), several as an
  `Async\CompositeException`; a handler added to a finished coroutine or a gone scope runs at once;
  the last of those coroutines releases the handlers and what they hold, so a destructor there may
  wait, unless a deadline cancelled it before it started or a handler called `exit()`
  ([S9](dev/plans/S9-scope.md)).
- `Async\Channel`, `Async\ChannelException` and `Async\ChannelCloseReason`: `send()` and `recv()`
  pass values between coroutines through a buffer of `capacity` values, or hand each one over
  directly when the capacity is 0; both take an optional cancellation token, `sendAsync()` sends
  without waiting, and `close()` fails the waiting senders and receivers with a `ChannelException`
  naming the reason, while `recv()` still gets the values left.
  The buffer grows as values arrive, within `memory_limit` ([S9](dev/plans/S9-channel.md)).
- `Async\Channel::recvAsync()` returns a `Future` of the next value; `foreach` and `getIterator()`
  receive until the channel is closed, an explicit `close()` ending the loop quietly
  ([S9](dev/plans/S9-channel.md)).
- `await_*` items and every cancellation token take a `Completable` only: a `Channel` is refused, and
  `await_*` over `recvAsync()` Futures waits on channels; `timeout()` is declared to return `Completable`
  ([S9](dev/plans/S9-taskgroup.md)).
- `Async\Channel`'s `noProducerTimeout` and `noConsumerTimeout` close a channel that waits too long; a
  channel closes when the scope it was made in is cancelled, takes an error, is disposed or is freed, and
  the partial deadlock collector finds coroutines parked on it ([S9](dev/plans/S9-channel.md)).
- `Async\TaskGroup` and `Async\TaskSet`: `spawn()`, `spawnWithKey()`, `trySpawn()` and `trySpawnWithKey()`
  run callables as coroutines of one scope, at most `concurrency` at a time and the rest queued; `all()`,
  `race()` and `any()` (`joinAll()`, `joinNext()` and `joinAny()`, which take what they deliver, on a
  `TaskSet`) return Futures; `close()`, `cancel()`, `dispose()` and `finally()`. Dropping a group cancels
  its unfinished tasks without waiting, runs its finally handlers once and reports the errors no read took
  as an `Async\CompositeException`. A numeric string key is the integer key, as in an array. `awaitCompletion()`
  while a closed group has tasks left, `foreach` and waiting on a full queue throw "not implemented yet" for now ([S9](dev/plans/S9-taskgroup.md)).
- A coroutine whose body has returned or thrown keeps its outcome when its scope is cancelled while its
  closure is released, as when the closure held the last reference to the scope or the group
  ([S9](dev/plans/S9-taskgroup.md)).
- `map()`, `catch()` and `finally()` on a `recvAsync()`, signal or group Future nobody else holds complete
  when that Future does; before, the Future went with its object and the chain never completed. A dropped
  `recvAsync()->map()` chain now stays queued until PHP's GC collects it and takes a channel value
  ([S9](dev/plans/S9-taskgroup.md)).
