--TEST--
iterate(): the collector finds the caller of a walk whose callback can never wake
--INI--
true_async.partial_deadlock=cancel
true_async.partial_deadlock_interval=0
--FILE--
<?php

use Async\Channel;
use function Async\delay;
use function Async\iterate;
use function Async\spawn;

$main = Async\current_coroutine();

spawn(function () use ($main) {
    try {
        iterate([1], function () {
            try {
                (new Channel())->recv();
            } catch (Async\AsyncCancellation $cancellation) {
                echo "callback: ", $cancellation->getMessage(), "\n";
                throw $cancellation;
            }
        });
    } catch (Async\AsyncCancellation $cancellation) {
        echo "caller: ", $cancellation->getMessage(), "\n";
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

Warning: Partial deadlock: coroutine #3 spawned at %s:10 can never wake (iterate() at %s:12) in %s on line 12

Warning: Partial deadlock: coroutine #6 can never wake (Channel(capacity=0, receivers=1, senders=0, reserved receivers=0, reserved senders=0)) in %s on line 14
callback: Deadlock detected
caller: Deadlock detected
end
