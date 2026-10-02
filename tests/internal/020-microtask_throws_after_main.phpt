--TEST--
After main the scheduler coroutine runs the tick on its own stack: a microtask that throws there ends the request as the exit exception
--FILE--
<?php
use TrueAsync\Test;

Test\defer('A');
Test\defer('B', 'throw');
echo "end\n";
?>
--EXPECT--
end
microtask A sched=1
released A
microtask B sched=1
released B

Fatal error: Uncaught Exception: microtask B in [no active file]:0
Stack trace:
#0 {main}
  thrown in [no active file] on line 0
