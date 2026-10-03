--TEST--
Switch handlers: a handler that adds or removes switch handlers of its coroutine while they run gets a warning and a refusal
--FILE--
<?php
echo TrueAsync\Test\callbacks_scenario('switch-handlers-running'), "\n";
?>
--EXPECTF--
Warning: Cannot add a switch handler while the switch handlers run in %s on line %d

Warning: Cannot remove a switch handler while the switch handlers run in %s on line %d
add=0 remove=0 freed=1
