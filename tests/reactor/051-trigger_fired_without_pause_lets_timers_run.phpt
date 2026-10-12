--TEST--
A trigger fired again at every wake, as by a thread that fires without pause, does not keep the poll from running the timers (dev/SECURITY.md, S4.7)
--FILE--
<?php
use TrueAsync\Test;

// A poll that took the wakeup's completions until none came would wait for the million refires to run out
// before the timer of the delay could fire.
Test\trigger_new();
Test\trigger_refire(1000000);
Test\trigger_fire();

Async\delay(1);
echo "delay returned\n";

Test\trigger_free();
echo "end\n";
?>
--EXPECT--
delay returned
end
