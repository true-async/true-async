--TEST--
Channel: a recvAsync() Future dropped while its map() child is pending stays queued, and the child gets the value
--FILE--
<?php

use Async\Channel;

$channel = new Channel(1);
// The child, ignored and dropped too, keeps its source, as TrueAsync's does.
$channel->recvAsync()->map(function ($value) {
    echo "mapped: ", $value, "\n";
})->ignore();

$channel->send('value');
Async\suspend();
echo "count: ", count($channel), "\n";
?>
--EXPECT--
mapped: value
count: 0
