--TEST--
A Timer op is on the reactor's waits list while its waiter waits, and leaves it when it fires and when the waiter is cancelled, whose op is withdrawn from the queue (the Poll queue, which frees a withdrawn op at once)
--FILE--
<?php
use function Async\spawn;
use function Async\await;
use TrueAsync\Test;

Test\reactor_use_poll_queue();

function state(): string
{
    $state = Test\reactor_state();

    return "waits {$state['waits']}, pending {$state['pending']}";
}

$fired = spawn(fn() => Test\reactor_wait(10));
$cancelled = spawn(function () {
    try {
        Test\reactor_wait(60000);
    } catch (Async\AsyncCancellation $e) {
        echo "cancelled\n";
    }
});

Async\suspend();
echo "while both wait: ", state(), "\n";
await($fired);
echo "after the fire: ", state(), "\n";
$cancelled->cancel();
await($cancelled);
echo "after the cancel: ", state(), "\n";
?>
--EXPECT--
while both wait: waits 2, pending 2
after the fire: waits 1, pending 1
cancelled
after the cancel: waits 0, pending 0
