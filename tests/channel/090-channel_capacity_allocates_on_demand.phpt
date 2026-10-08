--TEST--
Channel: a large capacity allocates nothing until values arrive
--FILE--
<?php

use Async\Channel;

$before = memory_get_usage();
$large = new Channel(1 << 24);
$largest = new Channel(2147483647);
echo "grown KB: ", intdiv(memory_get_usage() - $before, 1024), "\n";

$largest->sendAsync(1);
$largest->sendAsync(2);
echo "count: ", count($largest), ", capacity: ", $largest->capacity(), "\n";
?>
--EXPECT--
grown KB: 0
count: 2, capacity: 2147483647
