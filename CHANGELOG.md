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
- A garbage collection runs in its own coroutine. `gc_collect_cycles()` waits for it to end,
  destructors included; a full root buffer only starts it, and the code that filled it goes on
  unless it adds another 100 000 possible roots before the collection runs.
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
  A fiber whose coroutine is cancelled before its body runs ends without running it, and `start()`
  throws that cancellation.
- A fiber's `resume()` and `throw()` throw `FiberError` once the scheduler has stopped or the fiber was destroyed.
- One coroutine resumes a fiber at a time, and a fiber whose starter was cancelled cannot be started again.
- An exception printed as uncaught at the end of the request may use async in its `__toString()`:
  a coroutine it spawns runs before the request ends, unless that `__toString()` throws or exits.
- An out-of-memory fatal error is printed once, as without the extension, unless it happens in a
  destructor at the end of the request.
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
- `Async\TaskGroup` and `Async\TaskSet`: tasks under a concurrency limit and a queue, `trySpawn()` that starts a task now or returns false, Futures over their results, `foreach` in completion order, TaskSet's `joinNext()`, `joinAny()` and `joinAll()` that take what they return, a numeric string key as the integer key; dropping a group cancels its unfinished tasks and reports the errors nobody read; a task still queued when `exit()`, an uncaught error or `Async\graceful_shutdown()` cancels the request never starts and ends with `AsyncCancellation` ([S9](dev/plans/S9-taskgroup.md)).
- A coroutine whose body has returned or thrown keeps its outcome when its scope is cancelled while its
  closure is released, as when the closure held the last reference to the scope or the group
  ([S9](dev/plans/S9-taskgroup.md)).
- `Async\Pool`, `Async\PoolException`, `Async\ServiceUnavailableException` and the circuit breaker types (`CircuitBreaker`, `CircuitBreakerState`, `CircuitBreakerStrategy`): `acquire()` and `tryAcquire()` without waiting yet, `release()` that refuses a value the pool did not hand out or got back already, `beforeAcquire` and `beforeRelease` hooks whose `Error` passes to the caller, a strategy asked `shouldRecover()` while INACTIVE ([S9](dev/plans/S9-pool.md), S9.34).
- `Async\Pool::acquire()` waits for a release in arrival order, with `$timeout` throwing `TimeoutException`, and `close()` wakes the waiters with `PoolException`; the healthcheck runs every `healthcheckInterval` ms in a coroutine of its own, so it may suspend ([S9](dev/plans/S9-pool.md), S9.35).
- `Async\Pool` keeps a cancellation, and the exception that started `close()` or a call, on top of a destructor's exception, and `acquire()` on a pool closed while `shouldRecover()` ran throws `PoolException` without calling the factory ([S9](dev/plans/S9-pool.md), S9.36).
- The `Async\Pool` constructor counts the resources its factory acquires toward `min` and stays within `max`, stops when the factory closes the pool, destroying the resource just made, and wakes the `acquire()` callers that parked while the factory ran ([S9](dev/plans/S9-pool.md), S9.37).
- `Async\iterate()`: runs a callback for each element of an array or `Traversable` in coroutines of a child scope, at most `$concurrency` at a time, stops at the first exception or at a `false`, cancels the coroutines the callbacks spawned that still run after the walk (with `$cancelPending: false` it waits for them), and stops the walk when the caller is cancelled ([S9](dev/plans/S9-iterate.md), S9.40).
- `Async\iterate()` stops the walk on a cancel of the caller or a spawned coroutine's error even when a callback catches the cancellation, lets a coroutine cleaning up after the walk's cancel finish, and keeps a destructor's error of the walk under the thrown exception ([S9](dev/plans/S9-iterate.md), S9.41).
- `Async\iterate()` throws a cancellation a callback throws itself, such as an expired `timeout()`, and stops the walk; before, it ended that callback silently ([S9](dev/plans/S9-iterate.md), S9.41).
- `Async\iterate()` and `Async\Pool::release()` no longer also print as "Uncaught" an exception thrown again by a coroutine a callback spawned, or by a hook and the destructor, when the thrown exception is it or holds it as a previous; before, the request ended with exit code 255 ([S9](dev/plans/S9-iterate.md), S9.42).
- `Async\FileSystemWatcher` and `Async\FileSystemEvent` on Linux: a `foreach` over the changes of a file or directory, recursive, coalesced or raw, debounced, filtered by extension in every mode; an open watcher nobody iterates keeps nothing waiting; other platforms throw for now ([S9](dev/plans/S9-fs-watcher.md), S9.44).
- `Async\FileSystemWatcher` on Windows, on a directory-change op of the core and ior (core `async-core-io-2026-10-13-8`, [S9](dev/plans/S9-fs-watcher.md), S9.45).
- `Async\FileSystemWatcher`: every iterator made before a monitoring error throws it once; an extension with `.` or `/` inside is a `ValueError` (S9.46).
- `Async\available_parallelism()`, `Async\loadavg()`, `Async\CpuSnapshot`, `Async\cpu_usage()` and `Async\runtime_stats()`: the CPUs a process may use (affinity and the Linux cgroup or Windows job object quota, rounded up), CPU counters, CPU use per request with `process_percent` a share of the usable CPUs, and the scheduler's counters ([S9](dev/plans/S9-runtime-info.md), S9.49).
- `Async\FileSystemWatcher` with `recursive: true` no longer crashes a coroutine with a small stack on a deep tree, and a fatal error during the watch no longer leaks a descriptor past the request (S9.47).
- `map()`, `catch()` and `finally()` on a `recvAsync()`, signal or group Future nobody else holds complete
  when that Future does; before, the Future went with its object and the chain never completed. A dropped
  `recvAsync()->map()` chain stays queued and takes a channel value; its channel, group or signal wait holds
  it, so PHP's GC collects it only with that channel or group ([S9](dev/plans/S9-taskgroup.md), S14.1).
- On the Poll queue, a stream that takes the number of a socket curl just closed keeps its wait record, and a cancelled `socket_accept()` no longer crashes (core `async-core-io-2026-10-12-1`, S8.4a).
- An exception whose unwind starts an automatic GC run reaches its `catch` unchanged and an `exit()` there stays an exit; a coroutine cancelled while it waits for a GC run keeps its temporaries collectable (core `async-core-io-2026-10-12-2`, S14.6).
- A file given to curl and closed by another coroutine while `curl_exec()` waits ends the transfer with PHP's "resource has gone away" warning instead of a write to freed memory (core `async-core-io-2026-10-12-3`, S8.7).
