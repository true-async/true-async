--TEST--
A coroutine parked in await() is woken in its own tick while its pop finishes a coroutine cancelled before it ran, whose release starts the graceful shutdown: it runs on, and the popped coroutine keeps its turn
--FILE--
<?php
use function Async\spawn;
use function Async\await;
use function Async\suspend;
use Async\AsyncCancellation;

class ThrowsOnRelease
{
    public function __destruct()
    {
        throw new Exception("from destructor");
    }
}

$target = null;

/* Parks in await(); its pop finishes the cancelled coroutine queued next, whose release starts the
 * graceful shutdown, which cancels the waiter in its own tick. */
$waiter = spawn(function () use (&$target) {
    try {
        await($target);
    } catch (AsyncCancellation $e) {
        echo "waiter: ", $e->getMessage(), "\n";
    }
});

spawn(function ($argument) {}, new ThrowsOnRelease())->cancel();

$target = spawn(function () {
    try {
        suspend();
    } catch (AsyncCancellation $e) {
        echo "target: ", $e->getMessage(), "\n";
    }
});

echo "main end\n";
?>
--EXPECTF--
main end
waiter: Graceful shutdown

Fatal error: Uncaught Exception: from destructor in %s:%d
Stack trace:
#0 [internal function]: ThrowsOnRelease->__destruct()
#1 %s(%d): Async\await(Object(Async\Coroutine))
#2 [internal function]: {closure:%s:%d}()
#3 {main}
  thrown in %s on line %d
