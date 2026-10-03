--TEST--
Callbacks vector: removing a callback that was never added finds nothing and changes nothing
--FILE--
<?php
echo TrueAsync\Test\callbacks_scenario('remove-absent'), "\n";
?>
--EXPECT--
removed=0 length=3 ran:ABC
