--TEST--
Channel: a channel stays open after its owner scope's last coroutine ends
--FILE--
<?php

use Async\Channel;
use Async\ChannelException;
use Async\Scope;
use function Async\await;

$scope = new Scope();
$channel = await($scope->spawn(fn() => new Channel(1)));

echo "closed after the scope's last coroutine ended: ", var_export($channel->isClosed(), true), "\n";

try {
    $channel->send(1);
    echo "sent\n";
} catch (ChannelException $exception) {
    echo "send: ", $exception->reason->name, "\n";
}

echo "received: ", $channel->recv(), "\n";
?>
--EXPECT--
closed after the scope's last coroutine ended: false
sent
received: 1
