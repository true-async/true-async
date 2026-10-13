--TEST--
Channel: a parent's cancel of a completed owner scope closes the channel
--FILE--
<?php

use Async\Channel;
use Async\Scope;
use function Async\await;
use function Async\delay;

function channel_made_in(Scope $scope): Channel
{
    $channel = await($scope->spawn(fn() => new Channel(1)));
    echo "after completion: ", var_export($channel->isClosed(), true), "\n";

    return $channel;
}

echo "a parent's cancel\n";
$parent = new Scope();
$child = Scope::inherit($parent);
$channel = channel_made_in($child);
$parent->spawn(fn() => delay(1000));
$parent->cancel();
echo "after the parent's cancel: ", var_export($channel->isClosed(), true), "\n";

try {
    $channel->recv();
} catch (Async\ChannelException $exception) {
    echo "recv: ", $exception->reason->name, "\n";
}
?>
--EXPECT--
a parent's cancel
after completion: false
after the parent's cancel: true
recv: SCOPE_DISPOSED
