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

/** Gives up the CPU: the current coroutine goes to the back of the run queue and runs again in its turn. */
function suspend(): void {}

/** The coroutine that is running; the script's top level runs in the main coroutine. */
function current_coroutine(): Coroutine {}

/**
 * Every coroutine that was spawned and has not finished, the main one included.
 *
 * @return Coroutine[]
 */
function get_coroutines(): array {}
