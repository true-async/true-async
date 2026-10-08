--TEST--
Channel: a woken receiver cancelled before it runs hands its value on to a pending recvAsync() Future
--FILE--
<?php

use Async\Channel;
use function Async\await;
use function Async\spawn;
use function Async\suspend;

$channel = new Channel(1);
$receiver = spawn(function () use ($channel) {
    try {
        $channel->recv();
        echo "receiver got a value\n";
    } catch (Async\AsyncCancellation) {
        echo "receiver cancelled\n";
    }
});
suspend();

$future = $channel->recvAsync();
$channel->sendAsync('value');
echo "future completed before the cancel: ", var_export($future->isCompleted(), true), "\n";

$receiver->cancel();
await($receiver);
echo "future: ", await($future), "\n";
echo "count: ", count($channel), "\n";
?>
--EXPECT--
future completed before the cancel: false
receiver cancelled
future: value
count: 0
