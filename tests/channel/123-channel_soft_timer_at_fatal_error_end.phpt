--TEST--
Channel: a soft timer armed with a pending recvAsync() Future and a parked receiver at a fatal error's end is withdrawn cleanly
--FILE--
<?php

use Async\Channel;
use Async\ChannelException;
use function Async\spawn;

$channel = new Channel(0, 10000);
$future = $channel->recvAsync();
$future->ignore();

spawn(function () use ($channel) {
    try {
        $channel->recv();
    } catch (Throwable $exception) {
        echo "receiver: ", get_class($exception), "\n";
    }
});
Async\suspend();

register_shutdown_function(function () use ($channel) {
    echo "shutdown: closed=", var_export($channel->isClosed(), true), "\n";
});

trigger_error("stop here", E_USER_ERROR);
?>
--EXPECTF--
Deprecated: Passing E_USER_ERROR to trigger_error() is deprecated since 8.4, throw an exception or call exit with a string message instead in %s on line %d

Fatal error: stop here in %s on line %d
shutdown: closed=false
