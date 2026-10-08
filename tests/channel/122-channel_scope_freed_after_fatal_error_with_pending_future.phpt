--TEST--
Channel: an owner scope freed after a fatal error, with a recvAsync() Future still pending, leaves the channel open and frees cleanly
--FILE--
<?php

use Async\Channel;
use Async\Scope;
use function Async\await;

$scope = new Scope();
[$channel, $future] = await($scope->spawn(function () {
    $channel = new Channel(0);
    $future = $channel->recvAsync();
    $future->ignore();

    return [$channel, $future];
}));

register_shutdown_function(function () use ($channel) {
    echo "shutdown: closed=", var_export($channel->isClosed(), true), "\n";
});

trigger_error("stop here", E_USER_ERROR);
?>
--EXPECTF--
Deprecated: Passing E_USER_ERROR to trigger_error() is deprecated since 8.4, throw an exception or call exit with a string message instead in %s on line %d

Fatal error: stop here in %s on line %d
shutdown: closed=false
