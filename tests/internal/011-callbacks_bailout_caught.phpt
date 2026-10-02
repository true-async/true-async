--TEST--
Callbacks vector: a caught bailout from a nested notify leaves the vector notifiable and restores the scheduler-context flag
--FILE--
<?php
echo TrueAsync\Test\callbacks_scenario('bailout-caught'), "\n";
?>
--EXPECT--
AW(caught)B again:W sched=0
