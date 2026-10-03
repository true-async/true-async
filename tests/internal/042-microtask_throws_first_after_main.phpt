--TEST--
After main a microtask that throws stops the tick with nothing queued: the scheduler coroutine runs the next tick for the microtask behind it before the request ends with the exception
--FILE--
<?php
use TrueAsync\Test;

Test\defer('A', 'throw');
Test\defer('B');
echo "end\n";
?>
--EXPECT--
end
microtask A sched=1
released A
microtask B sched=1
released B

Fatal error: Uncaught Exception: microtask A in [no active file]:0
Stack trace:
#0 {main}
  thrown in [no active file] on line 0
