--TEST--
Channel: after a bailout the value a parked rendezvous sender was delivering stays for the next receiver
--INI--
memory_limit=8M
--FILE--
<?php

use Async\Channel;
use function Async\spawn;
use function Async\suspend;

$channel = new Channel(0);

register_shutdown_function(function () use ($channel) {
    echo "shutdown: count ", count($channel), ", got ", $channel->recv(), "\n";
});

spawn(function () use ($channel) {
    $channel->send("the-value");
    echo "not reached\n";
});

spawn(function () {
    suspend();
    str_repeat('x', 10000000);
});
?>
--EXPECTF--
Fatal error: Allowed memory size of %d bytes exhausted%s(tried to allocate %d bytes) in %s on line %d
%Ashutdown: count 1, got the-value
