--TEST--
await_all() with a token that has already completed throws before any trigger is linked
--FILE--
<?php

use Async\FutureState;
use Async\Future;
use Async\OperationCanceledException;
use function Async\spawn;
use function Async\await_all;
use function Async\await_any_or_fail;
use function Async\suspend;

$state = new FutureState();
$state->complete("done");
$token = new Future($state);

$coroutine = spawn(function () {
    suspend();
    return "result";
});

try {
    await_all([$coroutine], $token);
    echo "not reached\n";
} catch (OperationCanceledException $e) {
    echo $e->getMessage(), "\n";
    var_dump($e->getPrevious());
}

var_dump($coroutine->isCompleted());

$finished = spawn(function () {
    return "finished";
});

suspend();

try {
    await_any_or_fail([$coroutine], $finished);
    echo "not reached\n";
} catch (OperationCanceledException $e) {
    echo "finished coroutine token: ", $e->getMessage(), "\n";
}

?>
--EXPECT--
Operation has been cancelled
NULL
bool(false)
finished coroutine token: Operation has been cancelled
