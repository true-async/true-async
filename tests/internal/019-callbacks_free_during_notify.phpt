--TEST--
Callbacks vector: a callback that tears its own vector down ends the notify; the vector is usable again
--FILE--
<?php
echo TrueAsync\Test\callbacks_scenario('free-during'), "\n";
?>
--EXPECT--
A then:B
