<?php

/** @generate-class-entries */

namespace Async;

/**
 * A TaskGroup whose reads consume: an entry leaves the set when its result is delivered.
 *
 * @strict-properties
 * @not-serializable
 */
final class TaskSet implements Awaitable, \Countable, \IteratorAggregate
{
    /** @implementation-alias Async\TaskGroup::__construct */
    public function __construct(?int $concurrency = null, ?int $queueLimit = null, ?Scope $scope = null) {}

    /** @implementation-alias Async\TaskGroup::spawn */
    public function spawn(callable $task, mixed ...$args): void {}

    /** @implementation-alias Async\TaskGroup::spawnWithKey */
    public function spawnWithKey(string|int $key, callable $task, mixed ...$args): void {}

    /** @implementation-alias Async\TaskGroup::trySpawn */
    public function trySpawn(callable $task, mixed ...$args): bool {}

    /** @implementation-alias Async\TaskGroup::trySpawnWithKey */
    public function trySpawnWithKey(string|int $key, callable $task, mixed ...$args): bool {}

    /**
     * The next task to end, in completion order, taken from the set; waits while the set is open and rejects
     * when it completes with nothing to take.
     *
     * @throws AsyncException on an empty set.
     * @implementation-alias Async\TaskGroup::race
     */
    public function joinNext(): Future {}

    /**
     * The next successful task, taken from the set; rejects with a CompositeException of the failures, which it
     * takes, when the set settles with failures only.
     *
     * @throws AsyncException on an empty set.
     * @implementation-alias Async\TaskGroup::any
     */
    public function joinAny(): Future {}

    /**
     * all() of TaskGroup, after which every entry leaves the set.
     *
     * @implementation-alias Async\TaskGroup::all
     */
    public function joinAll(bool $ignoreErrors = false): Future {}

    /** @implementation-alias Async\TaskGroup::cancel */
    public function cancel(?AsyncCancellation $cancellation = null): void {}

    /** @implementation-alias Async\TaskGroup::close */
    public function close(): void {}

    /** @implementation-alias Async\TaskGroup::dispose */
    public function dispose(): void {}

    /** @implementation-alias Async\TaskGroup::isFinished */
    public function isFinished(): bool {}

    /** @implementation-alias Async\TaskGroup::isClosed */
    public function isClosed(): bool {}

    /**
     * The tasks not delivered yet.
     *
     * @implementation-alias Async\TaskGroup::count
     */
    public function count(): int {}

    /** @implementation-alias Async\TaskGroup::awaitCompletion */
    public function awaitCompletion(): void {}

    /** @implementation-alias Async\TaskGroup::finally */
    public function finally(\Closure $callback): void {}

    /** @implementation-alias Async\TaskGroup::getIterator */
    public function getIterator(): \Iterator {}
}
