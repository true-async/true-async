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
  it and runs again in its turn; refused in the scheduler's own work and inside a Fiber the
  scheduler has not adopted.
- `Async\Coroutine::getSuspendFileAndLine()`, `getSuspendLocation()` and `getTrace()` for a
  coroutine parked in `suspend()`.
- `Async\await()`: waits for a coroutine to finish and returns its result, or throws its
  exception (the same object at every call, and the exception no longer ends the request as
  uncaught); refused for the current coroutine itself.
- `Async\Coroutine::getAwaitingInfo()`: `await: coroutine #<id>` for a coroutine parked in
  `await()`, an empty array otherwise.
- A garbage collection runs in its own coroutine and the code that started it, main included,
  waits for it to end, destructors included.
