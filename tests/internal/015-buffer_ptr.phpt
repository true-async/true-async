--TEST--
Circular buffer: on a wrapped buffer the pointer helpers refuse full and empty, swap by offset across the wrap, grow on demand
--FILE--
<?php
echo TrueAsync\Test\buffer_scenario('ptr'), "\n";
?>
--EXPECT--
empty=1 full=1 capacity=7:ACBD
