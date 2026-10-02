--TEST--
Callbacks vector: a notify puts back the scheduler-context flag it found, set or clear
--FILE--
<?php
echo TrueAsync\Test\callbacks_scenario('sched-kept'), "\n";
?>
--EXPECT--
outside:A sched=0 inside:A sched=1
