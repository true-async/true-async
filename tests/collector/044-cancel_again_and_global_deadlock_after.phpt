--TEST--
The automatic run with cancel: a coroutine that catches the cancellation and parks on another dead target is cancelled again without a second warning, and a later global deadlock is resolved as before
--INI--
true_async.partial_deadlock=cancel
true_async.partial_deadlock_interval=0
--FILE--
<?php
use Async\Future;
use Async\FutureState;
use function Async\spawn;
use function Async\delay;

function start(): void
{
    spawn(function () {
        for ($round = 1; $round <= 2; $round++) {
            try {
                (new Future(new FutureState()))->await();
            } catch (Async\AsyncCancellation $e) {
                echo "round ", $round, ": ", $e->getMessage(), "\n";
            }
        }
    });
}

start();
delay(10);
delay(10);
echo "main parks on a dead Future\n";
(new Future(new FutureState()))->await();
?>
--EXPECTF--
Warning: Partial deadlock: coroutine #%d spawned at %s:%d can never wake (await: future) in %s on line %d
round 1: Deadlock detected
round 2: Deadlock detected
main parks on a dead Future
%A
Fatal error: Uncaught Async\DeadlockError: Deadlock detected: no active coroutines, 1 coroutines in waiting%A
