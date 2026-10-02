--TEST--
Callbacks vector: a caught bailout from a nested notify leaves no stale frame or bit and restores the scheduler-context flag
--FILE--
<?php
echo TrueAsync\Test\callbacks_scenario('bailout-caught'), "\n";
?>
--EXPECT--
AW(caught)B again:W depth=0 sched=0
