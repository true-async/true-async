--TEST--
get_deadlocked_coroutines(): a coroutine whose body is a closure of an internal method, the closure kept only by its callable and the call, parked on a dead Future, is found
--FILE--
<?php
use Async\Future;
use Async\FutureState;
use function Async\spawn;
use function Async\suspend;
use function Async\get_deadlocked_coroutines;

function start(): void
{
    spawn(Closure::fromCallable([new Future(new FutureState()), 'await']));
}

start();
suspend();
suspend();

$found = get_deadlocked_coroutines();
echo count($found), " found\n";

foreach ($found as $coroutine) {
    $coroutine->cancel();
}
?>
--EXPECT--
1 found
