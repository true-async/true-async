--TEST--
Channel: a recvAsync() Future left starving when a reserved receiver's wait ends is closed by the hard timer, or with a soft one by the global deadlock
--FILE--
<?php

use Async\Channel;
use Async\ChannelException;
use function Async\await;
use function Async\spawn;

function check_future_after_receiver_exit(bool $hard): void
{
    $channel = new Channel(0, 100, 0, $hard);
    $parked = false;
    $receiver = spawn(function () use ($channel, &$parked) {
        $parked = true;

        return $channel->recv();
    });

    // A coroutine runs on to its park: a delay could end before the receiver parks
    while (!$parked) {
        Async\suspend();
    }

    // The value is promised to the receiver: no side starves and the timer goes.
    $channel->sendAsync(1);
    $future = $channel->recvAsync();
    echo "receiver: ", await($receiver), "\n";

    try {
        $future->await();
    } catch (ChannelException $exception) {
        echo $hard ? "hard" : "soft", " timer, the Future: ", $exception->reason->name, "\n";
    }
}

check_future_after_receiver_exit(false);
check_future_after_receiver_exit(true);
?>
--EXPECT--
receiver: 1
soft timer, the Future: DEADLOCK
receiver: 1
hard timer, the Future: NO_PRODUCERS
