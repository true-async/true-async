--TEST--
Callbacks vector: removing a callback that has not run yet, at the cursor, keeps it from running and runs every other one once
--FILE--
<?php
echo TrueAsync\Test\callbacks_scenario('remove-pending'), "\n";
?>
--EXPECT--
ADC length=3
