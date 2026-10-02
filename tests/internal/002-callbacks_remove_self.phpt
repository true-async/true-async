--TEST--
Callbacks vector: a callback removing itself does not run again; the rest run once
--FILE--
<?php
echo TrueAsync\Test\callbacks_scenario('remove-self'), "\n";
?>
--EXPECT--
ACB CB
