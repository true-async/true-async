--TEST--
A holder's start of a trigger counts it until its stop
--FILE--
<?php
use TrueAsync\Test;

Test\trigger_new();
Test\trigger_start();
Test\trigger_start();
echo "started twice: ", Test\reactor_state()['started'], "\n";
Test\trigger_stop();
echo "one stop: ", Test\reactor_state()['started'], "\n";
Test\trigger_stop();
echo "stopped: ", Test\reactor_state()['started'], "\n";
?>
--EXPECT--
started twice: 1
one stop: 1
stopped: 0
