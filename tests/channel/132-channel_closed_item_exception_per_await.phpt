--TEST--
Channel: each await_* of an already closed channel throws a ChannelException of its own
--FILE--
<?php

use Async\Channel;
use function Async\await_any_or_fail;

$channel = new Channel(1);
$channel->close();

function closed_item_exception(Channel $channel): Async\ChannelException
{
    try {
        await_any_or_fail([$channel]);
    } catch (Async\ChannelException $exception) {
        return $exception;
    }
}

$first = closed_item_exception($channel);
$second = closed_item_exception($channel);
echo $first->reason->name, " ", $second->reason->name, "\n";
echo "same object: ", var_export($first === $second, true), "\n";
?>
--EXPECT--
EXPLICIT EXPLICIT
same object: false
