--TEST--
Channel: recvAsync() Futures still pending at a fatal error's end are freed after their channel without touching it
--FILE--
<?php

use Async\Channel;

// The global variables are destroyed in the reverse order of their first use: the channel goes first.
$first = $second = null;
$channel = new Channel();

$first = $channel->recvAsync();
$first->ignore();
$second = $channel->recvAsync();
$second->ignore();

register_shutdown_function(function () {
    echo "shutdown\n";
});

trigger_error("stop here", E_USER_ERROR);
?>
--EXPECTF--
Deprecated: Passing E_USER_ERROR to trigger_error() is deprecated since 8.4, throw an exception or call exit with a string message instead in %s on line %d

Fatal error: stop here in %s on line %d
shutdown
