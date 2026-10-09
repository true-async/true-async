--TEST--
Channel: cancel() of a completed parent closes a channel made in a child scope the script still holds
--FILE--
<?php

use Async\Channel;
use Async\Scope;
use function Async\await;

$parent = new Scope();
$child = Scope::inherit($parent);
$channel = await($child->spawn(fn() => new Channel(1)));

$parent->cancel();

try {
    $channel->recv();
} catch (Async\ChannelException $exception) {
    echo "recv: ", $exception->reason->name, "\n";
}
?>
--EXPECT--
recv: SCOPE_DISPOSED
