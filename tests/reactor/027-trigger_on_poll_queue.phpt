--TEST--
The trigger's wakeup on the Poll queue: a fire from another thread wakes the parked waiter, and the wakeup polls again for the next fire
--FILE--
<?php
use function Async\{spawn, await};
use TrueAsync\Test;

Test\reactor_use_poll_queue();
Test\trigger_new();

for ($i = 1; $i <= 3; $i++) {
    await(spawn(function () use ($i) {
        Test\trigger_fire(30);
        Test\trigger_wait();
        echo "woken $i\n";
    }));
}

echo "pending: ", Test\reactor_state()['pending'], "\n";
?>
--EXPECT--
woken 1
woken 2
woken 3
pending: 1
