--TEST--
Circular buffer: growing a wrapped buffer keeps the order, its head at slot 0 too
--FILE--
<?php
echo TrueAsync\Test\buffer_scenario('wrap-grow'), "\n";
?>
--EXPECT--
full=1 1 2 slots=8: 3 4 5 6 count=0 1 slots=8: 2 3 4 5
