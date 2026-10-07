--TEST--
Callbacks vector: a callback moved by a removal during a notify, the cursor rule's coinciding positions included, keeps the slot it sits at
--FILE--
<?php
echo TrueAsync\Test\callbacks_scenario('slots'), "\n";
?>
--EXPECT--
ABDC slots right removed:111 length=0; ABC slots right removed:11 length=0
