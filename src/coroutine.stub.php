<?php

/** @generate-class-entries */

namespace Async;

/**
 * @strict-properties
 * @not-serializable
 */
final class Coroutine implements Completable
{
    public function getId(): int {}

    /** The coroutine's next enqueue puts it at the front of the run queue, once. */
    public function asHiPriority(): Coroutine {}

    /** The result once the coroutine finished, else null. */
    public function getResult(): mixed {}

    /** The exception once the coroutine finished with one, else null. */
    public function getException(): mixed {}

    /** The backtrace of a queued or suspended coroutine, else null. */
    public function getTrace(int $options = DEBUG_BACKTRACE_PROVIDE_OBJECT, int $limit = 0): ?array {}

    public function getSpawnFileAndLine(): array {}

    public function getSpawnLocation(): string {}

    public function getSuspendFileAndLine(): array {}

    public function getSuspendLocation(): string {}

    public function isStarted(): bool {}

    public function isQueued(): bool {}

    public function isRunning(): bool {}

    public function isSuspended(): bool {}

    public function isCancelled(): bool {}

    public function isCancellationRequested(): bool {}

    public function isCompleted(): bool {}

    public function getAwaitingInfo(): array {}

    public function cancel(?AsyncCancellation $cancellation = null): void {}

    /**
     * Called as fn(Coroutine $coroutine) in a new coroutine once this one finishes, after its waiters,
     * with the other finally callbacks of this coroutine; at once when it has finished. Not called when
     * the coroutine ended in a fatal error. What it throws goes up the coroutine's scope.
     */
    public function finally(\Closure $callback): void {}
}
