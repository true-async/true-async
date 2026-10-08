--TEST--
Channel: a soft timer closes the channel with NO_PRODUCERS while another coroutine sleeps
--FILE--
<?php

use Async\Channel;
use Async\ChannelException;
use function Async\await;
use function Async\delay;
use function Async\spawn;

$start = hrtime(true);
$receiver = spawn(function () use ($start) {
    $channel = new Channel(0, 100, 100, false);

    try {
        $channel->recv();
    } catch (ChannelException $exception) {
        $elapsed = (hrtime(true) - $start) / 1e6;
        echo "receiver: ", $exception->reason->name, ", before the sleeper: ", var_export($elapsed < 250, true), "\n";
    }
});
spawn(function () {
    delay(300);
    echo "sleeper: done\n";
});
await($receiver);
?>
--EXPECT--
receiver: NO_PRODUCERS, before the sleeper: true
sleeper: done
