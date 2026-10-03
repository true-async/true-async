--TEST--
A coroutine parked in await() is woken in its own pop, which goes on to a started coroutine queued behind the one that woke it: that coroutine keeps its turn and runs after the woken one
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

/* Started here, it yields again when the queue drains and lands behind the cancelled coroutine. */
$yielded = spawn(function () {
    suspend();

    try {
        suspend();
    } catch (AsyncCancellation $e) {
        echo "yielded: ", $e->getMessage(), "\n";
    }
});
suspend();

$target = null;

/* Parks in await(); its pop finishes the cancelled coroutine queued next, whose release starts the
 * graceful shutdown, which cancels the waiter in its own pop; the pop then returns the yielded one. */
$waiter = spawn(function () use (&$target) {
    try {
        await($target);
    } catch (AsyncCancellation $e) {
        echo "waiter: ", $e->getMessage(), "\n";
    }
});

spawn(function ($argument) {}, new ThrowsOnRelease())->cancel();

$target = spawn(function () {
    echo "target ran\n";
});

echo "main end\n";
?>
--EXPECTF--
main end
waiter: Graceful shutdown
yielded: Graceful shutdown

Fatal error: Uncaught Exception: from destructor in %s:%d
Stack trace:
#0 [internal function]: ThrowsOnRelease->__destruct()
#1 %s(%d): Async\await(Object(Async\Coroutine))
#2 [internal function]: {closure:%s:%d}()
#3 {main}
  thrown in %s on line %d
