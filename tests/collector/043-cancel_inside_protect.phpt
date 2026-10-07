--TEST--
The automatic run with cancel: a stuck coroutine parked inside protect() is cancelled at once, protection cleared, as the global deadlock cancels its waiters
--INI--
true_async.partial_deadlock=cancel
true_async.partial_deadlock_interval=0
error_reporting=E_ALL & ~E_WARNING
--FILE--
<?php
use Async\Future;
use Async\FutureState;
use function Async\spawn;
use function Async\protect;
use function Async\delay;

function start(): void
{
    $future = new Future(new FutureState());
    spawn(function () use ($future) {
        try {
            protect(function () use ($future) {
                $future->await();
            });
        } catch (Async\AsyncCancellation $e) {
            echo "cancelled inside protect(): ", $e->getMessage(), "\n";
        }
    });
}

start();
delay(10);
echo "end\n";
?>
--EXPECT--
cancelled inside protect(): Deadlock detected
end
