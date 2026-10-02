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
