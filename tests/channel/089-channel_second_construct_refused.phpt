--TEST--
Channel: a second __construct() throws Error and keeps the values and the capacity
--FILE--
<?php

use Async\Channel;

$channel = new Channel(2);
$channel->sendAsync(str_repeat('a', 100));
$channel->sendAsync(str_repeat('b', 100));

try {
    $channel->__construct(4);
} catch (Error $error) {
    echo get_class($error), ": ", $error->getMessage(), "\n";
}

echo "count: ", count($channel), ", capacity: ", $channel->capacity(), "\n";
echo "recv: ", $channel->recv()[0], $channel->recv()[0], "\n";
?>
--EXPECT--
Error: Cannot call constructor twice
count: 2, capacity: 2
recv: ab
