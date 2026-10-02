--TEST--
Callbacks vector: a caught bailout from a nested notify leaves that vector marked (refused) and the outer notify restores the scheduler-context flag
--FILE--
<?php
echo TrueAsync\Test\callbacks_scenario('bailout-caught'), "\n";
?>
--EXPECT--
AW(caught)B again: sched=0
