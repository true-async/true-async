--TEST--
Channel: a soft timer closes the channel with NO_PRODUCERS while another coroutine sleeps
--FILE--
<?php

use Async\Channel;
use Async\ChannelException;
use function Async\await;
use function Async\delay;
use function Async\spawn;

$receiver = spawn(function () {
    $channel = new Channel(0, 100, 100, false);

    try {
        $channel->recv();
    } catch (ChannelException $exception) {
        echo "receiver: ", $exception->reason->name, "\n";
    }
});
// Armed after the channel's timer and longer: the order of the two lines shows the timer fired first.
spawn(function () {
    delay(300);
    echo "sleeper: done\n";
});
await($receiver);
?>
--EXPECT--
receiver: NO_PRODUCERS
sleeper: done
