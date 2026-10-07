<?php

/** @generate-class-entries */

namespace Async;

/** Names the scope spawn_with() puts a coroutine in. */
interface ScopeProvider
{
    /** The scope for the next coroutine; null means the current coroutine's scope. */
    public function provideScope(): ?Scope;
}

/** A ScopeProvider that also sees each coroutine spawn_with() puts in its scope. */
interface SpawnStrategy extends ScopeProvider
{
    /**
     * Called after the coroutine joined `$scope`, before it is queued; what it returns is not read.
     * An exception thrown here cancels the spawn.
     */
    public function beforeCoroutineEnqueue(Coroutine $coroutine, Scope $scope): array;

    /** Called once the coroutine is queued; an exception thrown here cancels the coroutine. */
    public function afterCoroutineEnqueue(Coroutine $coroutine, Scope $scope): void;
}

/**
 * A group of coroutines and child scopes, cancelled together. A scope outlives its object until its
 * last coroutine finishes.
 *
 * @strict-properties
 * @not-serializable
 */
final class Scope implements ScopeProvider
{
    /**
     * A child of `$parentScope`, or of the current coroutine's scope when it is null (at the top
     * level, of the global scope). It inherits the parent's safe disposal.
     */
    public static function inherit(?Scope $parentScope = null): Scope {}

    #[\Override] public function provideScope(): Scope {}

    /** A root scope: no parent stands above it, and its disposal is not safe until allowZombies(). */
    public function __construct() {}

    /** Coroutines still running when the scope is cancelled are cancelled. */
    public function asNotSafely(): Scope {}

    /** Coroutines still running when the scope is cancelled become zombies and run to their end. */
    public function allowZombies(): Scope {}

    /** Starts `$callable` in a new coroutine of this scope; refused once the scope is closed. */
    public function spawn(\Closure $callable, mixed ...$params): Coroutine {}

    /**
     * Cancels the child scopes and the coroutines with `$cancellationError`, or with
     * AsyncCancellation("Scope was cancelled") when it is null; with safe disposal a started
     * coroutine becomes a zombie instead. A scope with nothing left to cancel is closed.
     */
    public function cancel(?AsyncCancellation $cancellationError = null): void {}

    /** True once the scope is cancelled, closed or gone, or no coroutine of it or of its children runs. */
    public function isFinished(): bool {}

    /** True once no coroutine can be spawned in the scope any more. */
    public function isClosed(): bool {}

    public function isCancelled(): bool {}

    /**
     * Called as fn(Scope $scope, Coroutine $coroutine, \Throwable $error) for an error of a coroutine of
     * this scope that nobody awaited, before the scope is cancelled for it; returning without throwing
     * keeps the scope running. What it throws goes on up instead, with the error as its previous. It
     * runs as the coroutine finishes, where suspend(), await() and delay() throw.
     */
    public function setExceptionHandler(callable $exceptionHandler): void {}

    /** The same for an error coming up from a child scope; it is called instead of setExceptionHandler()'s. */
    public function setChildScopeExceptionHandler(callable $exceptionHandler): void {}

    /** @return Scope[] the child scopes that still have their object */
    public function getChildScopes(): array {}
}
