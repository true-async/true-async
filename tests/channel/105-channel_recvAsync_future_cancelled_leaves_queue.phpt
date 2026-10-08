--TEST--
Channel: a recvAsync() Future cancelled while pending leaves the queue, and the value goes to the next receiver
--FILE--
<?php

use Async\Channel;
use function Async\await;
use function Async\spawn;
use function Async\suspend;

$channel = new Channel(0);
$future = $channel->recvAsync();
$future->ignore();
$future->cancel();
echo "cancelled: ", var_export($future->isCancelled(), true), "\n";

$receiver = spawn(fn() => $channel->recv());
suspend();
$channel->send('value');
echo "receiver: ", await($receiver), "\n";
?>
--EXPECT--
cancelled: true
receiver: value
