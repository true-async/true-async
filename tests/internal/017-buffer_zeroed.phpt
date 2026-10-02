--TEST--
Circular buffer: a zero-filled buffer that was never constructed reads as empty
--FILE--
<?php
echo TrueAsync\Test\buffer_scenario('zeroed'), "\n";
?>
--EXPECT--
count=0 empty=1 not_empty=0 pop=1
