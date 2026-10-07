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
delay(10);
delay(10);
echo "end\n";
?>
--EXPECTF--
Warning: Partial deadlock: coroutine #%d spawned at %s:%d can never wake (await: future) in %s on line %d
end
%A
