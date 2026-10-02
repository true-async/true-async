--TEST--
asHiPriority() puts the coroutine's next enqueue at the front of the run queue, once (D20, D35)
--XFAIL--
Not implemented yet: S3.6 of dev/PLAN.md
--FILE--
<?php
use function Async\spawn;
use function Async\suspend;

$task = function (string $name) {
    echo "$name 1\n";
    suspend();
    echo "$name 2\n";
    suspend();
    echo "$name 3\n";
};

spawn($task, 'A');
spawn($task, 'B');
spawn($task, 'C')->asHiPriority();
?>
--EXPECT--
A 1
B 1
C 1
C 2
A 2
B 2
C 3
A 3
B 3
