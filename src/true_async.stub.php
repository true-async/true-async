<?php

/** @generate-class-entries */

namespace Async;

/** An object a coroutine can wait for; implemented only by the classes of this extension. */
interface Awaitable {}

/** An awaitable that completes once, with a result or an exception. */
interface Completable extends Awaitable
{
    public function cancel(?AsyncCancellation $cancellation = null): void;

    public function isCompleted(): bool;

    public function isCancelled(): bool;
}

/** Starts `$task` in a new coroutine; it runs once the current coroutine yields or ends. */
function spawn(callable $task, mixed ...$args): Coroutine {}

/** Waits until `$awaitable` completes: returns its result, or throws the exception it ended with. */
function await(Completable $awaitable): mixed {}

/** Gives up the CPU: the current coroutine goes to the back of the run queue and runs again in its turn. */
function suspend(): void {}

/**
 * Calls `$closure` with the current coroutine protected from cancellation: a cancellation requested
 * meanwhile is thrown when the outermost protect() returns. Returns what `$closure` returns.
 */
function protect(\Closure $closure): mixed {}

/** The coroutine that is running; the script's top level runs in the main coroutine. */
function current_coroutine(): Coroutine {}

/**
 * Every coroutine that was spawned and has not finished, the main one included.
 *
 * @return Coroutine[]
 */
function get_coroutines(): array {}

/**
 * Starts the graceful shutdown: every coroutine is cancelled with `$cancellationError`, or with
 * AsyncCancellation("Graceful shutdown") when it is null. A shutdown already started keeps its own
 * cancellation and the argument is ignored.
 */
function graceful_shutdown(?AsyncCancellation $cancellationError = null): void {}
