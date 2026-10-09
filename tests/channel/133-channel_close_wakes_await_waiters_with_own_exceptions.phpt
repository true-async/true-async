--TEST--
Channel: close() wakes each coroutine parked in await_* on the channel with a ChannelException of its own
--FILE--
<?php

use Async\Channel;
use function Async\await;
use function Async\await_any_or_fail;
use function Async\spawn;
use function Async\suspend;

$channel = new Channel(1);
$parked = 0;
$waiter = function () use ($channel, &$parked) {
    $parked++;

    try {
        await_any_or_fail([$channel]);
    } catch (Async\ChannelException $exception) {
        return $exception;
    }
};
$first = spawn($waiter);
$second = spawn($waiter);

while ($parked < 2) {
    suspend();
}

$channel->close();
echo "same object: ", var_export(await($first) === await($second), true), "\n";
?>
--EXPECT--
same object: false
