--TEST--
Callbacks vector: freeing a vector that was never notified disposes each callback once
--FILE--
<?php
echo TrueAsync\Test\callbacks_scenario('free-disposes'), "\n";
?>
--EXPECT--
disposed:abc length=0
