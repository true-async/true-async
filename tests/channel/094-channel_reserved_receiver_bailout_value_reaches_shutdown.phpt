--TEST--
Channel: a receiver woken with a value and unwound by a bailout before it runs gives the value back to the channel
--INI--
memory_limit=8M
--FILE--
<?php

use Async\Channel;
use function Async\spawn;

$channel = new Channel(1);

register_shutdown_function(function () use ($channel) {
    echo "shutdown: ", $channel->recv(), "\n";
});

spawn(function () use ($channel) {
    $channel->recv();
    echo "not reached\n";
});

spawn(function () use ($channel) {
    $channel->sendAsync("the-value");
    str_repeat('x', 10000000);
});
?>
--EXPECTF--
Fatal error: Allowed memory size of %d bytes exhausted%s(tried to allocate %d bytes) in %s on line %d
%Ashutdown: the-value
