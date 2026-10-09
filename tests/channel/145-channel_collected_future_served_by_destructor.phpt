--TEST--
Channel: a recvAsync() Future that only its map() child holds is collected safely when a destructor of the cycle sends on the channel, and the child gets the value
--FILE--
<?php

use Async\Channel;

final class SendsOnDestruct
{
    public function __construct(private Channel $channel)
    {
    }

    public function __destruct()
    {
        echo "destructor sends: ", var_export($this->channel->sendAsync('value'), true), "\n";
    }
}

$channel = new Channel(1);
$future = $channel->recvAsync();
$hook = new SendsOnDestruct($channel);
// The source holds the child in its chain, and the child holds the source and, through its mapper, the hook.
$future->map(function ($value) use ($hook) {
    echo "mapped: ", $value, "\n";
})->ignore();
unset($hook);
$future->ignore();
unset($future);

var_dump(gc_collect_cycles() > 0);
Async\suspend();
echo "count: ", count($channel), "\n";
?>
--EXPECT--
destructor sends: true
mapped: value
bool(true)
count: 0
