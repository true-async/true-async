--TEST--
Circular buffer: the pointer helpers refuse full and empty buffers, swap by offset, grow on demand
--FILE--
<?php
echo TrueAsync\Test\buffer_scenario('ptr'), "\n";
?>
--EXPECT--
empty=1 full=1 capacity=7:CBAD
