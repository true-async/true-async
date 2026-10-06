--TEST--
The wait for the rest after await_first_success() waits for coroutines only: a Future that never completes does not hold it
--FILE--
<?php

use Async\FutureState;
use Async\Future;
use function Async\spawn;
use function Async\await_first_success;
use function Async\suspend;

$never = new FutureState();
$never->ignore();

$coroutine = spawn(function () {
    suspend();
    return "coroutine";
});

$slow = spawn(function () {
    suspend();
    suspend();
    return "slow";
});

[$result, $errors] = await_first_success([new Future($never), $coroutine, $slow]);

echo "result: $result\n";
var_dump($slow->isCompleted(), count($errors));

?>
--EXPECT--
result: coroutine
bool(true)
int(0)
