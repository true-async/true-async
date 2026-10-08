--TEST--
Channel: on a rendezvous channel a pending recvAsync() Future takes a send()'s value at once, and close() keeps nothing
--FILE--
<?php

use Async\Channel;
use function Async\await;

$channel = new Channel(0);
$first = $channel->recvAsync();
$second = $channel->recvAsync();

$channel->send('one');
echo "send() returned, count: ", count($channel), "\n";
echo "sendAsync(): ", var_export($channel->sendAsync('two'), true), ", count: ", count($channel), "\n";
echo "sendAsync() with no receiver: ", var_export($channel->sendAsync('three'), true), ", count: ", count($channel), "\n";

$channel->close();
echo await($first), " ", await($second), "\n";

try {
    $channel->recv();
} catch (Async\ChannelException $exception) {
    echo "recv(): ", $exception->getMessage(), "\n";
}
?>
--EXPECT--
send() returned, count: 0
sendAsync(): true, count: 0
sendAsync() with no receiver: true, count: 1
one two
recv(): Channel is closed
