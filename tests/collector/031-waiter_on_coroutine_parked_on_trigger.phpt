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
        $GLOBALS['waiter_woke'] = true;
    });
}

start();

for ($i = 0; $i < 4; $i++) {
    suspend();
}

echo count(get_deadlocked_coroutines()), " found\n";
Test\trigger_fire();

/* Not a fixed delay: on a loaded machine the trigger's completion and the delay's timer can come in
 * one poll, and main then runs ahead of the waiter. The bound turns a waiter that never wakes into a
 * wrong output rather than a timeout. */
for ($i = 0; $i < 1000 && !isset($GLOBALS['waiter_woke']); $i++) {
    delay(1);
}

Test\trigger_free();
echo "end\n";
?>
--EXPECT--
0 found
trigger fired
waiter woke
end
