--TEST--
Callbacks vector: a callback that tears its vector down and adds another; the added one runs in the same notify
--FILE--
<?php
echo TrueAsync\Test\callbacks_scenario('free-add'), "\n";
?>
--EXPECT--
AP
