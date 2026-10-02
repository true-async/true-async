--TEST--
Circular buffer: push_front with resize on a full wrapped buffer grows first and goes ahead of the rest
--FILE--
<?php
echo TrueAsync\Test\buffer_scenario('front-full'), "\n";
?>
--EXPECT--
 1 2 full=1 capacity=7: 9 10 11 12
