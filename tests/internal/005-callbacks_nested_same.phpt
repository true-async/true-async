--TEST--
Callbacks vector: a nested notify of the vector being notified is refused
--FILE--
<?php
echo TrueAsync\Test\callbacks_scenario('nested-same'), "\n";
?>
--EXPECT--
A(refused)B
