--TEST--
Pool: the collector finds a coroutine parked in acquire() on a pool only it holds
--INI--
true_async.partial_deadlock=cancel
true_async.partial_deadlock_interval=0
--FILE--
<?php

use Async\Pool;
use function Async\delay;
use function Async\spawn;

$main = Async\current_coroutine();

spawn(function () use ($main) {
    $pool = new Pool(factory: fn() => new stdClass(), max: 1);
    $held = $pool->acquire();
    try {
        $pool->acquire();
        echo "waiter: acquired\n";
    } catch (Async\AsyncCancellation $cancellation) {
        echo "waiter: ", $cancellation->getMessage(), "\n";
    }
    // From a coroutine of its own: a cancel made here would keep this frame's values in its trace.
    spawn(fn() => $main->cancel());
});

try {
    delay(100000);
} catch (Async\AsyncCancellation) {
}
echo "end\n";
?>
--EXPECTF--
Warning: Partial deadlock: coroutine #%d spawned at %s:%d can never wake (Pool(idle=0, active=1, reserved=0, max=1)) in %s on line %d
waiter: Deadlock detected
end
