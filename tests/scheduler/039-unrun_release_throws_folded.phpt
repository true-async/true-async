--TEST--
A destructor that throws while a coroutine cancelled before it ran is finished at pop ends the request; the exception does not reach the coroutine popped next
--FILE--
<?php
use function Async\spawn;
use function Async\suspend;
use Async\AsyncCancellation;

class ThrowsOnRelease
{
    public function __destruct()
    {
        throw new Exception("from destructor");
    }
}

/* Started and parked, so the shutdown reaches it in its suspend(), not the destructor's exception. */
$other = spawn(function () {
    try {
        suspend();
        suspend();
    } catch (AsyncCancellation $e) {
        echo "other: ", get_class($e), ": ", $e->getMessage(), "\n";
    }
});
suspend();

/* Finished where the queue pops it; the release of its argument throws. */
spawn(function ($argument) {}, new ThrowsOnRelease())->cancel();
try {
    suspend();
} catch (AsyncCancellation $e) {
    echo "main: ", $e->getMessage(), "\n";
}

echo "main end\n";
?>
--EXPECTF--
main: Graceful shutdown
main end
other: Async\AsyncCancellation: Graceful shutdown

Fatal error: Uncaught Exception: from destructor in %s:%d
Stack trace:
#0 [internal function]: ThrowsOnRelease->__destruct()
#1 %s(%d): Async\suspend()
#2 [internal function]: {closure:%s:%d}()
#3 {main}
  thrown in %s on line %d
