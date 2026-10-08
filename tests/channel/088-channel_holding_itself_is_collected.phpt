--TEST--
Channel: a channel whose buffer or rendezvous slot holds the channel is collected by gc_collect_cycles()
--FILE--
<?php

use Async\Channel;

for ($i = 0; $i < 100; $i++) {
    $channel = new Channel(2);
    $channel->sendAsync($channel);
    $channel->sendAsync(str_repeat('x', 1000));
}

unset($channel);
echo "buffer collected: ", gc_collect_cycles(), "\n";

for ($i = 0; $i < 100; $i++) {
    $channel = new Channel(0);
    $channel->sendAsync($channel);
}

unset($channel);
echo "slot collected: ", gc_collect_cycles(), "\n";
?>
--EXPECT--
buffer collected: 100
slot collected: 100
