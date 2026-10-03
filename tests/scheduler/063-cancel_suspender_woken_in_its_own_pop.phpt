--TEST--
A coroutine woken in its own pop is cancelled by a destructor of a later coroutine that pop finishes unrun: the cancellation waits in its waker and is thrown as its suspend() returns, with no error from the wake
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

class CancelsOnRelease
{
    public function __construct(private Closure $cancel)
    {
    }

    public function __destruct()
    {
        try {
            ($this->cancel)();
            echo "cancelled the waiter\n";
        } catch (Throwable $e) {
            echo get_class($e), ": ", $e->getMessage(), "\n";
        }
    }
}

$target = null;
$waiter = null;

/* Parks in await(); its pop finishes the two cancelled coroutines queued next: the release of the
 * first starts the graceful shutdown, which wakes the waiter in its own pop, and the release of the
 * second cancels it again. */
$waiter = spawn(function () use (&$target) {
    try {
        await($target);
    } catch (AsyncCancellation $e) {
        echo "waiter: ", $e->getMessage(), "\n";
    }
});

spawn(function ($argument) {}, new ThrowsOnRelease())->cancel();
spawn(function ($argument) {}, new CancelsOnRelease(function () use (&$waiter) {
    $waiter->cancel(new AsyncCancellation("from the second release"));
}))->cancel();

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
cancelled the waiter
waiter: Graceful shutdown

Fatal error: Uncaught Exception: from destructor in %s:%d
Stack trace:
#0 [internal function]: ThrowsOnRelease->__destruct()
#1 %s(%d): Async\await(Object(Async\Coroutine))
#2 [internal function]: {closure:%s:%d}()
#3 {main}
  thrown in %s on line %d
