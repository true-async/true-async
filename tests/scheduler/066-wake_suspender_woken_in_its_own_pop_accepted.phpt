--TEST--
A coroutine woken in its own pop and woken again with an error by a destructor of a later coroutine that pop finishes unrun: the second wake is taken, and its suspend() throws the error with the first under it
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

class WakesOnRelease
{
    public function __construct(private Closure $cancel)
    {
    }

    public function __destruct()
    {
        try {
            ($this->cancel)();
            echo "woke the waiter\n";
        } catch (Throwable $e) {
            echo get_class($e), ": ", $e->getMessage(), "\n";
        }
    }
}

$target = null;
$waiter = null;

/* Parks in await(); its pop finishes the two cancelled coroutines queued next: the release of the
 * first starts the graceful shutdown, which wakes the waiter in its own pop, and the release of the
 * second wakes it again. */
$waiter = spawn(function () use (&$target) {
    try {
        await($target);
    } catch (Throwable $e) {
        echo "waiter: ", get_class($e), ": ", $e->getMessage(), ", previous: ", $e->getPrevious()?->getMessage(), "\n";
    }
});

spawn(function ($argument) {}, new ThrowsOnRelease())->cancel();
spawn(function ($argument) {}, new WakesOnRelease(function () use (&$waiter) {
    TrueAsync\Test\enqueue_with_error($waiter, new Exception("from the second release"), true);
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
woke the waiter
waiter: Exception: from the second release, previous: Graceful shutdown

Fatal error: Uncaught Exception: from destructor in %s:%d
Stack trace:
#0 [internal function]: ThrowsOnRelease->__destruct()
#1 %s(%d): Async\await(Object(Async\Coroutine))
#2 [internal function]: {closure:%s:%d}()
#3 {main}
  thrown in %s on line %d
