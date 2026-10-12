<?php

/** @generate-class-entries */

namespace Async;

/** Thrown by a closed pool, and by a release() of a value the pool has not handed out. */
class PoolException extends AsyncException {}

/**
 * A set of reusable resources (connections, clients, any value) that coroutines take with acquire() and
 * give back with release(): at most `$max` resources exist, `$min` of them made in advance.
 *
 * A hook (`$beforeAcquire`, `$beforeRelease`, `$healthcheck`) rejects a resource by returning false or by
 * throwing an Exception, and the resource is destroyed; an Error thrown by a hook, a cancellation included,
 * destroys the resource too and passes to the caller.
 *
 * @strict-properties
 * @not-serializable
 */
final class Pool implements \Countable, CircuitBreaker
{
    /**
     * @param callable $factory Makes a resource: fn(): mixed. Its exception passes to the caller.
     * @param callable|null $destructor Called with each resource the pool destroys: fn(mixed $resource): void.
     * @param callable|null $healthcheck Checks an idle resource every `$healthcheckInterval` ms:
     *   fn(mixed $resource): bool.
     * @param callable|null $beforeAcquire Checks an idle resource before acquire() hands it out:
     *   fn(mixed $resource): bool. A rejected one is destroyed and the next is taken.
     * @param callable|null $beforeRelease Checks a resource given back: fn(mixed $resource): bool. A rejected
     *   one is destroyed instead of kept.
     * @param int $min Resources made by the constructor.
     * @param int $max The most resources at once, idle and handed out.
     * @param int $healthcheckInterval Milliseconds between healthchecks; 0: none.
     */
    public function __construct(
        callable $factory,
        ?callable $destructor = null,
        ?callable $healthcheck = null,
        ?callable $beforeAcquire = null,
        ?callable $beforeRelease = null,
        int $min = 0,
        int $max = 10,
        int $healthcheckInterval = 0,
    ) {}

    /**
     * An idle resource, or a new one while fewer than `$max` exist.
     *
     * @param int $timeout Milliseconds to wait for a resource; 0: no limit.
     * @throws PoolException If the pool is closed.
     * @throws ServiceUnavailableException If the circuit breaker is INACTIVE.
     * @throws TimeoutException If no resource came within `$timeout`.
     */
    public function acquire(int $timeout = 0): mixed {}

    /**
     * acquire() without the wait: null when the pool is closed, INACTIVE or has no resource to give. A
     * factory that returns null makes the two nulls alike.
     */
    public function tryAcquire(): mixed {}

    /**
     * Gives back a resource acquire() or tryAcquire() returned: kept idle, or destroyed when
     * `$beforeRelease` rejects it or the pool is closed.
     *
     * @throws PoolException If the pool has not handed `$resource` out, or has got it back already.
     */
    public function release(mixed $resource): void {}

    /** Destroys the idle resources; the ones handed out are destroyed at their release(). A second call does nothing. */
    public function close(): void {}

    public function isClosed(): bool {}

    /** Resources idle and handed out, those inside a hook included. */
    public function count(): int {}

    public function idleCount(): int {}

    /** Resources handed out or inside a hook. */
    public function activeCount(): int {}

    /**
     * The strategy gets reportSuccess() at each release the pool keeps and reportFailure() at each one it
     * rejects; while INACTIVE, acquire() and tryAcquire() ask its shouldRecover(). Its exception passes to the
     * caller.
     */
    public function setCircuitBreakerStrategy(?CircuitBreakerStrategy $strategy): void {}

    public function getState(): CircuitBreakerState {}

    public function activate(): void {}

    /** acquire() throws ServiceUnavailableException and tryAcquire() returns null until activate() or recover(). */
    public function deactivate(): void {}

    /** Serves as ACTIVE does; the strategy moves the pool on. */
    public function recover(): void {}
}
