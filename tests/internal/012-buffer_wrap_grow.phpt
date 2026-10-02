--TEST--
Circular buffer: growing a wrapped buffer keeps the order
--FILE--
<?php
echo TrueAsync\Test\buffer_scenario('wrap-grow'), "\n";
?>
--EXPECT--
full=1 1 2 capacity=7: 3 4 5 6 count=0
