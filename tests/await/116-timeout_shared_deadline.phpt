--TEST--
A Timeout's deadline is taken by timeout() and shared by every wait that uses it (D32 rule 3)
--FILE--
<?php

use Async\OperationCanceledException;
use function Async\await;
use function Async\delay;
use function Async\spawn;
use function Async\timeout;

$timeout = timeout(150);

echo "first: ", await(spawn(function () {
    delay(100);
    return "in time";
}), $timeout), "\n";

try {
    await(spawn(function () {
        delay(100);
        return "late";
    }), $timeout);
    echo "second: not cancelled\n";
} catch (OperationCanceledException $e) {
    echo "second: ", get_class($e->getPrevious()), ": ", $e->getPrevious()->getMessage(), "\n";
}

?>
--EXPECT--
first: in time
second: Async\TimeoutException: Timeout occurred after 150 milliseconds
