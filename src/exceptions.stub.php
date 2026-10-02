<?php

/** @generate-class-entries */

namespace Async;

/**
 * Thrown into a cancelled coroutine. An Error, so `catch (\Exception)` does not stop a
 * cancellation.
 */
class AsyncCancellation extends \Error {}

/** Common type of exception. */
class AsyncException extends \Exception {}

/** Thrown when every coroutine waits and nothing can wake one. */
class DeadlockError extends \Error {}

/**
 * Several exceptions raised together.
 * @strict-properties
 */
final class CompositeException extends \Exception
{
    private array $exceptions;

    public function addException(\Throwable $exception): void {}

    /** @return array<int, \Throwable> */
    public function getExceptions(): array {}
}
