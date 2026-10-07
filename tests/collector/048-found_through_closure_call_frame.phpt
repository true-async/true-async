--TEST--
get_deadlocked_coroutines(): two coroutines parked inside closures called on their stacks, each closure the only holder of the other coroutine, are found
--FILE--
<?php
use function Async\spawn;
use function Async\await;
use function Async\suspend;
use function Async\get_deadlocked_coroutines;

function start_pair(): void
{
    $a = null;
    $b = null;
    $a = spawn(function () use (&$b) {
        $wait = function () use (&$b) {
            suspend();
            await($b);
        };
        $wait();
    });
    $b = spawn(function () use (&$a) {
        (function () use (&$a) {
            suspend();
            await($a);
        })();
    });
}

start_pair();

for ($i = 0; $i < 4; $i++) {
    suspend();
}

$found = get_deadlocked_coroutines();
echo count($found), " found\n";

foreach ($found as $coroutine) {
    $coroutine->cancel();
}
?>
--EXPECT--
2 found
