--TEST--
get_deadlocked_coroutines(): a coroutine whose body is a method of an object only its callable and the call keep, parked on a dead Future, is found
--FILE--
<?php
use Async\Future;
use Async\FutureState;
use function Async\spawn;
use function Async\suspend;
use function Async\get_deadlocked_coroutines;

final class Job
{
    public function run(): void
    {
        (new Future(new FutureState()))->await();
    }
}

function start(): void
{
    spawn([new Job(), 'run']);
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
