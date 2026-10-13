--TEST--
A holder's start of a trigger counts it until the trigger's free
--FILE--
<?php
use TrueAsync\Test;

Test\trigger_new();
Test\trigger_start();
Test\trigger_free();
echo "freed while started: ", Test\reactor_state()['started'], "\n";
?>
--EXPECT--
freed while started: 0
