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

/**
 * Waits until `$awaitable` completes: returns its result, or throws the exception it ended with.
 * A `$cancellation` that completes first throws OperationCanceledException.
 */
function await(Completable $awaitable, ?Completable $cancellation = null): mixed {}

/** The first result of `$triggers`, or null when it is empty; the first error is thrown. */
function await_any_or_fail(iterable $triggers, ?Awaitable $cancellation = null): mixed {}

/** `[the first result or null, the errors before it]`: errors are collected, not thrown. */
function await_first_success(iterable $triggers, ?Awaitable $cancellation = null): mixed {}

/** Every result by its key; the first error is thrown. */
function await_all_or_fail(iterable $triggers, ?Awaitable $cancellation = null, bool $preserveKeyOrder = true): array {}

/** `[results, errors]` of every trigger, by their keys. */
function await_all(iterable $triggers, ?Awaitable $cancellation = null, bool $preserveKeyOrder = true, bool $fillNull = false): array {}

/** The first `$count` results by their keys; the first error is thrown. */
function await_any_of_or_fail(int $count, iterable $triggers, ?Awaitable $cancellation = null, bool $preserveKeyOrder = true): array {}

/** `[results, errors]` once `$count` triggers succeeded (all of them for 0 or less). */
function await_any_of(int $count, iterable $triggers, ?Awaitable $cancellation = null, bool $preserveKeyOrder = true, bool $fillNull = false): array {}

/** Gives up the CPU: the current coroutine goes to the back of the run queue and runs again in its turn. */
function suspend(): void {}

/**
 * Parks the current coroutine for `$ms` milliseconds; 0 gives up the CPU as suspend() does. Returns at
 * once when no coroutine runs (async is off).
 */
function delay(int $ms): void {}

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
 * The coroutines nothing can wake any more: each waits only for targets that no code able to run
 * can complete or cancel, and no such code holds the coroutine itself. The walk runs now; holding the
 * returned coroutines makes them reachable, and they may be cancelled.
 *
 * @return Coroutine[]
 */
function get_deadlocked_coroutines(): array {}

/**
 * Starts the graceful shutdown: every coroutine is cancelled with `$cancellationError`, or with
 * AsyncCancellation("Graceful shutdown") when it is null. A shutdown already started keeps its own
 * cancellation and the argument is ignored.
 */
function graceful_shutdown(?AsyncCancellation $cancellationError = null): void {}
