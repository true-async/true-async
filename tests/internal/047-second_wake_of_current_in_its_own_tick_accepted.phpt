--TEST--
A coroutine parked in await() runs its own tick in scheduler context: a microtask there cancels it and another wakes it with an error, which is accepted and chained over the cancellation
--FILE--
<?php
use function Async\spawn;
use function Async\await;
use function Async\suspend;

$waiter = null;
$target = null;

$waiter = spawn(function () use (&$waiter, &$target) {
    TrueAsync\Test\defer('a', null, function () use (&$waiter) {
        var_dump($waiter->cancel());
    });
    TrueAsync\Test\defer('b', null, function () use (&$waiter) {
        try {
            var_dump(TrueAsync\Test\enqueue_with_error($waiter, new Exception("second wake")));
        } catch (Error $e) {
            echo "b: ", get_class($e), ": ", $e->getMessage(), "\n";
        }
    });

    try {
        await($target);
        echo "waiter: await returned\n";
    } catch (Throwable $e) {
        echo "waiter: ", get_class($e), ": ", $e->getMessage(), ", previous: ", $e->getPrevious()?->getMessage(), "\n";
    }

    echo "waiter end\n";
});

$target = spawn(function () {
    suspend();
    suspend();
    echo "target end\n";
});

suspend();
suspend();
suspend();
echo "main end\n";
?>
--EXPECT--
microtask a sched=1
NULL
released a
microtask b sched=1
bool(true)
released b
waiter: Exception: second wake, previous: Coroutine cancelled
waiter end
target end
main end
