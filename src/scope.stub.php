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

    /**
     * Waits until no coroutine of the scope or of its child scopes runs, zombies aside; returns at once
     * for a closed or gone scope. Throws AsyncCancellation for a cancelled scope, and the error a
     * coroutine's unhandled exception or cancel() brings while it waits, unless safe disposal has
     * made the coroutines zombies first; OperationCanceledException when `$cancellation` completes
     * first. Refused from a coroutine of the scope or of its children.
     */
    public function awaitCompletion(Completable $cancellation): void {}

    /**
     * Waits on a cancelled scope until no coroutine of it or of its child scopes is left, zombies
     * included; returns at once for a gone scope and, as TrueAsync's, for a closed one that is not
     * cancelled (a cancel closes an idle scope without cancelling it). A coroutine's unhandled
     * exception that no scope's exception handler took on its way up comes here while the coroutine
     * waits: it goes to `$errorHandler` as fn(\Throwable $error, Scope $scope), which runs in the
     * waiting coroutine, and the wait goes on; without a handler it is thrown. One that comes while
     * the handler runs goes on up as if nobody waited; one that came before the waiting coroutine was
     * cancelled is in the chain of previous of the cancellation thrown, and so is the error an
     * AsyncCancellation that a scope's handler threw in its place, which is thrown too.
     * OperationCanceledException when `$cancellation` completes first. Refused for a scope that is neither cancelled nor closed, and,
     * unless it returns at once, from a coroutine of the scope or of its children.
     */
    public function awaitAfterCancellation(?callable $errorHandler = null, ?Completable $cancellation = null): void {}

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

    /**
     * cancel() with no error, as TrueAsync's: it cancels a scope that has coroutines or child scopes,
     * which refuses a spawn once it is closed or gone, and closes one that has nothing left to cancel.
     */
    public function dispose(): void {}

    /** dispose() with safe disposal whatever the scope's own: started coroutines become zombies. */
    public function disposeSafely(): void {}

    /**
     * Cancels the scope as dispose() does with AsyncCancellation("Scope has been disposed due to
     * timeout") once `$timeout` ms have passed, unless the scope is gone by then; the earliest of
     * several calls wins. Nothing for a closed scope, one with no coroutine and no child scope, or once
     * async is off (an output handler, the request's teardown).
     */
    public function disposeAfterTimeout(int $timeout): void {}

    /**
     * Called as fn(?Scope $scope) in a new coroutine of a child scope once this scope is disposed or
     * closed with nothing left to run, with the other finally callbacks of this scope; `$scope` is null
     * when the object is gone. At once when the scope is gone. What it throws goes up from that child
     * scope.
     */
    public function finally(\Closure $callback): void {}

    /** @return Scope[] the child scopes that still have their object */
    public function getChildScopes(): array {}
}
