--TEST--
Callbacks vector: a callback added during a notify runs in that notify
--FILE--
<?php
echo TrueAsync\Test\callbacks_scenario('add-during'), "\n";
?>
--EXPECT--
ABC
