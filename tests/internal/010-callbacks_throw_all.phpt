--TEST--
Callbacks vector: every callback runs after a throw, with no exception pending; the exceptions chain over the entry one
--FILE--
<?php
echo TrueAsync\Test\callbacks_scenario('throw-all'), "\n";
?>
--EXPECT--
ABC caught:c<a<entry
