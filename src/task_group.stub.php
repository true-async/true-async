<?php

/** @generate-class-entries */

namespace Async;

/**
 * Runs callables as coroutines of one scope, at most `$concurrency` at a time with the rest queued, and keeps
 * their results by key. Reads do not consume: all(), race() and any() may be called again.
 *
 * @strict-properties
 * @not-serializable
 */
final class TaskGroup implements Awaitable, \Countable, \IteratorAggregate
{
    /**
     * @param int|null $concurrency The most tasks running at once; null or 0: no limit.
     * @param int|null $queueLimit The most queued tasks; null: 2 * $concurrency, no limit without one; 0: no limit.
     *   On a full queue spawn() waits.
     * @param Scope|null $scope The scope the tasks run in, which the group owns from then on; null: a new child
     *   scope of the current one.
     */
    public function __construct(?int $concurrency = null, ?int $queueLimit = null, ?Scope $scope = null) {}

    /**
     * Starts the callable now when a slot is free, else queues it, waiting while the queue is full; its key is
     * the next integer.
     *
     * @throws AsyncException on a closed group, a closed scope, or a next integer a spawnWithKey() took (the
     *   next call takes the integer after it); until S9.29 also on a full queue ("not implemented yet").
     */
    public function spawn(callable $task, mixed ...$args): void {}

    /**
     * spawn() under `$key`; a numeric string is the integer key, as in an array.
     *
     * @throws AsyncException on a closed group, a closed scope or a key already present.
     */
    public function spawnWithKey(string|int $key, callable $task, mixed ...$args): void {}

    /**
     * Starts the callable when a slot is free and returns true; else queues nothing and returns false.
     *
     * @throws AsyncException on a closed group, a closed scope, or a next integer a spawnWithKey() took (the
     *   next call takes the integer after it).
     */
    public function trySpawn(callable $task, mixed ...$args): bool {}

    /**
     * trySpawn() under `$key`; a numeric string is the integer key, as in an array.
     *
     * @throws AsyncException on a closed group, a closed scope or a key already present.
     */
    public function trySpawnWithKey(string|int $key, callable $task, mixed ...$args): bool {}

    /**
     * Settles once nothing runs and nothing is queued, with the results by key in spawn order, or rejects with a
     * CompositeException of the errors in spawn order.
     *
     * @param bool $ignoreErrors Resolve with the results only; the errors left out need no report.
     */
    public function all(bool $ignoreErrors = false): Future {}

    /**
     * The first task settled at the call in spawn order, else the first task to end: its result, or its own
     * exception.
     *
     * @throws AsyncException on an empty group.
     */
    public function race(): Future {}

    /**
     * The first successful task settled at the call in spawn order, else the first to succeed; rejects with a CompositeException when the group settles with errors only.
     *
     * @throws AsyncException on an empty group.
     */
    public function any(): Future {}

    /** @return array The results of the successful tasks by key. */
    public function getResults(): array {}

    /** @return array The exceptions of the failed tasks by key, which then need no report. */
    public function getErrors(): array {}

    /** The errors present need no report. */
    public function suppressErrors(): void {}

    /**
     * Closes the group, ends the queued tasks with the cancellation without starting them and cancels the
     * scope, which interrupts the running ones.
     *
     * @param AsyncCancellation|null $cancellation null: AsyncCancellation("TaskGroup cancelled").
     */
    public function cancel(?AsyncCancellation $cancellation = null): void {}

    /** No more tasks are taken; the queued and running ones go on. */
    public function close(): void {}

    /** cancel() with AsyncCancellation("Scope is being disposed due to TaskGroup disposal"). */
    public function dispose(): void {}

    /** Nothing runs and nothing is queued; an open group can take tasks again. */
    public function isFinished(): bool {}

    public function isClosed(): bool {}

    /** The tasks queued, running and settled. */
    public function count(): int {}

    /**
     * Waits until the closed group has no task left; never throws a task's error.
     *
     * @throws AsyncException on an open group.
     */
    public function awaitCompletion(): void {}

    /**
     * The callback runs once, with the group as its argument, in a coroutine of the group's scope when the
     * closed group has no task left; on a completed group it is called at once, in the caller, and its exception
     * leaves finally().
     */
    public function finally(\Closure $callback): void {}

    /** Yields key => [result, null] or key => [null, error] as the tasks end; only for foreach. */
    public function getIterator(): \Iterator {}
}
