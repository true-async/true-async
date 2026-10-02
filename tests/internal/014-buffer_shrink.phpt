--TEST--
Circular buffer: an underused buffer halves on push, keeping the order, unless auto_optimize is off
--FILE--
<?php
echo TrueAsync\Test\buffer_scenario('shrink'), "\n";
?>
--EXPECT--
slots 64->32: 31 32 33 off: slots 64->64: 31 32 33
