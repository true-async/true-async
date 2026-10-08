--TEST--
Channel: a sender promised a slot fails when the channel closes before it runs, and the slot goes to nobody
--FILE--
<?php

use Async\Channel;
use Async\ChannelException;
use function Async\spawn;
use function Async\suspend;
use function Async\await;

$channel = new Channel(1);
$channel->sendAsync(1);

$sender = spawn(function () use ($channel) {
    try {
        $channel->send(2);
        echo "sent\n";
    } catch (ChannelException $exception) {
        echo "sender: ", $exception->getMessage(), "\n";
    }
});
suspend();

// The receive frees the slot and promises it to the sender; the close comes before the sender runs.
echo "recv: ", $channel->recv(), "\n";
$channel->close();
// The sender has left the queue, but its reservation counts until its frame runs.
echo "sender waits for: ", $sender->getAwaitingInfo()[0], "\n";

await($sender);
echo "count: ", count($channel), ", sendAsync: ", var_export($channel->sendAsync(3), true), "\n";
?>
--EXPECT--
recv: 1
sender waits for: Channel(capacity=1, receivers=0, senders=0, reserved receivers=0, reserved senders=1)
sender: Channel is closed
count: 0, sendAsync: false
