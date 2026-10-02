--TEST--
Callbacks vector: removing a callback that already ran keeps every pending one, each run once
--FILE--
<?php
echo TrueAsync\Test\callbacks_scenario('remove-run'), "\n";
?>
--EXPECT--
ABDC length=3
