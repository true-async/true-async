--TEST--
Channel: getIterator() returns an Iterator over the channel, with null keys
--FILE--
<?php

use Async\Channel;

$channel = new Channel(2);
$channel->sendAsync('a');
$channel->sendAsync('b');
$channel->close();

$iterator = $channel->getIterator();
echo get_class($iterator), ", Iterator: ", var_export($iterator instanceof Iterator, true), "\n";

foreach ($iterator as $key => $value) {
    echo var_export($key, true), " => $value\n";
}

echo "count: ", count($channel), "\n";
?>
--EXPECT--
InternalIterator, Iterator: true
NULL => a
NULL => b
count: 0
