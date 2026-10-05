--TEST--
Circular buffer: count on a wrapped buffer, its head behind its tail
--FILE--
<?php
echo TrueAsync\Test\buffer_scenario('count-wrapped'), "\n";
?>
--EXPECT--
 1 2 head=0 tail=2 count=2 head=1 count=3 full=1: 3 4 5
