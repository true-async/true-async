--TEST--
A user class cannot implement Async\Completable: generic wait code would read it as an event
--FILE--
<?php

final class Fake implements Async\Completable
{
    public function cancel(?Async\AsyncCancellation $cancellation = null): void {}
    public function isCompleted(): bool { return true; }
    public function isCancelled(): bool { return false; }
}

echo "unreachable\n";
?>
--EXPECTF--
Fatal error: Class Fake cannot implement interface Async\Awaitable: only the classes of true_async implement it in %s on line %d
