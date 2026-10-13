--TEST--
A stop of a trigger not started does nothing: a start after it counts the trigger as started
--FILE--
<?php
use TrueAsync\Test;

Test\trigger_new();
Test\trigger_stop();
Test\trigger_stop();
Test\trigger_start();
echo "started: ", Test\reactor_state()['started'], "\n";
?>
--EXPECT--
started: 1
