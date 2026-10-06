--TEST--
await() with a Coroutine token that fails during the wait: OperationCanceledException with the token's exception as previous, and the target still runs
--FILE--
<?php

use Async\OperationCanceledException;
use function Async\spawn;
use function Async\await;
use function Async\suspend;

$token = spawn(function () {
    suspend();
    throw new RuntimeException("token failed");
});

$worker = spawn(function () {
    suspend();
    suspend();
    suspend();
    return "late";
});

try {
    await($worker, $token);
    echo "not reached\n";
} catch (OperationCanceledException $e) {
    echo get_class($e), ": ", $e->getMessage(), "\n";
    echo get_class($e->getPrevious()), ": ", $e->getPrevious()->getMessage(), "\n";
}

var_dump(await($worker));

?>
--EXPECT--
Async\OperationCanceledException: Operation has been cancelled
RuntimeException: token failed
string(4) "late"
