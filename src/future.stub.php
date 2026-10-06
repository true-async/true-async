<?php

/** @generate-class-entries */

namespace Async;

/**
 * The writing end of one completion: complete() or error() sets the outcome that every Future on
 * this state reads.
 *
 * @strict-properties
 * @not-serializable
 */
final class FutureState
{
    public function __construct() {}

    /** Completes the operation with `$result`; an AsyncException when it is already completed. */
    public function complete(mixed $result): void {}

    /** Fails the operation with `$throwable`; an AsyncException when it is already completed. */
    public function error(\Throwable $throwable): void {}

    public function isCompleted(): bool {}

    /** No warning at destruction for an outcome nobody used. */
    public function ignore(): void {}

    /** One line: "FutureState(completed)" or "FutureState(pending)". */
    public function getAwaitingInfo(): array {}

    /** @return array{0: ?string, 1: int} */
    public function getCreatedFileAndLine(): array {}

    /** "file:line", or "unknown". */
    public function getCreatedLocation(): string {}

    /** @return array{0: ?string, 1: int} [null, 0] before the completion. */
    public function getCompletedFileAndLine(): array {}

    /** "file:line", or "unknown" before the completion. */
    public function getCompletedLocation(): string {}
}

/** The reading end of a FutureState, or the result of a map(), catch() or finally(). */
final class Future implements Completable
{
    public static function completed(mixed $value = null): Future {}

    public static function failed(\Throwable $throwable): Future {}

    public function __construct(FutureState $state) {}

    public function isCompleted(): bool {}

    /** Completed with an AsyncCancellation. */
    public function isCancelled(): bool {}

    /** Fails a pending future with `$cancellation`, or AsyncCancellation("Future has been cancelled"). */
    public function cancel(?AsyncCancellation $cancellation = null): void {}

    /** No warning at destruction for an outcome nobody used; returns this future. */
    public function ignore(): Future {}

    /** A future of `$map($result)`; this future's error passes on without the call. */
    public function map(callable $map): Future {}

    /** A future of `$catch($error)`; this future's result passes on without the call. */
    public function catch(callable $catch): Future {}

    /** A future of this future's outcome once `$finally` returned; what `$finally` throws replaces it. */
    public function finally(callable $finally): Future {}

    /**
     * The result, or the error thrown; OperationCanceledException once `$cancellation` completes
     * first.
     */
    public function await(?Completable $cancellation = null): mixed {}

    /** One line: "FutureState(completed)" or "FutureState(pending)". */
    public function getAwaitingInfo(): array {}

    /** @return array{0: ?string, 1: int} */
    public function getCreatedFileAndLine(): array {}

    /** "file:line", or "unknown". */
    public function getCreatedLocation(): string {}

    /** @return array{0: ?string, 1: int} [null, 0] before the completion. */
    public function getCompletedFileAndLine(): array {}

    /** "file:line", or "unknown" before the completion. */
    public function getCompletedLocation(): string {}
}
