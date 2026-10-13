--TEST--
A Timer op is on the reactor's waits list and in its timer heap while its waiter waits, and leaves both when it fires and when the waiter is cancelled; the queue never holds it
--FILE--
<?php
use function Async\spawn;
use function Async\await;
use TrueAsync\Test;

Test\reactor_use_poll_queue();

function state(): string
{
    $state = Test\reactor_state();

    return "waits {$state['waits']}, timers {$state['timers']}, pending {$state['pending']}";
}

// The fired waiter waits again until main has seen both waits: its timer may fire before main looks.
$seen = false;
$fired = spawn(function () use (&$seen) {
    do {
        Test\reactor_wait(10);
    } while (!$seen);
});
$cancelled = spawn(function () {
    try {
        Test\reactor_wait(60000);
    } catch (Async\AsyncCancellation $e) {
        echo "cancelled\n";
    }
});

while (Test\reactor_state()['waits'] < 2) {
    Async\suspend();
}

echo "while both wait: ", state(), "\n";
$seen = true;
await($fired);
echo "after the fire: ", state(), "\n";
$cancelled->cancel();
await($cancelled);
echo "after the cancel: ", state(), "\n";
?>
--EXPECT--
while both wait: waits 2, timers 2, pending 0
after the fire: waits 1, timers 1, pending 0
cancelled
after the cancel: waits 0, timers 0, pending 0
