--TEST--
Callbacks vector: the first callback that throws ends the notify, as in TrueAsync; its exception chains over the entry one
--FILE--
<?php
echo TrueAsync\Test\callbacks_scenario('throw-stops'), "\n";
?>
--EXPECT--
A caught:a<entry left=3
