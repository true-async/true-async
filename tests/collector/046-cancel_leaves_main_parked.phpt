--TEST--
The automatic run with cancel: a stuck main is warned about and left parked, so the global deadlock ends the request with its DeadlockError once the rest of it ends
--INI--
true_async.partial_deadlock=cancel
true_async.partial_deadlock_interval=0
--FILE--
<?php
use Async\Future;
use Async\FutureState;
use function Async\spawn;
use function Async\delay;

spawn(function () {
    delay(50);
    echo "worker ends\n";
});

(new Future(new FutureState()))->await();
echo "main goes on\n";
?>
--EXPECTF--
Warning: Partial deadlock: coroutine #%d can never wake (await: future) in %s on line %d
worker ends
%A
Fatal error: Uncaught Async\DeadlockError: Deadlock detected: no active coroutines, 1 coroutines in waiting%A
