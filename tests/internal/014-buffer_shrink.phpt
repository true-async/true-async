--TEST--
Circular buffer: a buffer filled from tail 0 grows twice and keeps the order
--FILE--
<?php
echo TrueAsync\Test\buffer_scenario('grow'), "\n";
?>
--EXPECT--
slots=16: 1 2 3 4 5 6 7 8
