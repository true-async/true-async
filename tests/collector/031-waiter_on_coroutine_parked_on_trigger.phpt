--TEST--
get_deadlocked_coroutines(): a coroutine parked on a cross-thread trigger, and one awaiting it, are not reported
--FILE--
<?php
use function Async\spawn;
use function Async\await;
use function Async\suspend;
use function Async\delay;
use function Async\get_deadlocked_coroutines;
use TrueAsync\Test;

Test\trigger_new();

function start(): void
{
    $on_trigger = spawn(function () {
        Test\trigger_wait();
        echo "trigger fired\n";
    });
    spawn(function () use ($on_trigger) {
        await($on_trigger);
        echo "waiter woke\n";
    });
}

start();

for ($i = 0; $i < 4; $i++) {
    suspend();
}

echo count(get_deadlocked_coroutines()), " found\n";
Test\trigger_fire();
delay(20);

Test\trigger_free();
echo "end\n";
?>
--EXPECT--
0 found
trigger fired
waiter woke
end
