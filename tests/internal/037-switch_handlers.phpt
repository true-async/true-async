--TEST--
Switch handlers: growth past four, an add of a registered handler, removal by id in order, a call keeping those that return true, the vector freed with the last
--FILE--
<?php
echo TrueAsync\Test\callbacks_scenario('switch-handlers'), "\n";
?>
--EXPECT--
remove-none=0 same=1,1 removed-c=1 again=0 length=4 leave:ABDE length=3 enter:ADE removed-e=1 again-e=0 length=1 freed=1
