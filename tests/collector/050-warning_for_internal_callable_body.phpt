--TEST--
The automatic run: a coroutine whose body is an internal method of an object only its callable and the call keep is reported
--INI--
true_async.partial_deadlock_interval=0
--FILE--
<?php
use Async\Future;
use Async\FutureState;
use function Async\spawn;
use function Async\delay;

function start(): void
{
    spawn([new Future(new FutureState()), 'await']);
}

start();
wait_for_idle_run();
echo "end\n";
// A delay can end before the scheduler goes idle: a timer due when main parks wakes it in place
function wait_for_idle_run(): void
{
    $runs = TrueAsync\Test\collector_idle_runs();

    while (TrueAsync\Test\collector_idle_runs() === $runs) {
        delay(1);
    }
}
?>
--EXPECTF--
Warning: Partial deadlock: coroutine #%d spawned at %s:%d can never wake (await: future) in %s on line %d
end
%A
