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
- `Async\get_deadlocked_coroutines()`: the coroutines parked on targets that nothing running can
  complete or cancel, found while other coroutines run. INI `true_async.partial_deadlock` (`report`
  by default: one `E_WARNING` per such coroutine at the scheduler's idle point; `cancel`: the
  warning, then `AsyncCancellation("Deadlock detected")` into each but main; `off`) and
  `true_async.partial_deadlock_interval` (ms between the automatic runs, default `1000`).
