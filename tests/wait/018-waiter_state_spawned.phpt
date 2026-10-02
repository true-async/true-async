--TEST--
S3.7 item 11: a spawned coroutine parked in await() is suspended, running and not completed; after await() returns it is running
--FILE--
<?php

use function Async\spawn;
use function Async\await;
use function Async\current_coroutine;

$target = null;

$waiter = spawn(function() use (&$target) {
    await($target);
    echo "waiter after await:\n";
    var_dump(current_coroutine()->isRunning());
});

$target = spawn(function() use ($waiter) {
    echo "waiter while parked:\n";
    var_dump($waiter->isSuspended());
    var_dump($waiter->isRunning());
    var_dump($waiter->isCompleted());
});

// Queue [waiter, target]: the waiter parks on target, target inspects it, then the waiter continues.
await($waiter);
echo "end\n";
?>
--EXPECT--
waiter while parked:
bool(true)
bool(true)
bool(false)
waiter after await:
bool(true)
end
