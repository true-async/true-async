<?php

/** @generate-class-entries */

namespace Async;

/**
 * The cancellation token of timeout(): one deadline, taken when timeout() returns, for every wait
 * that uses it. Once it fires a wait throws OperationCanceledException whose previous is a
 * TimeoutException.
 *
 * @strict-properties
 * @not-serializable
 */
final class Timeout implements Completable
{
    private function __construct() {}

    /** Ends it for good: its waiters, and every later use, throw with `$cancellation` as the previous. */
    public function cancel(?AsyncCancellation $cancellation = null): void {}

    /** Fired, cancelled, or past its deadline. */
    public function isCompleted(): bool {}

    public function isCancelled(): bool {}
}
