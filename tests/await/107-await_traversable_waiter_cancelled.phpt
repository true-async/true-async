--TEST--
A waiter cancelled during await_all() over a generator that suspends: the iterator coroutine is cancelled and the generator's finally runs
--FILE--
<?php

use function Async\spawn;
use function Async\await;
use function Async\await_all;
use function Async\suspend;

function triggers()
{
    try {
        for ($i = 0; $i < 100; $i++) {
            yield spawn(function () {
                suspend();
                suspend();
                suspend();
                return 1;
            });
            suspend();
        }
    } finally {
        echo "generator finally\n";
    }
}

$waiter = spawn(function () {
    try {
        await_all(triggers());
        echo "not reached\n";
    } catch (Async\AsyncCancellation $e) {
        echo "waiter: ", $e->getMessage(), "\n";
    }
});

suspend();
suspend();
$waiter->cancel();
await($waiter);
suspend();
suspend();
echo "end\n";

?>
--EXPECT--
waiter: Coroutine cancelled
generator finally
end
