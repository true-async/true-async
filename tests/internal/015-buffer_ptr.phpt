--TEST--
Circular buffer: the pointer helpers refuse an empty and a full wrapped buffer, swap by offset across the wrap and next to the tail, grow on demand
--FILE--
<?php
echo TrueAsync\Test\buffer_scenario('ptr'), "\n";
?>
--EXPECT--
empty=1 full=1 slots=8:CABD
