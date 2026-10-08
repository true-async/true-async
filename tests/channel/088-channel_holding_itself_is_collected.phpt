--TEST--
Channel: a channel whose buffer or rendezvous slot holds the channel is collected by gc_collect_cycles()
--FILE--
<?php

use Async\Channel;

function collected(array $references): int
{
    return count(array_filter($references, fn(WeakReference $reference) => $reference->get() === null));
}

$references = [];

for ($i = 0; $i < 100; $i++) {
    $channel = new Channel(2);
    $channel->sendAsync($channel);
    $channel->sendAsync(str_repeat('x', 1000));
    $references[] = WeakReference::create($channel);
}

unset($channel);
gc_collect_cycles();
echo "buffer collected: ", collected($references), "\n";

$references = [];

for ($i = 0; $i < 100; $i++) {
    $channel = new Channel(0);
    $channel->sendAsync($channel);
    $references[] = WeakReference::create($channel);
}

unset($channel);
gc_collect_cycles();
echo "slot collected: ", collected($references), "\n";
?>
--EXPECT--
buffer collected: 100
slot collected: 100
