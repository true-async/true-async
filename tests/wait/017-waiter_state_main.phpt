--TEST--
S3.7 item 11: main parked in await() is suspended, running and not completed; after await() returns it is running
--FILE--
<?php

use function Async\spawn;
use function Async\await;
use function Async\current_coroutine;

$main = current_coroutine();

$target = spawn(function() use ($main) {
    echo "main while parked:\n";
    var_dump($main->isSuspended());
    var_dump($main->isRunning());
    var_dump($main->isCompleted());
    return "target";
});

await($target);

echo "main after await:\n";
var_dump($main->isRunning());
?>
--EXPECT--
main while parked:
bool(true)
bool(true)
bool(false)
main after await:
bool(true)
