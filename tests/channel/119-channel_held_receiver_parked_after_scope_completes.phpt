--TEST--
Channel: a receiver on a channel that live code holds stays parked after the owner scope's coroutines end
--FILE--
<?php

use Async\Channel;
use Async\ChannelException;
use Async\Scope;
use function Async\await;
use function Async\delay;
use function Async\spawn;

final class Holder
{
    public ?Channel $channel = null;
}

$holder = new Holder();
$scope = new Scope();
await($scope->spawn(function () use ($holder) {
    $holder->channel = new Channel(0);
}));

$receiver = spawn(function () use ($holder) {
    try {
        $value = $holder->channel->recv();
        echo "received: ", $value, "\n";
    } catch (ChannelException $exception) {
        echo "receiver: ", $exception->reason->name, "\n";
    }
});
delay(50);
echo "after the scope's coroutines ended: closed=", var_export($holder->channel->isClosed(), true),
    ", receiver parked=", var_export(!$receiver->isCompleted(), true), "\n";

$holder->channel->send("late");
await($receiver);
?>
--EXPECT--
after the scope's coroutines ended: closed=false, receiver parked=true
received: late
