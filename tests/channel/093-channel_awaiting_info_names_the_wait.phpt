--TEST--
Channel: getAwaitingInfo() of a coroutine parked in recv() or send() names the channel
--FILE--
<?php

use Async\Channel;
use Async\ChannelException;
use function Async\spawn;
use function Async\suspend;

$empty = new Channel(0);
$receiver = spawn(function () use ($empty) {
    try {
        $empty->recv();
    } catch (ChannelException $exception) {
        echo "receiver: ", $exception->getMessage(), "\n";
    }
});

$full = new Channel(1);
$full->sendAsync(1);
$sender = spawn(function () use ($full) {
    try {
        $full->send(2);
    } catch (ChannelException $exception) {
        echo "sender: ", $exception->getMessage(), "\n";
    }
});

suspend();
var_dump($receiver->getAwaitingInfo(), $sender->getAwaitingInfo());

$empty->close();
$full->close();
suspend();
?>
--EXPECT--
array(1) {
  [0]=>
  string(85) "Channel(capacity=0, receivers=1, senders=0, reserved receivers=0, reserved senders=0)"
}
array(1) {
  [0]=>
  string(85) "Channel(capacity=1, receivers=0, senders=1, reserved receivers=0, reserved senders=0)"
}
receiver: Channel is closed
sender: Channel is closed
